#include "BatchedEnvironment.h"

#include "box3d/box3d.h"
#include "box3d/types.h"
#include "components/BoxCollider.h"
#include "components/Environment.h"
#include "components/EnvironmentId.h"
#include "components/MeshRenderer.h"
#include "components/RigidBody.h"
#include "components/Transform.h"
#include "flecs.h"
#include "spdlog/spdlog.h"
#include "systems/Physics.h"
#include "systems/Transform.h"

#include <barrier>
#include <random>

/// Create a new batched environment
/// @param batchName Descriptive name for this batch
/// @param batchSize Number of environments to simulate per thread
/// @param numThreads Number of parallel threads to use
/// @param fixedDeltaTime Fixed tick time-step
pulsar::BatchedEnvironment::BatchedEnvironment(const std::string& batchName, const std::size_t batchSize, const std::uint32_t numThreads, const float fixedDeltaTime)
    : name(batchName), jobsAvailableBarrier(numThreads + 1), workDoneBarrier(numThreads + 1)
{
    spdlog::info("Launching batch '{}' with {} environments across {} thread(s)", batchName, batchSize * numThreads, numThreads);
    assert(batchSize * numThreads <= B3_MAX_WORLDS && "Batch size exceeds max Box3D worlds. Increase B3_MAX_WORLDS in CMakeLists.txt.");

    threadBatches.reserve(numThreads);

    b3WorldDef worldDef = b3DefaultWorldDef();
    worldDef.gravity = b3Vec3{.x = 0.0f, .y = -9.81f, .z = 0.0f};
    worldDef.workerCount = 1;

    b3BodyDef floorBodyDef = b3DefaultBodyDef();
    floorBodyDef.type = b3_staticBody;

    b3BodyDef blockBodyDef = b3DefaultBodyDef();
    blockBodyDef.type = b3_dynamicBody;

    for (std::size_t threadI = 0; threadI < numThreads; ++threadI)
    {
        auto& threadBatch = threadBatches.emplace_back();
        threadBatch.envRoots.reserve(batchSize);
        threadBatch.b3WorldIds.reserve(batchSize);
        threadBatch.agentObservations.resize(batchSize);

        for (std::size_t envI = 0; envI < batchSize; ++envI)
        {
            threadBatch.agentObservations[envI].reserve(128); // todo formalize number of agents per env

            // Create physics world
            const auto b3WorldId = b3CreateWorld(&worldDef);
            threadBatch.b3WorldIds.push_back(b3WorldId);

            // Create env root
            const auto root = threadBatch.sharedEcs.entity()
                                  .set<Components::Environment>({b3WorldId})
                                  .set<Components::EnvironmentId>({envI});
            threadBatch.envRoots.push_back(root);
        }

        // Transform system
        Systems::Transform::Register(threadBatch.sharedEcs);
        // Physics system
        Systems::Physics::Register(threadBatch.sharedEcs, threadBatch.b3WorldIds, fixedDeltaTime);

        for (std::size_t envI = 0; envI < threadBatch.b3WorldIds.size(); ++envI)
        {
            const auto root = threadBatch.envRoots[envI];
            const auto b3WorldId = threadBatch.b3WorldIds[envI];

            // Create ground floor
            auto floor = createEnvObject("EnvFloor" + std::to_string(envI), threadBatch.sharedEcs, root);
            floorBodyDef.userData = reinterpret_cast<void*>(floor.id());

            (void)floor
                .set<Components::Transform>({.localPosition = glm::vec3{0.0f, -0.25f, 0.0f}, .localScale = glm::vec3{10.0f, 0.25f, 10.0f}})
                .set<Components::RigidBody>({.worldId = b3WorldId, .bodyId = b3CreateBody(b3WorldId, &floorBodyDef)})
                .set<Components::BoxCollider>({.extents = glm::vec3{5.0f, 0.125f, 5.0f}})
                .add<Components::MeshRenderer>();

            // Create falling blocks
            for (std::size_t i = 0; i < 5; ++i)
            {
                std::mt19937 rng{std::random_device{}()};
                std::uniform_real_distribution<float> dist(-10.0f, 10.0f);

                auto block = createEnvObject("Block" + std::to_string(envI) + "_" + std::to_string(i + 1), threadBatch.sharedEcs, root);
                blockBodyDef.userData = reinterpret_cast<void*>(block.id());

                (void)block
                    .set<Components::Transform>({.localPosition = glm::vec3{dist(rng), 5.0f, dist(rng)}})
                    .set<Components::RigidBody>({.worldId = b3WorldId, .bodyId = b3CreateBody(b3WorldId, &blockBodyDef)})
                    .set<Components::BoxCollider>({.extents = glm::vec3{0.5f}})
                    .add<Components::MeshRenderer>();
            }
        }

        // Start worker
        threadBatch.worker = std::jthread([&, threadI, fixedDeltaTime](const std::stop_token& stopToken) {
            spdlog::debug("[Worker {}] Worker started", threadI);

            while (true)
            {
                jobsAvailableBarrier.arrive_and_wait();

                if (stopToken.stop_requested()) break;

                const auto ecsContinue = threadBatch.sharedEcs.progress(fixedDeltaTime);
                if (!ecsContinue)
                {
                    spdlog::warn("[Worker {}] ECS has quit?", threadI);
                }

                workDoneBarrier.arrive_and_wait();
            }

            spdlog::debug("[Worker {}] Worker stopped", threadI);
        });
    }
}

pulsar::BatchedEnvironment::~BatchedEnvironment()
{
    // Finish all work
    for (auto& batch : threadBatches)
    {
        batch.worker.request_stop();
    }

    jobsAvailableBarrier.arrive_and_wait();

    for (auto& batch : threadBatches)
    {
        if (batch.worker.joinable())
        {
            batch.worker.join();
        }

        for (const auto& b3WorldId : batch.b3WorldIds)
        {
            b3DestroyWorld(b3WorldId);
        }
    }
}

void pulsar::BatchedEnvironment::stepAll()
{
    jobsAvailableBarrier.arrive_and_wait();
    workDoneBarrier.arrive_and_wait();
}

const pulsar::ThreadBatch& pulsar::BatchedEnvironment::getThreadBatch(const std::size_t index) const
{
    return threadBatches.at(index);
}

const std::vector<pulsar::ThreadBatch>& pulsar::BatchedEnvironment::getThreadBatches() const
{
    return threadBatches;
}

flecs::entity pulsar::BatchedEnvironment::createEnvObject(const std::string& name, const flecs::world& world, const flecs::entity& envRoot)
{
    const auto envId = envRoot.get<Components::EnvironmentId>().envId;

    spdlog::debug("Creating env object '{}' in env {}", name, envId);

    return world.entity(flecs::Parent{envRoot}, name.c_str())
        .set<Components::EnvironmentId>({.envId = envId})
        .add<Components::Transform>();
}
