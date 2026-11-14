#pragma once
#include "../env/BatchedEnvironment.h"
#include "SDL3/SDL_video.h"

namespace pulsar::Renderer
{
    class RenderWindow
    {
        SDL_Window* window;

        const BatchedEnvironment& env;
        const ThreadBatch& batch;
        std::size_t envIndex;

        void exit();

      public:
        explicit RenderWindow(const BatchedEnvironment& env, const ThreadBatch& batch, std::size_t envIndex);

        void render();
    };
}
