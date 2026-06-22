#include "executor.h"

#include "logging.h"

#include <stdlib.h>
#include <errno.h>
#include <SDL2/SDL.h>

typedef struct executor_task_t {
    executor_action_cb action;
    executor_cleanup_cb finalize;
    void *arg;
    SDL_mutex *lock;
    SDL_bool cancelled;
    struct executor_task_t *prev;
    struct executor_task_t *next;
} executor_task_t;

typedef struct executor_queue_t {
    executor_t *executor;
    executor_task_t *head;
    SDL_mutex *lock;
    SDL_cond *cond;
} executor_queue_t;

struct executor_t {
    SDL_mutex *lock;
    SDL_Thread **threads;
    size_t num_threads;
    executor_queue_t pending;
    executor_queue_t active;
    SDL_bool destroyed;
};

#define LINKEDLIST_IMPL
#define LINKEDLIST_TYPE executor_task_t
#define LINKEDLIST_PREFIX tasks
#define LINKEDLIST_DOUBLE 1

#include "linked_list.h"

#undef LINKEDLIST_TYPE
#undef LINKEDLIST_PREFIX
#undef LINKEDLIST_DOUBLE

static int thread_worker(void *context);

static void queue_init(executor_queue_t *queue, executor_t *executor);

static void queue_destroy(executor_queue_t *queue);

static executor_task_t *queue_poll(executor_queue_t *queue);

static void queue_push(executor_queue_t *queue, executor_task_t *task);

static void queue_remove(executor_queue_t *queue, executor_task_t *task);

static void queue_clear(executor_queue_t *queue);

static void queue_unlock(executor_queue_t *queue);

static int queue_is_task_cancelled(const executor_queue_t *queue, const executor_task_t *task);

static int queue_cancel_task(executor_queue_t *queue, const executor_task_t *task);

static void queue_destroy_task(executor_task_t *task);

static int task_comparator(executor_task_t *p, const void *fv);

static executor_task_t *task_create(executor_action_cb action, executor_cleanup_cb finalize, void *arg);

static void task_cancel(executor_task_t *task);

static int task_is_cancelled(executor_task_t *task);

static void task_finalize(executor_task_t *task, int result);

static void task_destroy(executor_task_t *task);

executor_t *executor_create(const char *name, int num_threads) {
    executor_t *executor = calloc(1, sizeof(executor_t));
    executor->lock = SDL_CreateMutex();
    executor->destroyed = SDL_FALSE;
    queue_init(&executor->pending, executor);
    queue_init(&executor->active, executor);
    if (num_threads > 0) {
        executor->num_threads = num_threads;
    } else {
        executor->num_threads = SDL_GetCPUCount();
    }
    executor->threads = calloc(executor->num_threads, sizeof(SDL_Thread *));
    for (size_t i = 0, j = executor->num_threads; i < j; i++) {
        executor->threads[i] = SDL_CreateThread(thread_worker, name, executor);
    }
    return executor;
}

void executor_destroy(executor_t *executor) {
    SDL_assert_release(!executor->destroyed);
    SDL_LockMutex(executor->lock);
    executor->destroyed = SDL_TRUE;
    SDL_UnlockMutex(executor->lock);
    queue_unlock(&executor->active);
    queue_unlock(&executor->pending);
    for (size_t i = 0, j = executor->num_threads; i < j; i++) {
        SDL_WaitThread(executor->threads[i], NULL);
    }
    free(executor->threads);
    queue_destroy(&executor->active);
    queue_destroy(&executor->pending);
    SDL_DestroyMutex(executor->lock);
    free(executor);
}

const executor_task_t *executor_submit(executor_t *executor, executor_action_cb action, executor_cleanup_cb finalize,
                                       void *arg) {
    SDL_assert(executor != NULL);
    SDL_assert(action != NULL);
    SDL_LockMutex(executor->lock);
    if (executor->destroyed) {
        SDL_UnlockMutex(executor->lock);
        return NULL;
    }
    SDL_UnlockMutex(executor->lock);
    executor_task_t *task = task_create(action, finalize, arg);
    queue_push(&executor->pending, task);
    return task;
}


int executor_cancel(executor_t *executor, const executor_task_t *task) {
    if (executor_is_destroyed(executor) != 0 || task == NULL) {
        return -1;
    }
    return queue_cancel_task(&executor->active, task) || queue_cancel_task(&executor->pending, task);
}

executor_task_state_t executor_task_state(const executor_t *executor, const executor_task_t *task) {
    if (executor_is_destroyed(executor) != 0) {
        return EXECUTOR_TASK_STATE_NOT_FOUND;
    }

    int cancelled = queue_is_task_cancelled(&executor->active, task);
    if (cancelled != -1) {
        return cancelled ? EXECUTOR_TASK_STATE_CANCELLED : EXECUTOR_TASK_STATE_ACTIVE;
    }
    cancelled = queue_is_task_cancelled(&executor->pending, task);
    if (cancelled == -1) {
        return EXECUTOR_TASK_STATE_NOT_FOUND;
    }
    return cancelled ? EXECUTOR_TASK_STATE_CANCELLED : EXECUTOR_TASK_STATE_PENDING;
}

int executor_active_tasks_count(const executor_t *executor) {
    if (executor_is_destroyed(executor) != 0) {
        return 0;
    }
    SDL_LockMutex(executor->active.lock);
    int len = tasks_len(executor->active.head);
    SDL_UnlockMutex(executor->active.lock);
    return len;
}

int executor_is_destroyed(const executor_t *executor) {
    if (SDL_LockMutex(executor->lock) != 0) {
        return -1;
    }
    int destroyed = executor->destroyed;
    SDL_UnlockMutex(executor->lock);
    return destroyed;
}

int executor_noop(void *arg) {
    (void) arg;
    return 0;
}

static int thread_worker(void *context) {
    executor_t *executor = context;
    for (executor_task_t *task = NULL; (task = queue_poll(&executor->pending)) != NULL;) {
        if (executor_is_destroyed(executor)) {
            task_finalize(task, ECANCELED);
            task_destroy(task);
            break;
        }
        queue_push(&executor->active, task);
        int result = task->cancelled == 0 ? task->action(task->arg) : ECANCELED;
        if (executor_is_destroyed(executor)) {
            result = ECANCELED;
        }
        task_finalize(task, result);
        queue_remove(&executor->active, task);
        task_destroy(task);
    }
    queue_clear(&executor->pending);
    return 0;
}

void queue_init(executor_queue_t *queue, executor_t *executor) {
    queue->executor = executor;
    queue->head = NULL;
    queue->lock = SDL_CreateMutex();
    queue->cond = SDL_CreateCond();
}

void queue_destroy(executor_queue_t *queue) {
    SDL_DestroyCond(queue->cond);
    SDL_DestroyMutex(queue->lock);
}

static executor_task_t *queue_poll(executor_queue_t *queue) {
    if (SDL_LockMutex(queue->lock) != 0) {
        return NULL;
    }
    while (queue->head == NULL) {
        if (executor_is_destroyed(queue->executor)) {
            SDL_UnlockMutex(queue->lock);
            return NULL;
        }
        SDL_CondWait(queue->cond, queue->lock);
    }
    executor_task_t *task = queue->head;
    SDL_assert(task != NULL);
    queue->head = task->next;
    if (queue->head != NULL) {
        queue->head->prev = NULL;
    }
    SDL_UnlockMutex(queue->lock);
    task->prev = NULL;
    task->next = NULL;
    return task;
}

static void queue_push(executor_queue_t *queue, executor_task_t *task) {
    SDL_LockMutex(queue->lock);
    queue->head = tasks_append(queue->head, task);
    SDL_UnlockMutex(queue->lock);
    SDL_CondSignal(queue->cond);
}

static void queue_remove(executor_queue_t *queue, executor_task_t *task) {
    SDL_LockMutex(queue->lock);
    queue->head = tasks_remove(queue->head, task);
    SDL_UnlockMutex(queue->lock);
    SDL_CondSignal(queue->cond);
}

static void queue_clear(executor_queue_t *queue) {
    SDL_LockMutex(queue->lock);
    tasks_free(queue->head, queue_destroy_task);
    queue->head = NULL;
    SDL_UnlockMutex(queue->lock);
}

static void queue_unlock(executor_queue_t *queue) {
    SDL_CondBroadcast(queue->cond);
}

static int queue_is_task_cancelled(const executor_queue_t *queue, const executor_task_t *task) {
    int result = -1;
    if (task == NULL) {
        return result;
    }
    SDL_LockMutex(queue->lock);
    executor_task_t *match = tasks_find_by(queue->head, task, task_comparator);
    if (match != NULL) {
        result = task_is_cancelled(match);
    }
    SDL_UnlockMutex(queue->lock);
    return result;
}

static int queue_cancel_task(executor_queue_t *queue, const executor_task_t *task) {
    SDL_LockMutex(queue->lock);
    executor_task_t *match = tasks_find_by(queue->head, task, task_comparator);
    if (match == NULL) {
        SDL_UnlockMutex(queue->lock);
        return 0;
    }
    task_cancel(match);
    SDL_UnlockMutex(queue->lock);
    return 1;
}

static void queue_destroy_task(executor_task_t *task) {
    task_finalize(task, ECANCELED);
    task_destroy(task);
}

static int task_comparator(executor_task_t *p, const void *fv) {
    return (int) ((void *) p - fv);
}

executor_task_t *task_create(executor_action_cb action, executor_cleanup_cb finalize, void *arg) {
    executor_task_t *task = calloc(1, sizeof(executor_task_t));
    task->action = action;
    task->finalize = finalize;
    task->arg = arg;
    task->lock = SDL_CreateMutex();
    return task;
}

static void task_finalize(executor_task_t *task, int result) {
    if (task->finalize) {
        task->finalize(task->arg, result);
    }
}

static void task_destroy(executor_task_t *task) {
    SDL_DestroyMutex(task->lock);
    free(task);
}

static void task_cancel(executor_task_t *task) {
    SDL_LockMutex(task->lock);
    task->cancelled = SDL_TRUE;
    SDL_UnlockMutex(task->lock);
}

static int task_is_cancelled(executor_task_t *task) {
    int cancelled;
    SDL_LockMutex(task->lock);
    cancelled = task->cancelled;
    SDL_UnlockMutex(task->lock);
    return cancelled;
}