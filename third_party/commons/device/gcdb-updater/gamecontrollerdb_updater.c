#include "gamecontrollerdb_updater.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <curl/curl.h>

#include "executor.h"
#include "logging.h"

typedef struct write_context {
    commons_gcdb_updater_t *updater;
    CURL *curl;
    char *buf;
    size_t size;
    FILE *fp;
    int status;
} task_context;

static bool update_thread_start(commons_gcdb_updater_t *updater);

static int update_task(void *arg);

static void update_finalizer(void *arg, int result);

static void write_mapping_lines(task_context *ctx);

static void setup_headers(commons_gcdb_updater_t *updater, struct curl_slist **headers);

void commons_gcdb_updater_init(commons_gcdb_updater_t *updater, executor_t *executor) {
    assert(updater != NULL);
    assert(updater->platform != NULL);
    assert(updater->path != NULL);
    assert(updater->update_task == NULL);
    assert(executor != NULL);

    updater->executor = executor;
    updater->lock = SDL_CreateMutex();

    const char *platform = updater->platform_use != NULL ? updater->platform_use : updater->platform;
    // `platform:${platform},` => +10 chars
    size_t substr_len = strlen(platform) + 10;
    updater->platform_match_substr = malloc(substr_len + 1);
    updater->platform_match_substr_len = substr_len;
    snprintf(updater->platform_match_substr, substr_len + 1, "platform:%s,", platform);
}

void commons_gcdb_updater_deinit(commons_gcdb_updater_t *updater) {
    assert(updater != NULL);
    SDL_LockMutex(updater->lock);
    if (updater->update_task != NULL) {
        executor_cancel(updater->executor, updater->update_task);
        updater->update_task = NULL;
    }
    SDL_UnlockMutex(updater->lock);
    free(updater->platform_match_substr);
    SDL_DestroyMutex(updater->lock);
    memset(updater, 0, sizeof(*updater));
}

bool commons_gcdb_updater_update(commons_gcdb_updater_t *updater) {
    assert(updater != NULL);
    assert(updater->lock != NULL);
    bool started = false;
    SDL_LockMutex(updater->lock);
    executor_task_state_t state = executor_task_state(updater->executor, updater->update_task);
    if (state == EXECUTOR_TASK_STATE_NOT_FOUND || state == EXECUTOR_TASK_STATE_CANCELLED) {
        started = update_thread_start(updater);
    }
    SDL_UnlockMutex(updater->lock);
    return started;
}

static bool update_thread_start(commons_gcdb_updater_t *updater) {
    task_context *ctx = calloc(1, sizeof(task_context));
    ctx->updater = updater;
    ctx->curl = curl_easy_init();
    ctx->buf = malloc(1);
    const executor_task_t *task = executor_submit(updater->executor, update_task, update_finalizer, ctx);
    if (task == NULL) {
        curl_easy_cleanup(ctx->curl);
        free(ctx->buf);
        free(ctx);
        return false;
    }
    updater->update_task = task;
    return true;
}

static size_t body_cb(void *buffer, size_t size, size_t nitems, task_context *ctx) {
    if (ctx->status < 0) {
        return 0;
    }
    size_t realsize = size * nitems;
    void *allocated = realloc(ctx->buf, ctx->size + realsize + 1);
    assert(allocated != NULL);
    ctx->buf = allocated;

    memcpy(&(ctx->buf[ctx->size]), buffer, realsize);
    ctx->size += realsize;
    ctx->buf[ctx->size] = 0;
    write_mapping_lines(ctx);
    return realsize;
}

static void write_header_lines(task_context *ctx) {
    if (!ctx->fp) {
        return;
    }
    char *curLine = ctx->buf;
    while (curLine) {
        char *nextLine = memchr(curLine, '\n', ctx->size - (curLine - ctx->buf));
        if (nextLine) {
            *nextLine = '\0';
            if (nextLine > curLine) {
                nextLine[-1] = '\0';
            }
            char *headerValue = strstr(curLine, ": ");
            if (headerValue && strncasecmp(curLine, "etag", headerValue - curLine) == 0) {
                fprintf(ctx->fp, "# etag: %s\n", headerValue + 2);
            }
            curLine = nextLine + 1;
        } else {
            size_t rem = ctx->size - (curLine - ctx->buf);
            ctx->size = rem;
            memmove(ctx->buf, curLine, rem);
            break;
        }
    }
}

static size_t header_cb(char *buffer, size_t size, size_t nitems, task_context *ctx) {
    if (ctx->status == 0) {
        int status = 0;
        curl_easy_getinfo(ctx->curl, CURLINFO_RESPONSE_CODE, &status);
        if (status == 301 || status == 302) {
            return size * nitems;
        }
        ctx->status = status;
        if (status != 200) {
            return 0;
        }

        const char *condb = ctx->updater->path;
        ctx->fp = fopen(condb, "w");
        if (ctx->fp != NULL) {
            commons_log_debug("GameControllerDB", "Locking controller db file %s", condb);
#ifndef __WIN32
            if (lockf(fileno(ctx->fp), F_LOCK, 0) != 0) {
                ctx->status = -1;
            }
#endif
        } else {
            commons_log_error("GameControllerDB", "Failed to open file %s: %s", condb, strerror(errno));
            ctx->status = -1;
            return 0;
        }
    }
    assert(ctx->status == 200);
    /* received header is nitems * size long in 'buffer' NOT ZERO TERMINATED */
    /* 'userdata' is set with CURLOPT_HEADERDATA */
    size_t realsize = size * nitems;
    void *allocated = realloc(ctx->buf, ctx->size + realsize + 1);
    assert(allocated != NULL);
    ctx->buf = allocated;

    memcpy(&(ctx->buf[ctx->size]), buffer, realsize);
    ctx->size += realsize;
    ctx->buf[ctx->size] = 0;
    write_header_lines(ctx);
    return realsize;
}

static int update_task(void *arg) {
    task_context *ctx = arg;
    CURL *curl = ctx->curl;
    static char *const url = "https://github.com/gabomdq/SDL_GameControllerDB/raw/master/gamecontrollerdb.txt";
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, body_cb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
    struct curl_slist *headers = NULL;
    setup_headers(ctx->updater, &headers);
    if (headers != NULL) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, ctx);
    CURLcode code = curl_easy_perform(curl);
    commons_gcdb_status_t result;
    if (ctx->status == 200) {
        commons_log_debug("GameControllerDB", "Updated controller db");
        result = COMMONS_GCDB_UPDATER_UPDATED;
    } else if (ctx->status == 304) {
        commons_log_debug("GameControllerDB", "Controller db has no update");
        result = COMMONS_GCDB_UPDATER_NO_UPDATE;
    } else if (ctx->status >= 400) {
        commons_log_warn("GameControllerDB", "Failed to fetch %s: (HTTP %d)", url, ctx->status);
        result = COMMONS_GCDB_UPDATER_HTTP_ERROR;
    } else if (code != CURLE_OK) {
        commons_log_warn("GameControllerDB", "Failed to fetch %s: (%d, curl %d)", url, ctx->status, code);
        result = COMMONS_GCDB_UPDATER_CURL_ERROR;
    } else {
        result = COMMONS_GCDB_UPDATER_NO_UPDATE;
    }
    if (ctx->fp) {
        commons_log_debug("GameControllerDB", "Unlocking controller db file");
#ifndef __WIN32
        lockf(fileno(ctx->fp), F_ULOCK, 0);
#endif
        fclose(ctx->fp);
    }

    if (headers) {
        curl_slist_free_all(headers);
    }

    return (int) result;
}

static void update_finalizer(void *arg, int result) {
    task_context *ctx = arg;
    if (result != ECANCELED) {
        if (ctx->updater->callback != NULL) {
            ctx->updater->callback((commons_gcdb_status_t) result, ctx->updater->callback_ctx);
        }
    }
    free(ctx->buf);
    curl_easy_cleanup(ctx->curl);
    free(ctx);
}

void setup_headers(commons_gcdb_updater_t *updater, struct curl_slist **headers) {
    FILE *fp = fopen(updater->path, "r");
    if (fp == NULL) {
        return;
    }

    char linebuf[1024];
    while (fgets(linebuf, 1024, fp)) {
        if (linebuf[0] != '#') {
            break;
        }
        linebuf[strnlen(linebuf, 1024) - 1] = '\0';
        if (strncmp(linebuf, "# etag: ", 8) == 0) {
            char headbuf[2048];
            snprintf(headbuf, 2047, "if-none-match: %s", &linebuf[8]);
            commons_log_verbose("GameControllerDB", "Get update with ETag: %s", &linebuf[8]);
            *headers = curl_slist_append(*headers, headbuf);
        }
    }

    fclose(fp);
}

void write_mapping_lines(task_context *ctx) {
    if (ctx->fp == NULL) {
        return;
    }
    commons_gcdb_updater_t *updater = ctx->updater;
    char *curLine = ctx->buf;
    while (curLine) {
        char *nextLine = memchr(curLine, '\n', ctx->size - (curLine - ctx->buf));
        if (nextLine) {
            *nextLine = '\0';
            char *plat = strstr(curLine, updater->platform_match_substr);
            if (plat) {
                char *platEnd = plat + updater->platform_match_substr_len;
                if (platEnd < nextLine) {
                    memmove(plat, platEnd, nextLine - platEnd);
                    *(plat + (nextLine - platEnd)) = '\0';
                } else {
                    *plat = '\0';
                }
                fprintf(ctx->fp, "%splatform:%s,\n", curLine, updater->platform);
            }
            curLine = nextLine + 1;
        } else {
            size_t rem = ctx->size - (curLine - ctx->buf);
            ctx->size = rem;
            memmove(ctx->buf, curLine, rem);
            break;
        }
    }
}
