#pragma once

#include "flecs.h"

namespace pulsar::Systems
{
    class Transform
    {
      public:
        static void Register(const flecs::world& world);
    };
}
