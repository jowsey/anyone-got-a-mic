#pragma once

#include <vector>

namespace pulsar::Renderer
{
    struct TextureData
    {
        std::uint32_t width;
        std::uint32_t height;
        std::vector<std::uint8_t> data;
    };
}
