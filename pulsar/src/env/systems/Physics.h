#pragma once

#include "flecs.h"

namespace pulsar::Systems
{
    class Physics
    {
        static void Register(const flecs::world& world);
    };
}