#include "assets.h"

#include <SDL3/SDL.h>

bool quak_get_asset_path(char *output, size_t output_size,
                         const char *relative_path)
{
    int written;

    if (!output || output_size == 0 || !relative_path || relative_path[0] == '\0')
        return false;

#if defined(__ANDROID__)
    written = SDL_snprintf(output, output_size, "%s", relative_path);
#else
    const char *base_path = SDL_GetBasePath();

    if (!base_path) {
        SDL_Log("SDL_GetBasePath failed while resolving '%s': %s",
                relative_path, SDL_GetError());
        return false;
    }

    written = SDL_snprintf(output, output_size, "%sassets/%s",
                           base_path, relative_path);
#endif

    if (written < 0 || (size_t)written >= output_size) {
        SDL_Log("Asset path is too long: '%s'", relative_path);
        return false;
    }

    return true;
}
