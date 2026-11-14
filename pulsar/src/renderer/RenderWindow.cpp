#include "RenderWindow.h"

#include "SDL3/SDL_error.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_stdinc.h"
#include "spdlog/spdlog.h"

pulsar::Renderer::RenderWindow::RenderWindow(const BatchedEnvironment& env, const ThreadBatch& batch, const std::size_t envIndex)
    : env(env), batch(batch), envIndex(envIndex)
{
    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow(
        "pulsar",
        1280,
        720,
        SDL_WINDOW_RESIZABLE
    );

    if (window == nullptr)
    {
        spdlog::error("Failed to create SDL window: {}", SDL_GetError());
        std::exit(1);
    }

    active = true;
}

void pulsar::Renderer::RenderWindow::render()
{
    (void)env;
    (void)batch;
    (void)envIndex;

    SDL_Event event{};

    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            exit();
        }
    }

    // todo
}

void pulsar::Renderer::RenderWindow::exit()
{
    active = false;

    SDL_DestroyWindow(window);
    SDL_Quit();
}

bool pulsar::Renderer::RenderWindow::getActive() const
{
    return active;
}