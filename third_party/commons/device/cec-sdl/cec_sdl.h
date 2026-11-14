#pragma once

#include <stdbool.h>

#include <SDL_mutex.h>
#include <SDL_thread.h>
#include <SDL_log.h>

typedef struct cec_sdl_ctx_t {
    char name[15];
    SDL_mutex *lock;
    SDL_cond *cond;
    SDL_mutex *cond_lock;
    SDL_Thread *thread;
    void *cec_iface;

    bool quit;
    bool enable_unfocused;
} cec_sdl_ctx_t;

cec_sdl_ctx_t *cec_sdl_create(const char *name);

void cec_sdl_init(cec_sdl_ctx_t *ctx, const char *name);

void cec_sdl_deinit(cec_sdl_ctx_t *ctx);

void cec_sdl_destroy(cec_sdl_ctx_t *ctx);