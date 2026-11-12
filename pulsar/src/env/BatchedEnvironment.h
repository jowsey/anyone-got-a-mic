#pragma once
#include <vector>

#include "box3d/id.h"
#include "flecs.h"
#include "flecs/addons/cpp/world.hpp"
#include ""

namespace pulsar
{
    /// A batch of environments belonging to a thread
    struct Batch
    {
        flecs::world ecsWorld;
        std::vector<flecs::entity> envRoots;
        std::vector<b3WorldId> b3WorldIds;
    };

    class BatchedEnvironment
    {
        std::string name;
        std::vector<Batch> batches;
        // BS::thread_pool threadPool;

      public:
        explicit BatchedEnvironment(const std::string& batchName, std::size_t batchSize = 1, std::uint8_t numThreads = 1);

        ~BatchedEnvironment();

        [[nodiscard]] bool stepAll(float dt) const;
    };
}
