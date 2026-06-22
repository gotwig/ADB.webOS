#include "cec_sdl.h"

#include <stdio.h>

#include <stdlib.h>
#include <stdbool.h>

#include "ceccloader.h"

#include "cec_key.h"

static int cec_thread_worker(void *arg);

static inline bool cec_interrupted(cec_sdl_ctx_t *ctx);

static void reopen_first(cec_sdl_ctx_t *ctx);

static void cb_alert(void *cbparam, libcec_alert alert, libcec_parameter param);

static ICECCallbacks cec_callbacks = {
        .alert = cb_alert,
        .keyPress = cec_sdl_cb_key,
};

cec_sdl_ctx_t *cec_sdl_create(const char *name) {
    cec_sdl_ctx_t *ctx = calloc(1, sizeof(cec_sdl_ctx_t));
    cec_sdl_init(ctx, name);
    return ctx;
}

void cec_sdl_init(cec_sdl_ctx_t *ctx, const char *name) {
    snprintf(ctx->name, 15, "%s", name != NULL ? name : "CEC-SDL");
    ctx->lock = SDL_CreateMutex();
    SDL_LockMutex(ctx->lock);
    ctx->cond = SDL_CreateCond();
    ctx->cond_lock = SDL_CreateMutex();
    ctx->cec_iface = calloc(1, sizeof(libcec_interface_t));
    ctx->thread = SDL_CreateThread(cec_thread_worker, "cec-worker", ctx);
    SDL_UnlockMutex(ctx->lock);
}

void cec_sdl_deinit(cec_sdl_ctx_t *ctx) {
    SDL_LockMutex(ctx->lock);
    ctx->quit = true;
    SDL_CondSignal(ctx->cond);
    SDL_WaitThread(ctx->thread, NULL);
    libcecc_destroy(ctx->cec_iface);
    SDL_UnlockMutex(ctx->lock);

    SDL_DestroyMutex(ctx->cond_lock);
    SDL_DestroyCond(ctx->cond);
    SDL_DestroyMutex(ctx->lock);
    free(ctx->cec_iface);
}

void cec_sdl_destroy(cec_sdl_ctx_t *ctx) {
    cec_sdl_deinit(ctx);
    free(ctx);
}

static int cec_thread_worker(void *arg) {
    cec_sdl_ctx_t *ctx = arg;
    libcec_configuration cec_conf;
    libcecc_reset_configuration(&cec_conf);
    cec_conf.clientVersion = LIBCEC_VERSION_CURRENT;
    cec_conf.bActivateSource = 0;
    cec_conf.callbacks = &cec_callbacks;
    cec_conf.callbackParam = ctx;
    strncpy(cec_conf.strDeviceName, ctx->name, 15);
    cec_conf.deviceTypes.types[0] = CEC_DEVICE_TYPE_PLAYBACK_DEVICE;

    if (libcecc_initialise(&cec_conf, ctx->cec_iface, NULL) != 1) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "failed to initialize libcecc: %s", dlerror());
        goto cleanup;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "libcecc initialized");
    reopen_first(ctx);
    SDL_LockMutex(ctx->cond_lock);
    while (!cec_interrupted(ctx)) {
        SDL_CondWait(ctx->cond, ctx->cond_lock);
    }
    SDL_UnlockMutex(ctx->cond_lock);

    cleanup:
    return 0;
}

static inline bool cec_interrupted(cec_sdl_ctx_t *ctx) {
    return ctx->quit;
}

static void cb_alert(void *cbparam, const libcec_alert alert, const libcec_parameter param) {
    (void) cbparam;
    (void) alert;
    (void) param;
}

static void reopen_first(cec_sdl_ctx_t *ctx) {
    cec_adapter devices[8];
    libcec_interface_t *iface = ctx->cec_iface;
    uint8_t devices_found = iface->find_adapters(iface->connection, devices, 8, NULL);
    if (devices_found == 0) return;
    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "Opened CEC device %s", devices[0].path);
    iface->open(iface->connection, devices[0].comm, 5000);
}