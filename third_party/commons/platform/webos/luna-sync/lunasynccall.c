#include "lunasynccall.h"

#include <libhelpers.h>
#include <pthread.h>
#include <string.h>

struct HContextSync {
    union {
        HContext ctx;
        __attribute__((unused)) unsigned char placeholder[128];
    } base;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool finished;
    char **output;
};

static bool callback(LSHandle *sh, LSMessage *reply, void *ctx);

bool HLunaServiceCallSync(const char *uri, const char *payload, bool public, char **output) {
    struct HContextSync context = {.base.ctx = {
            .multiple = 0,
            .pub = public ? 1 : 0,
            .callback = callback,
    }};

    pthread_mutex_init(&context.mutex, NULL);
    pthread_cond_init(&context.cond, NULL);
    context.output = output;

    if (HLunaServiceCall(uri, payload, &context.base.ctx) != 0) {
        pthread_mutex_destroy(&context.mutex);
        pthread_cond_destroy(&context.cond);
        return false;
    }
    pthread_mutex_lock(&context.mutex);
    while (!context.finished) {
        pthread_cond_wait(&context.cond, &context.mutex);
    }
    pthread_mutex_unlock(&context.mutex);

    pthread_mutex_destroy(&context.mutex);
    pthread_cond_destroy(&context.cond);
    return true;
}

static bool callback(LSHandle *sh, LSMessage *reply, void *ctx) {
    (void) sh;
    struct HContextSync *context = (struct HContextSync *) ctx;
    pthread_mutex_lock(&context->mutex);
    context->finished = true;
    if (context->output) {
        *context->output = strdup(HLunaServiceMessage(reply));
    }
    pthread_cond_signal(&context->cond);
    pthread_mutex_unlock(&context->mutex);
    return true;
}