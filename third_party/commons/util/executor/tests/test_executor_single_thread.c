#include "executor.h"
#include "logging.h"

#include <stdlib.h>
#include <assert.h>

#if __unix__

#include <unistd.h>

#else
#include <windows.h>
#define sleep(sec) Sleep((sec) * 1000)
#endif

typedef struct {
    int *array;
    int index;
} task_context_t;

static int task(void *arg) {
    task_context_t *ctx = arg;
    commons_log_info("test", "wait 3s before assigning value to array[%d]\n", ctx->index);
    sleep(3);
    return 0;
}

static void finalizer(void *arg, int result) {
    task_context_t *ctx = arg;
    if (result == 0) {
        commons_log_info("test", "assigning value to array[%d]\n", ctx->index);
        ctx->array[ctx->index] = 114514;
    }
    free(ctx);
}

int main() {
    int n_tasks = 810;
    executor_t *executor = executor_create("executor", 1);
    int *array = calloc(n_tasks, sizeof(int));
    for (int i = 0; i < n_tasks; i++) {
        task_context_t *ctx = calloc(1, sizeof(task_context_t));
        ctx->array = array;
        ctx->index = i;
        executor_submit(executor, task, finalizer, ctx);
    }
    sleep(4);
    // e & f should be 0
    commons_log_info("test", "destroying executor");
    executor_destroy(executor);
    for (int i = 0; i < n_tasks; i++) {
        if (i < 1) {
            assert(array[i] == 114514);
        } else {
            assert(array[i] == 0);
        }
    }
    free(array);
    return 0;
}