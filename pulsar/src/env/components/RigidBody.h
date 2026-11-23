#pragma once
#include "box3d/box3d.h"

namespace pulsar::Components
{
    struct RigidBody
    {
        b3WorldId worldId;
        b3BodyId bodyId;
    };
}