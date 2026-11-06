#include "BatchedEnvironment.h"

#include "box3d/constants.h"
#include "components/EnvironmentId.h"
#include "flecs.h"
#include "spdlog/spdlog.h"
#include "systems/Transform.h"

pulsar::BatchedEnvironment::BatchedEnvironment(const std::string& batchName, const std::size_t batchSize, const std::uint8_t numThreads)
    : name(batchName)
{
    spdlog::info("Launching batch '{}' with {} environments across {} thread(s)", batchName, batchSize, numThreads);

    envRoots.reserve(batchSize);
    ecsWorld.set_threads(numThreads);
    box3dWorlds.reserve(std::min(batchSize, static_cast<std::size_t>(B3_MAX_WORLDS)));

    for (std::size_t i = 0; i < batchSize; ++i)
    {
        const auto root = ecsWorld.entity().set<Components::EnvironmentId>({i});
        envRoots.push_back(root);
    }

    // Transform system
    Systems::Transform::Register(ecsWorld);
}

bool pulsar::BatchedEnvironment::stepAll(const float dt) const
{
    return ecsWorld.progress(dt);
}