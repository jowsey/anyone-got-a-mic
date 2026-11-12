#pragma once

#include "box3d/id.h"
#include "flecs.h"

#include <vector>

namespace pulsar::Systems
{
    class Physics
    {
      public:
        static void Register(const flecs::world& world, const std::vector<b3WorldId>& worldIds, float deltaTime);
    };
}