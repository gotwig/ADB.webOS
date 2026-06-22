#include "smp_resource.h"
#include "smp_player.h"

#include <SDL.h>

SDL_bool SDL_webOSGetPanelResolution(int *width, int *height) __attribute__((weak));

struct StarfishResource {
    char windowId[32];
    int maxRefreshRate;
};

StarfishResource *StarfishResourceCreate(const char *appId) {
    (void) appId;
    StarfishResource *res = calloc(1, sizeof(StarfishResource));
    assert(res != NULL);
    if (!SDL_webOSGetRefreshRate(&res->maxRefreshRate)) {
        res->maxRefreshRate = 60;
    }
    return res;
}

void StarfishResourceDestroy(StarfishResource *res) {
    if (res->windowId[0] != '\0') {
        SDL_webOSDestroyExportedWindow(res->windowId);
    }
    free(res);
}

bool StarfishResourcePopulateLoadPayload(StarfishResource *resource, jvalue_ref arg,
                                         const SS4S_AudioInfo *audioInfo, const SS4S_VideoInfo *videoInfo) {
    (void) audioInfo;
    if (videoInfo != NULL) {
        if (resource->windowId[0] == '\0') {
            const char *createdWnd = SDL_webOSCreateExportedWindow(0);
            if (createdWnd == NULL) {
                StarfishLibContext->Log(SS4S_LogLevelError, "SMP", "Didn't get a valid windowId: %s",
                                        resource->windowId);
                return false;
            }
            strncpy(resource->windowId, createdWnd, sizeof(resource->windowId) - 1);
        }
        jvalue_ref option = jobject_get(arg, J_CSTR_TO_BUF("option"));
        jobject_set(option, J_CSTR_TO_BUF("windowId"), j_cstr_to_jval(resource->windowId));
    }
    return true;
}

bool StarfishResourceSetMediaId(StarfishResource *resource, const char *connId) {
    (void) resource;
    (void) connId;
    return true;
}

bool StarfishResourceSetMediaAudioData(StarfishResource *resource, const char *data) {
    (void) resource;
    (void) data;
    return true;
}

bool StarfishResourceSetMediaVideoData(StarfishResource *resource, const char *data, bool hdr) {
    (void) resource;
    (void) data;
    (void) hdr;
    return true;
}

bool StarfishResourceLoadCompleted(StarfishResource *resource, const char *mediaId) {
    (void) resource;
    (void) mediaId;
    return true;
}

bool StarfishResourcePostLoad(StarfishResource *resource, const SS4S_VideoInfo *info) {
    if (resource->windowId[0] == '\0') {
        return false;
    }
    int w = 1920, h = 1080;
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
        w = dm.w;
        h = dm.h;
    }

    // Safely query physical panel resolution (e.g. 3840x2160 on 4K sets) to prevent sub-screen rendering caps
    int panel_w = 0, panel_h = 0;
    if (SDL_webOSGetPanelResolution != NULL && SDL_webOSGetPanelResolution(&panel_w, &panel_h)) {
        if (panel_w > 0 && panel_h > 0) {
            w = panel_w;
            h = panel_h;
        }
    }

    SDL_Rect src = {0, 0, info->width, info->height};
    SDL_Rect dst = {0, 0, w, h};

    if (info->width > 0 && info->height > 0) {
        double stream_ratio = (double)info->width / info->height;
        double screen_ratio = (double)w / h;

        if (info->width < info->height) {
            // Video is portrait -> strictly fit height, center horizontally with pillarboxes
            dst.h = h;
            dst.w = (int)(h * stream_ratio);
            dst.x = (w - dst.w) / 2;
            dst.y = 0;
        } else {
            // Video is landscape
            if (stream_ratio > screen_ratio) {
                // Phone is wider than TV (e.g. 21:9 or 2.44:1 stream mapped to 16:9 TV).
                // Zoom & crop the ultra-wide margins (black borders or phone notches) to fit the TV perfectly
                src.w = (int)(info->height * screen_ratio);
                src.h = info->height;
                src.x = (info->width - src.w) / 2;
                src.y = 0;

                dst.w = w;
                dst.h = h;
                dst.x = 0;
                dst.y = 0;
            } else {
                // Phone matches or is narrower than TV -> fit height completely
                dst.h = h;
                dst.w = (int)(h * stream_ratio);
                dst.x = (w - dst.w) / 2;
                dst.y = 0;
            }
        }
    }

    SDL_webOSSetExportedWindow(resource->windowId, &src, &dst);
    return true;
}

bool StarfishResourceStartPlaying(StarfishResource *resource) {
    (void) resource;
    return true;
}

bool StarfishResourcePostUnload(StarfishResource *resource) {
    (void) resource;
    return true;
}