#pragma once

#include "box3d/id.h"
#include "box3d/types.h"

namespace pulsar::Components
{
    struct MeshCollider
    {
        b3MeshData* meshData;
        b3ShapeId shapeId;
    };
}
