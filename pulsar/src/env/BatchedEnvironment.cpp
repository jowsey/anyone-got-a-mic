#include "BatchedEnvironment.h"

#include "box3d/box3d.h"
#include "box3d/types.h"
#include "components/Environment.h"
#include "components/EnvironmentId.h"
#include "flecs.h"
#include "spdlog/spdlog.h"
#include "systems/Physics.h"
#include "systems/Transform.h"

#include <algorithm>

/// Create a new batched environment
/// @param batchName Descriptive name for this batch
/// @param batchSize Number of environments to simulate per thread
/// @param numThreads Number of parallel threads to use
/// @param fixedDeltaTime Fixed tick time-step
pulsar::BatchedEnvironment::BatchedEnvironment(const std::string& batchName, const std::size_t batchSize, const std::uint8_t numThreads, const float fixedDeltaTime)
    : name(batchName)
{
    spdlog::info("Launching batch '{}' with {} environments across {} thread(s)", batchName, batchSize * numThreads, numThreads);
    assert(batchSize <= B3_MAX_WORLDS && "Batch size exceeds max Box3D worlds. Increase B3_MAX_WORLDS in CMakeLists.txt.");

    batches.reserve(numThreads);

    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.gravity = b3Vec3{.x = 0.0f, .y = -9.81f, .z = 0.0f};
    worldDef.workerCount = 1;

    for (std::size_t threadI = 0; threadI < numThreads; ++threadI)
    {
        auto& batch = batches.emplace_back();
        batch.envRoots.reserve(batchSize);
        batch.b3WorldIds.reserve(batchSize);
        batch.agentObservations.reserve(batchSize);

        for (std::size_t envI = 0; envI < batchSize; ++envI)
        {
            batch.agentObservations[envI].reserve(128); // todo formalize number of agents per env

            // Create physics world
            const auto b3WorldId = b3CreateWorld(&worldDef);
            batch.b3WorldIds.push_back(b3WorldId);

            // Create env root
            const auto root = batch.sharedEcs.entity()
                                  .set<Components::Environment>({b3WorldId})
                                  .set<Components::EnvironmentId>({threadI * batchSize + envI}); // todo does this need to be unique across batches?
            batch.envRoots.push_back(root);
        }

        // Transform system
        Systems::Transform::Register(batch.sharedEcs);
        // Physics system
        Systems::Physics::Register(batch.sharedEcs, batch.b3WorldIds, fixedDeltaTime);
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
    return std::ranges::all_of(batches, [dt](const auto& batch) {
        return batch.sharedEcs.progress(dt);
    });
}