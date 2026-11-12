#pragma once
#include "box3d/id.h"

namespace pulsar::Components
{
    struct RigidBody
    {
        b3BodyId bodyId;

        // Has Transform force-updated since last physics tick?
        bool isTeleportDirty;
    };
}