#pragma once
#include "glm/vec3.hpp"

namespace pulsar::Components
{
    struct BoxCollider
    {
        glm::vec3 center{0};
        glm::vec3 extents{0.5f};

        b3ShapeId shapeId;
    };
}
