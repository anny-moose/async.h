#include <stdio.h>
#include <unistd.h>

#define ANNYMOOSE_ASYNC_IMPLEMENTATION
#include "async.h"

void* task(void* arg) {
    sleep((long)arg);

    return NULL;
}

int main(void) {
    thread_queue_t thr_q;

    // init_queue(&thr_q, 1);
    init_tq(&thr_q, 6);

    printf("submitting...\n");

    promise_t* promises[4];
    long num = 5;

    for (int i = 0; i < 4; i++) {
        promises[i] = submit_task(&thr_q, task, (void*)num);
    }

    printf("I am main, am working...\n");

    sleep(num); /* work */

    printf("I am main, am done. Off to await the promise!\n");

    for (int i = 0; i < 4; i++) printf("Done! promise %d: %ld\n", i, (long)await(promises[i]));

    destroy_tq(&thr_q);

    return 0;
}
