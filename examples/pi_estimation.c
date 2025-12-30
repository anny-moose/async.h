#include <err.h>
#include <stdio.h>
#include <unistd.h>

#define ANNYMOOSE_ASYNC_IMPLEMENTATION
#include "async.h"

typedef struct args {
    unsigned seed;
    int steps;
} args_t;

void* function(void* params) {
    args_t* args = params;

    long offset = 0;

    for (int i = 0; i < args->steps; i++) {
        offset += (rand_r(&args->seed) % 2) ? 1 : -1;
    }

    free(args);

    offset = labs(offset);

    return (void*)offset;
}

#define STEPS 10000
#define RUNS 1500

int main(void) {
    srand(time(NULL));
    thread_queue_t thr_q;

    if (init_tq(&thr_q, 6)) err(EXIT_FAILURE, "init_queue");

    promise_t* promises[RUNS];
    int generated_promises = 0;
    for (int i = 0; i < RUNS; i++) {
        args_t* args = malloc(sizeof(args_t));
        if (!args) {
            warn("malloc failed, promise %d not generated", i);
            continue;
        } /* Passing arguments to a tasks works just like it does in pthreads */

        args->seed = rand();
        args->steps = STEPS;
        promises[generated_promises] = submit_task(&thr_q, function, args);
        if (!promises[generated_promises]) {
            warn("promise %d not generated", i);
            free(args);
            continue;
        }

        generated_promises++;
    }

    long accumulation = 0;
    for (int i = 0; i < generated_promises; i++) {
        accumulation += (long)await(promises[i]);
    }

    const double sum_avg = (double)accumulation / RUNS;
    const double pi_estimate = (2 * STEPS) / (sum_avg * sum_avg);
    printf("Estimated value of PI is: %f\n", pi_estimate);

    destroy_tq(&thr_q);

    return EXIT_SUCCESS;
}
