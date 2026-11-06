#pragma once
#include <vector>

#include "box3d/id.h"
#include "flecs.h"
#include "flecs/addons/cpp/world.hpp"

namespace pulsar
{
    class BatchedEnvironment
    {
        std::string name;

        flecs::world ecsWorld;

        std::vector<flecs::entity> envRoots;
        std::vector<b3WorldId> box3dWorlds;

      public:
        explicit BatchedEnvironment(const std::string& batchName, std::size_t batchSize = 1, std::uint8_t numThreads = 1);

        [[nodiscard]] bool stepAll(float dt) const;
    };
}
