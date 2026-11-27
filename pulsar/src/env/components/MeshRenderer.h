#pragma once

namespace pulsar::Components
{
    struct MeshRenderer
    {
        std::optional<std::size_t> mesh;
        std::optional<std::size_t> albedo;
    };
}
