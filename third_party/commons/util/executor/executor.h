#pragma once

typedef struct executor_t executor_t;

typedef struct executor_task_t executor_task_t;

typedef int (*executor_action_cb)(void *arg);

typedef void (*executor_cleanup_cb)(void *arg, int result);

typedef enum executor_task_state_t {
    EXECUTOR_TASK_STATE_NOT_FOUND,
    EXECUTOR_TASK_STATE_PENDING,
    EXECUTOR_TASK_STATE_ACTIVE,
    EXECUTOR_TASK_STATE_CANCELLED,
} executor_task_state_t;

executor_t *executor_create(const char *name, int num_threads);

/**
 * Destroy resources and threads of this executor.
 * All threads created will be joined, and all the tasks will be finalized
 * @param executor Executor to destroy
 */
void executor_destroy(executor_t *executor);

/**
 * Create a new task and add to the queue to execute
 * @param executor Executor instance
 * @param action Task action. If a task gets cancelled, then the action won't be invoked
 * @param finalize Handle action result and free up resources. It will always be invoked even for cancelled task
 * @param arg Context to pass into action
 * @return Pointer of created task
 */
const executor_task_t *executor_submit(executor_t *executor, executor_action_cb action, executor_cleanup_cb finalize,
                                       void *arg);

/**
 * Cancel a task
 * @param executor Executor instance
 * @param task Task to cancel.
 */
int executor_cancel(executor_t *executor, const executor_task_t *task);

executor_task_state_t executor_task_state(const executor_t *executor, const executor_task_t *task);

int executor_active_tasks_count(const executor_t *executor);

int executor_is_destroyed(const executor_t *executor);

int executor_noop(void *arg);