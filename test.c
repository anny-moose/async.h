#include <stdio.h>
#include <unistd.h>

#define SYMBOL_APPEND
#define ANNYMOOSE_ASYNC_IMPLEMENTATION
#include "async.h"

void* task(void* arg) {
    (void)arg;

    printf("i ain't doing anything xd\n");

    return NULL;
}

int main(void) {
    thread_queue_t thr_q;

    init_queue(&thr_q, 6);

    printf("submitting...\n");

    promise_t* promises[4];
    long num = 20;

    for (int i = 0; i < 4; i++) {
        num += 5;
        promises[i] = submit_task(&thr_q, task, (void*)(long)num);
    }

    printf("I am main, am done. Off to await the promise!\n");

    for (int i = 0; i < 4; i++) printf("Done! promise %d: %ld\n", i, (long)await(promises[i]));

    destroy_queue(&thr_q);

    printf("kthxbye!\n");

    return 0;
}
