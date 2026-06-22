#include "panel_info.h"

#include <SDL_types.h>

SDL_bool SDL_webOSGetPanelResolution(int *width, int *height) __attribute__((weak));

SDL_bool SDL_webOSGetRefreshRate(int *rate) __attribute__((weak));

int commons_webos_get_panel_info(webos_panel_info_t *info) {
    if (SDL_webOSGetPanelResolution == NULL || SDL_webOSGetRefreshRate == NULL) {
        return -1;
    }
    if (!SDL_webOSGetPanelResolution(&info->width, &info->height)) {
        return -1;
    }
    if (!SDL_webOSGetRefreshRate(&info->rate)) {
        return -1;
    }
    return 0;
}