#if _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <print>

#include "env/BatchedEnvironment.h"

#include "CLI/CLI.hpp"
#include "spdlog/spdlog.h"

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    CLI::App app{"high-performance reinforcement learning environment"};
    app.option_defaults()->always_capture_default();
    app.get_formatter()->enable_option_type_names(false);

    argv = app.ensure_utf8(argv);

    std::uint32_t autoThreadCount = std::thread::hardware_concurrency() / 2;

    std::uint32_t batchSize = 512;
    std::uint32_t numThreads = autoThreadCount;
    float tickRateHz = 64;

    app.add_option("-b,--batch", batchSize, "number of parallel environments");

    app.add_option("-t,--threads", numThreads, "number of worker threads (or 'auto')")
        ->transform([autoThreadCount](const std::string& str) {
            return str == "auto" ? std::to_string(autoThreadCount) : str;
        })
        ->check(CLI::Range(1, 8192))
        ->default_val("auto");

    app.add_option("--tick-rate", tickRateHz, "simulation tick rate (Hz)");

    CLI11_PARSE(app, argc, argv);

    std::println("\n  💫 pulsar v{}\n  https://github.com/jowsey/anyone-got-a-mic\n", PULSAR_VERSION);

    const auto batch = pulsar::BatchedEnvironment("default", batchSize, numThreads);
    const float fixedDeltaTime = 1.0f / tickRateHz;

    while (batch.stepAll(fixedDeltaTime))
    {
    }

    return 0;
}
