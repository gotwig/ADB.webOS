#pragma once


#include <stddef.h>
#include <stdbool.h>

#include <SDL_thread.h>

typedef struct executor_t executor_t;
typedef struct executor_task_t executor_task_t;

typedef enum commons_gcdb_status_t {
    COMMONS_GCDB_UPDATER_UPDATED,
    COMMONS_GCDB_UPDATER_NO_UPDATE,
    COMMONS_GCDB_UPDATER_HTTP_ERROR,
    COMMONS_GCDB_UPDATER_CURL_ERROR,
} commons_gcdb_status_t;

typedef void (commons_gcdb_callback)(commons_gcdb_status_t result, void *context);

typedef struct commons_gcdb_updater_t {
    const char *path;
    const char *platform;
    const char *platform_use;

    commons_gcdb_callback *callback;
    void *callback_ctx;

    SDL_mutex *lock;

    char *platform_match_substr;
    size_t platform_match_substr_len;

    executor_t *executor;
    const executor_task_t *update_task;
} commons_gcdb_updater_t;

void commons_gcdb_updater_init(commons_gcdb_updater_t *updater, executor_t *executor);

void commons_gcdb_updater_deinit(commons_gcdb_updater_t *updater);

bool commons_gcdb_updater_update(commons_gcdb_updater_t *updater);
