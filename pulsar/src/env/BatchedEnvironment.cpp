#include "BatchedEnvironment.h"

#include "box3d/box3d.h"
#include "box3d/types.h"
#include "components/EnvironmentId.h"
#include "flecs.h"
#include "spdlog/spdlog.h"
#include "systems/Transform.h"

#include <algorithm>

/// Create a new batched environment
/// @param batchName Descriptive name for this batch
/// @param batchSize Number of environments to simulate per thread
/// @param numThreads Number of parallel threads to use
pulsar::BatchedEnvironment::BatchedEnvironment(const std::string& batchName, const std::size_t batchSize, const std::uint8_t numThreads)
    : name(batchName)
{
    spdlog::info("Launching batch '{}' with {} environments across {} thread(s)", batchName, batchSize * numThreads, numThreads);
    assert(batchSize <= B3_MAX_WORLDS && "Batch size exceeds max Box3D worlds. Increase B3_MAX_WORLDS in CMakeLists.txt.");

    batches.reserve(numThreads);

    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.gravity = b3Vec3{.x = 0.0f, .y = -9.81f, .z = 0.0f};
    worldDef.workerCount = 1;

    for (std::size_t i = 0; i < numThreads; ++i)
    {
        auto batch = Batch{};

        for (std::size_t j = 0; j < batchSize; ++j)
        {
            // Create env root
            const auto root = batch.ecsWorld.entity().set<Components::EnvironmentId>({i});
            batch.envRoots.push_back(root);

            // Create physics world
            const auto b3WorldId = b3CreateWorld(&worldDef);
            batch.b3WorldIds.push_back(b3WorldId);
        }

        // Transform system
        Systems::Transform::Register(batch.ecsWorld);

        batches.push_back(batch);
    }
}
pulsar::BatchedEnvironment::~BatchedEnvironment()
{
    for (const auto& batch : batches)
    {
        for (const auto& b3WorldId : batch.b3WorldIds)
        {
            b3DestroyWorld(b3WorldId);
        }
    }
}

bool pulsar::BatchedEnvironment::stepAll(const float dt) const
{
    // todo dispatch to threads
    return std::ranges::all_of(batches, [dt](const auto& batch) {
        return batch.ecsWorld.progress(dt);
    });
}