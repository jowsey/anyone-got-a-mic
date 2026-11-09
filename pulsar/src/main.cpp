#if _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <print>

#include "env/BatchedEnvironment.h"

#include "CLI/CLI.hpp"
#include "spdlog/spdlog.h"

int main(const int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    CLI::App app{"high-performance reinforcement learning environment"};
    app.option_defaults()->always_capture_default();
    app.get_formatter()->enable_option_type_names(false);
    app.fallthrough(false);

    argv = app.ensure_utf8(argv);

    std::uint32_t numThreads = std::thread::hardware_concurrency();

    const auto batchSizeOpt = app.add_option("-b,--batch", "number of parallel environments")->default_val(512);
    const auto threadCountOpt = app.add_option("-t,--threads", "number of worker threads (or 'auto')")
                                    ->transform([numThreads](const std::string& str) {
                                        return str == "auto" ? std::to_string(numThreads / 2) : str;
                                    })
                                    ->check(CLI::Range(1, 8192))
                                    ->default_val("auto");
    const auto tickRateOpt = app.add_option("--tick-rate", "simulation tick rate (Hz)")->default_val(64);

    app.parse_complete_callback([&] {
        std::println("\n  💫 pulsar v{}\n", PULSAR_VERSION);
    });

    app.callback([&] {
        if (!app.get_subcommands().empty()) return;

        const auto batchSize = batchSizeOpt->as<std::size_t>();
        const auto numThreads = threadCountOpt->as<std::uint8_t>();
        const auto tickRateHz = tickRateOpt->as<float>();

        const auto batch = pulsar::BatchedEnvironment("default", batchSize, numThreads);
        const float fixedDeltaTime = 1.0f / tickRateHz;

        while (batch.stepAll(fixedDeltaTime))
        {
        }
    });

    const auto benchmarkCmd = app.add_subcommand("benchmark", "benchmark batch & thread counts");
    const auto benchmarkIntervalOpt = benchmarkCmd->add_option("-i,--interval", "benchmark interval (seconds)")->default_val(5);
    const auto benchmarkThreadStepOpt = benchmarkCmd->add_option("-s,--step", "thread step size (threads)")->default_val(2);

    benchmarkCmd->callback([&] {
        const auto interval = benchmarkIntervalOpt->as<float>();
        const auto threadStep = benchmarkThreadStepOpt->as<std::uint32_t>();

        std::unordered_map<std::uint32_t, std::uint32_t> results;
        constexpr float fixedDeltaTime = 1.0f / 64.0f;

        const std::uint32_t numBenchmarks = numThreads / threadStep;
        std::string timeString;

        float estTotalSeconds = static_cast<float>(numBenchmarks) * interval;
        if (estTotalSeconds > 3600) timeString += std::to_string(static_cast<std::uint32_t>(estTotalSeconds / 3600.0f)) + "h";
        if (estTotalSeconds > 60) timeString += std::to_string(static_cast<std::uint32_t>(estTotalSeconds / 60.0f) % 60) + "m";
        timeString += std::to_string(static_cast<std::uint32_t>(estTotalSeconds) % 60) + "s";

        spdlog::info("Benchmarking {}..{} threads ({} runs), est. {}", threadStep, numThreads, numBenchmarks, timeString);

        for (std::uint32_t benchThreads = threadStep; benchThreads <= numThreads; benchThreads += threadStep)
        {
            const auto batch = pulsar::BatchedEnvironment("benchmark", 2048, benchThreads);

            std::uint32_t ticksRan = 0;
            const auto startTime = std::chrono::steady_clock::now();

            while (true)
            {
                if (!batch.stepAll(fixedDeltaTime))
                {
                    spdlog::error("Batch failed to step (?)");
                    break;
                }

                ++ticksRan;

                const auto elapsedTime = std::chrono::steady_clock::now() - startTime;
                if (std::chrono::duration<float>(elapsedTime).count() >= interval)
                {
                    break;
                }
            }

            results[benchThreads] = ticksRan;
            spdlog::info("{} threads: {} ticks in {}s ({}/s)", benchThreads, ticksRan, interval, static_cast<float>(ticksRan) / interval);
        }

        const auto fastest = std::ranges::max_element(results, [](const auto& a, const auto& b) {
            return a.second < b.second;
        });

        spdlog::info("Fastest: {} threads ({}/s)", fastest->first, static_cast<float>(fastest->second) / interval);
    });

    CLI11_PARSE(app, argc, argv);
    return 0;
}
