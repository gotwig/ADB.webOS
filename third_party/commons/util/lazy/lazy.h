#pragma once

#include <SDL_mutex.h>
#include <stdbool.h>

typedef void *(*lazy_supplier)(void *userdata);

typedef struct lazy_t {
    SDL_mutex *mutex;
    lazy_supplier supplier;
    void *userdata;

    void *value;
    bool value_supplied;
} lazy_t;


static inline void lazy_init(lazy_t *lazy, lazy_supplier supplier, void *userdata) {
    lazy->mutex = SDL_CreateMutex();
    SDL_LockMutex(lazy->mutex);
    lazy->supplier = supplier;
    lazy->userdata = userdata;
    lazy->value_supplied = false;
    SDL_UnlockMutex(lazy->mutex);
}

static inline void *lazy_obtain(lazy_t *lazy) {
    void *value = NULL;
    SDL_LockMutex(lazy->mutex);
    if (lazy->value_supplied) {
        value = lazy->value;
    } else {
        lazy->value_supplied = true;
        value = lazy->value = lazy->supplier(lazy->userdata);
    }
    SDL_UnlockMutex(lazy->mutex);
    return value;
}

/* lazy_deinit must NOT be called concurrently with lazy_obtain or
 * any other lazy_t operation. SDL_DestroyMutex (and pthreads) forbid
 * destroying a held mutex, so no amount of locking inside this
 * function can close the window between releasing the lock and
 * destroying it. The caller is responsible for quiescing all users
 * of this lazy_t before tearing it down. */
static inline void *lazy_deinit(lazy_t *lazy) {
    void *value = lazy->value_supplied ? lazy->value : NULL;
    SDL_DestroyMutex(lazy->mutex);
    memset(lazy, 0, sizeof(lazy_t));
    return value;
}
