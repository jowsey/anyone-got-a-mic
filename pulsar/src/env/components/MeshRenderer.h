#pragma once
#include <cstdint>

namespace pulsar::Components
{
    struct MeshRenderer
    {
        std::optional<std::uint32_t> gpuHandle{0}; // todo don't hardcode a damn cube
    };
}
