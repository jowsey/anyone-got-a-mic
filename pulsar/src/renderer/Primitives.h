#pragma once
#include "RenderWindow.h"

namespace pulsar::Renderer::Primitives
{
    static const RenderWindow::MeshData CubeMesh{
        .vertices{
            // front
            {.position = {-0.5f, -0.5f, 0.5f}, .colour = {1.0f, 0.0f, 0.0f, 1.0f}},
            {.position = {0.5f, -0.5f, 0.5f}, .colour = {0.0f, 1.0f, 0.0f, 1.0f}},
            {.position = {0.5f, 0.5f, 0.5f}, .colour = {0.0f, 0.0f, 1.0f, 1.0f}},
            {.position = {-0.5f, 0.5f, 0.5f}, .colour = {1.0f, 1.0f, 1.0f, 1.0f}},
            // back
            {.position = {-0.5f, -0.5f, -0.5f}, .colour = {1.0f, 1.0f, 1.0f, 1.0f}},
            {.position = {-0.5f, 0.5f, -0.5f}, .colour = {1.0f, 1.0f, 1.0f, 1.0f}},
            {.position = {0.5f, 0.5f, -0.5f}, .colour = {1.0f, 1.0f, 1.0f, 1.0f}},
            {.position = {0.5f, -0.5f, -0.5f}, .colour = {1.0f, 1.0f, 1.0f, 1.0f}},
        },
        .indices{
            0, 1, 2, 2, 3, 0, // front
            4, 5, 6, 6, 7, 4, // back
            4, 0, 3, 3, 5, 4, // left
            1, 7, 6, 6, 2, 1, // right
            3, 2, 6, 6, 5, 3, // top
            4, 7, 1, 1, 0, 4  // bottom
        },
    };
}
