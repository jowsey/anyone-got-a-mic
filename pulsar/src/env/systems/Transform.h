#pragma once

#include "flecs.h"
#include "glm/detail/type_mat4x4.hpp"

namespace pulsar::Systems
{
    class Transform
    {
        static void processChildTransforms(const flecs::entity& parent, const glm::mat4& parentWorld, bool parentDirty);

      public:
        static void Register(const flecs::world& world);
    };
}
