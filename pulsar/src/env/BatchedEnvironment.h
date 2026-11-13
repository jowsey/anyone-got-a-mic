#pragma once
#include <thread>
#include <vector>

#include "box3d/id.h"
#include "flecs.h"
#include "flecs/addons/cpp/world.hpp"

namespace pulsar
{
    struct AgentObservation
    {
        float placeholder;
    };

    struct ThreadBatch
    {
        flecs::world sharedEcs;
        std::vector<flecs::entity> envRoots;
        std::vector<b3WorldId> b3WorldIds;
        std::vector<std::vector<AgentObservation>> agentObservations;

        std::jthread worker;
    };

    class BatchedEnvironment
    {
        std::string name;
        std::vector<ThreadBatch> batches;

      public:
        explicit BatchedEnvironment(const std::string& batchName, std::size_t batchSize, std::uint8_t numThreads, float fixedDeltaTime);

        ~BatchedEnvironment();

        [[nodiscard]] bool stepAll(float dt) const;
    };
}
