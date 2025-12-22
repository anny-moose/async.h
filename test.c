#include <stdio.h>
#include <unistd.h>

#define SYMBOL_APPEND
#define ANNYMOOSE_ASYNC_IMPLEMENTATION
#include "async.h"

void* thr_func(void* arg) {
    (void)(arg);
    printf("I am a thread, am working...\n");
    sleep(7);
    printf("I am a thread, am done.\n");

    return NULL;
}

int main(void) {
    thread_queue_t thr_q;

    init_queue(&thr_q, 6);

    printf("submitting...\n");
    promise_t* promise = submit_task(&thr_q, thr_func, NULL);

    printf("I am main, am working...\n");
    sleep(3);
    printf("I am main, am done. Off to await the promise!\n");

    sem_wait(&promise->done);

    printf("Done!\n");

    return 0;
}
