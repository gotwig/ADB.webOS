/* Multithreaded regression test for refcounter_unref.
 *
 * The buggy version of refcounter_unref dropped the lock before
 * reading counter->counter == 0, so two threads doing the final
 * decrements could both see counter == 0 and both believe they
 * were holding the last reference. In a real caller this leads
 * to a double free.
 *
 * Strategy: set the refcount to N, spawn N threads that each call
 * refcounter_unref() exactly once, and assert that exactly one of
 * them gets back "true". A spinning atomic flag is used to release
 * the threads as close to simultaneously as possible — semaphores
 * tend to serialize wakeups.
 *
 * The race window is small, so the test runs many rounds with a
 * high thread count to maximize the chance of catching a
 * regression on contended hardware.
 */

#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "refcounter.h"

#define THREADS_PER_ROUND 16
#define ROUNDS 20000

typedef struct {
    refcounter_t *counter;
    SDL_atomic_t *trues;
    SDL_atomic_t *go;
} thread_ctx_t;

static int unref_once(void *data) {
    thread_ctx_t *ctx = data;
    while (!SDL_AtomicGet(ctx->go)) { /* spin until released */ }
    if (refcounter_unref(ctx->counter)) {
        SDL_AtomicAdd(ctx->trues, 1);
    }
    return 0;
}

int main(void) {
    if (SDL_Init(0) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    for (int round = 0; round < ROUNDS; round++) {
        refcounter_t counter;
        refcounter_init(&counter);
        for (int i = 1; i < THREADS_PER_ROUND; i++) {
            refcounter_ref(&counter);
        }
        /* counter is now THREADS_PER_ROUND */

        SDL_atomic_t trues, go;
        SDL_AtomicSet(&trues, 0);
        SDL_AtomicSet(&go, 0);
        thread_ctx_t ctx = {.counter = &counter, .trues = &trues, .go = &go};

        SDL_Thread *threads[THREADS_PER_ROUND];
        for (int i = 0; i < THREADS_PER_ROUND; i++) {
            threads[i] = SDL_CreateThread(unref_once, "unref", &ctx);
            assert(threads[i] != NULL);
        }
        SDL_AtomicSet(&go, 1);
        for (int i = 0; i < THREADS_PER_ROUND; i++) {
            SDL_WaitThread(threads[i], NULL);
        }

        int observed = SDL_AtomicGet(&trues);
        if (observed != 1) {
            fprintf(stderr,
                    "round %d: expected exactly one last-reference signal, got %d\n",
                    round, observed);
            refcounter_destroy(&counter);
            return 1;
        }

        refcounter_destroy(&counter);
    }

    SDL_Quit();
    return 0;
}
