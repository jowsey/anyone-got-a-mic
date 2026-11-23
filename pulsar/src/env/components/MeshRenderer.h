#pragma once

namespace pulsar::Components
{
    struct MeshHandleSet
    {
        std::optional<std::size_t> mesh;
        std::optional<std::size_t> albedo;
    };

    struct MeshRenderer
    {
        std::vector<MeshHandleSet> meshes{};
    };
}
