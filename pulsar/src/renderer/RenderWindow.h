#pragma once
#include "../env/BatchedEnvironment.h"
#include "SDL3/SDL_video.h"

namespace pulsar::Renderer
{
    class RenderWindow
    {
        SDL_Window* window;
        bool active;

        const BatchedEnvironment& env;
        const ThreadBatch& batch;
        std::size_t envIndex;

      public:
        explicit RenderWindow(const BatchedEnvironment& env, const ThreadBatch& batch, std::size_t envIndex);

        void render();
        void exit();

        [[nodiscard]] bool getActive() const;
    };
}
