#if _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "CLI/CLI.hpp"
#include "SDL3/SDL_timer.h"
#include "env/BatchedEnvironment.h"
#include "env/components/EnvironmentId.h"
#include "env/components/MeshRenderer.h"
#include "env/components/Transform.h"
#include "renderer/Primitives.h"
#include "renderer/RenderWindow.h"
#include "spdlog/spdlog.h"
#include "util/Fmt.h"

#include <latch>
#include <print>
#include <random>
#include <ranges>

int main(const int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::locale::global(std::locale(""));

    CLI::App app{"high-performance reinforcement learning environment"};
    app.option_defaults()->always_capture_default();
    app.get_formatter()->enable_option_type_names(false);
    app.fallthrough(false);

    argv = app.ensure_utf8(argv);

    // `pulsar`
    std::uint32_t hardwareThreads = std::thread::hardware_concurrency();

    const auto batchSizeOpt = app.add_option("-b,--batch-size", "number of environments per worker")->default_val(128);
    const auto threadCountOpt = app.add_option("-t,--threads", "number of worker threads (or 'auto')")
                                    ->transform([hardwareThreads](const std::string& str) {
                                        return str == "auto" ? std::to_string(hardwareThreads / 2) : str;
                                    })
                                    ->check(CLI::Range(1, 8192))
                                    ->default_val("auto");
    const auto tickRateOpt = app.add_option("--tick-rate", "simulation tick rate (Hz)")->default_val(64);
    const auto headlessFlag = app.add_flag("--headless", "run without rendering");

    app.parse_complete_callback([&] {
        std::println("\n  💫 pulsar v{}\n", PULSAR_VERSION);
        std::cout << std::flush;
    });

    app.callback([&] {
        if (!app.get_subcommands().empty()) return;

        const auto batchSize = batchSizeOpt->as<std::size_t>();
        const auto threadCount = threadCountOpt->as<std::uint32_t>();
        const auto tickRateHz = tickRateOpt->as<float>();
        const auto headless = headlessFlag->count() > 0;

        const float fixedDeltaTime = 1.0f / tickRateHz;
        auto batchedEnv = pulsar::BatchedEnvironment("default", batchSize, threadCount, fixedDeltaTime);

        std::unique_ptr<pulsar::Renderer::RenderWindow> window;

        std::latch finished{1};

        if (!headless)
        {
            // todo would be nice to change threadbatch at runtime but requires syncing with running systems
            window = std::make_unique<pulsar::Renderer::RenderWindow>(batchedEnv.getThreadBatch(0), 0);
            const auto cubeIndex = window->RegisterMesh(pulsar::Renderer::Primitives::CubeMesh);

            for (auto& threadBatch : batchedEnv.getThreadBatches())
            {
                for (auto& envRoot : threadBatch.envRoots)
                {
                    auto envId = envRoot.get<pulsar::Components::EnvironmentId>().envId;

                    std::mt19937 rng(std::random_device{}());
                    std::uniform_real_distribution<float> dist(-5.0f, 5.0f);

                    for (std::size_t i = 0; i < 5; i++)
                    {
                        auto randPosition = glm::vec3{dist(rng), 0, dist(rng)};

                        (void)threadBatch.sharedEcs.entity(flecs::Parent{envRoot}, std::format("Cube {}", i).c_str())
                            .set<pulsar::Components::EnvironmentId>({.envId = envId})
                            .set<pulsar::Components::Transform>({.localPosition = randPosition})
                            .set<pulsar::Components::MeshRenderer>(cubeIndex);
                    }
                }
            }
        }

        auto orchestratorThread = std::jthread([&](const std::stop_token& st) {
            while (!st.stop_requested())
            {
                batchedEnv.stepAll();
            }

            // todo give batch a way to say we're done?
            finished.count_down();
        });

        auto lastTime = SDL_GetTicksNS();
        while (window && !window->getIsQuitting())
        {
            const auto now = SDL_GetTicksNS();
            const auto deltaTime = static_cast<float>(now - lastTime) / 1'000'000'000.0f;

            window->pollEvents();
            window->render(deltaTime);

            lastTime = now;
        }

        if (headless)
        {
            // wait for orchestrator to finish
            finished.wait();
        }
        else
        {
            // window closed
            orchestratorThread.request_stop();
        }

        if (orchestratorThread.joinable()) orchestratorThread.join();

        if (window)
        {
            window->dispose();
        }
    });

    // `pulsar benchmark`
    const auto benchmarkCmd = app.add_subcommand("benchmark", "benchmark batch & thread counts");
    const auto benchmarkIntervalOpt = benchmarkCmd->add_option("-i,--interval", "benchmark interval (seconds)")->default_val(5);
    const auto benchmarkThreadStepOpt = benchmarkCmd->add_option("-s,--step", "thread step size (threads)")->default_val(2);

    benchmarkCmd->callback([&] {
        const auto interval = benchmarkIntervalOpt->as<float>();
        const auto threadStep = benchmarkThreadStepOpt->as<std::uint32_t>();

        std::unordered_map<std::uint32_t, std::uint32_t> results;
        constexpr float fixedDeltaTime = 1.0f / 64.0f;

        const std::uint32_t numBenchmarks = hardwareThreads / threadStep;
        std::string timeString;

        const float estTotalSeconds = static_cast<float>(numBenchmarks) * interval;
        if (estTotalSeconds > 3600) timeString += std::to_string(static_cast<std::uint32_t>(estTotalSeconds / 3600.0f)) + "h";
        if (estTotalSeconds > 60) timeString += std::to_string(static_cast<std::uint32_t>(estTotalSeconds / 60.0f) % 60) + "m";
        timeString += std::to_string(static_cast<std::uint32_t>(estTotalSeconds) % 60) + "s";

        spdlog::info("Benchmarking {}..{} threads ({} runs), est. {}", threadStep, hardwareThreads, numBenchmarks, timeString);

        for (std::uint32_t benchThreads = threadStep; benchThreads <= hardwareThreads; benchThreads += threadStep)
        {
            constexpr auto batchSize = 128;
            auto batch = pulsar::BatchedEnvironment("benchmark", batchSize, benchThreads, fixedDeltaTime);

            std::uint64_t totalEnvTicks = 0;
            const auto startTime = std::chrono::steady_clock::now();

            while (true)
            {
                batch.stepAll();

                totalEnvTicks += batchSize * benchThreads;

                const auto elapsedTime = std::chrono::steady_clock::now() - startTime;
                if (std::chrono::duration<float>(elapsedTime).count() >= interval)
                {
                    break;
                }
            }

            results[benchThreads] = totalEnvTicks;
            auto prettyTotal = pulsar::Utils::Fmt::prettyPrint(static_cast<double>(totalEnvTicks));
            auto prettyAvg = pulsar::Utils::Fmt::prettyPrint(static_cast<double>(totalEnvTicks) / interval);
            spdlog::info("{} threads: {} ticks in {}s ({}/s)", benchThreads, prettyTotal, interval, prettyAvg);
        }

        const auto fastest = std::ranges::max_element(results, [](const auto& a, const auto& b) {
            return a.second < b.second;
        });

        auto prettyBestAvg = pulsar::Utils::Fmt::prettyPrint(static_cast<double>(fastest->second) / interval);
        spdlog::info("Fastest: {} threads ({}/s)", fastest->first, prettyBestAvg);
    });

    CLI11_PARSE(app, argc, argv);
    return 0;
}