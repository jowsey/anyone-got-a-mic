#include "Path.h"

#include "SDL3/SDL_filesystem.h"

std::filesystem::path pulsar::Utils::Path::getEnginePath()
{
    return std::filesystem::path(SDL_GetBasePath()).parent_path().parent_path();
}