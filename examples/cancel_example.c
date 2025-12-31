#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define ANNYMOOSE_ASYNC_IMPLEMENTATION
#include "async.h"

void* other_func(void* params) {
    sleep((long)params);
    return params;
}

void* work_func(void* params) {
    sleep((long)params);
    return params;
}

int cmp_func(const void* task_param) {
    const task_t* task = task_param;

    if (task->callback == work_func) return 1;
    return 0;
}

#define TIME 5
#define PROMISES 60

int main(void) {
    thread_queue_t tq;

    init_tq(&tq, 6);

    printf("Promises ");

    promise_t* promises[PROMISES];
    int generated_promises = 0;
    for (int i = 0; i < PROMISES; i++) {
        promises[generated_promises] = submit_task(&tq, (i % 3) ? work_func : other_func, (void*)(long)TIME);
        if (!promises[generated_promises]) {
            warn("promise %d not generated", i);
            continue;
        }

        if (!(i % 3)) printf("%d ", i);

        generated_promises++;
    }

    printf("are special.\n");

    remove_tasks(&tq, cmp_func);

    for (int i = 0; i < generated_promises / 3; i++) {
        printf("Promise %d was ", i);
        if (!await(promises[i])) /* awaiting a canceled promise returns NULL and sets errno to ECANCELED. Since our work function doesn't
                                    return NULL, a simple null check is enough */
            printf("canceled.\n");
        else
            printf("completed succesfully.\n");
    }

    destroy_tq(&tq); /* destroying a thread queue cancels all pending promises */

    for (int i = generated_promises / 3; i < generated_promises; i++) {
        printf("Promise %d was ", i);
        if (!await(promises[i]))
            printf("canceled.\n");
        else
            printf("completed succesfully.\n");
    }

    /*
     * Notice how the first 4 regular promises always succeed, even though this type of task gets cancelled before they finish. That is
     * because promises can not be cancelled mid-execution.
     */

    return EXIT_SUCCESS;
}
