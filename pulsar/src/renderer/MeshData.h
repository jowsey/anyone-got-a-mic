#pragma once

#include "glm/vec3.hpp"
#include "glm/vec4.hpp"

#include <vector>

namespace pulsar::Renderer
{
    struct Vertex
    {
        glm::vec3 position;
        glm::vec4 colour;
    };

    struct MeshData
    {
        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
    };
}
