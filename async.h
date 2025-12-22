/*
 *
 * If you are reading this beast i highly advise you to expand all preprocessor macros beforehand, as in this file they don't help
 * readibility in the slightest
 *
 */
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>

#include "annylib.h"

/* define SYMBOL_APPEND before including to resolve name clashing */

#define GLUE(pref, sym) pref##sym
#define JOIN(pref, sym) GLUE(pref, sym)
#define DECL(sym) JOIN(SYMBOL_APPEND, sym)
/* why does C require three macros for this? fuck if i know. */

typedef struct thread_queue {
    pthread_t* threads;
    pthread_mutex_t queue_mtx;
    LList_t queue;
    pthread_cond_t cv;
} DECL(thread_queue_t);

typedef struct promise {
    void* data;
    sem_t done; /* should be waited before accessing data */
} DECL(promise_t);

typedef struct task {
    void* args;
    void* (*callback)(void*);
    DECL(promise_t) * promise;
} DECL(task_t);

static inline void* DECL(worker_func)(void* arg) {
    DECL(thread_queue_t)* state = (DECL(thread_queue_t)*)arg;

    pthread_mutex_t mtx;
    pthread_mutex_init(&mtx, NULL);
    pthread_mutex_lock(&mtx);

    goto work; /* skip waiting for condition just in case we already have work */
wait:
    printf("waiting for cond!\n");
    if (pthread_cond_wait(&state->cv, &mtx) != 0) err(EXIT_FAILURE, "failed waiting on CV");
work: /* this is supposed to be a microoptimization but it probably does
          fuckall */
    printf("locking queue mutex!\n");
    pthread_mutex_lock(&state->queue_mtx);
    DECL(task_t)* task = (DECL(task_t)*)LList_pop_head(&state->queue);
    pthread_mutex_unlock(&state->queue_mtx);
    if (!task) goto wait;

    printf("doing work!\n");
    task->promise->data = task->callback(task->args);
    sem_post(&task->promise->done);

    free(task->args);
    free(task);

    goto work;
}

int DECL(init_queue)(DECL(thread_queue_t) * queue, int num_threads);

int DECL(init_promise)(DECL(promise_t) * promise);

DECL(promise_t) * DECL(submit_task)(DECL(thread_queue_t) * ctx, void* (*callback)(void*), void* arg);

#ifdef ANNYMOOSE_ASYNC_IMPLEMENTATION
/* todo: handle and check errors for all funcs */

int DECL(init_queue)(DECL(thread_queue_t) * queue, int num_threads) {
    pthread_mutex_init(&queue->queue_mtx, NULL);
    pthread_cond_init(&queue->cv, NULL);
    LList_init(&queue->queue, sizeof(DECL(task_t)), NULL);

    queue->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&queue->threads[i], NULL, DECL(worker_func), queue);
    }

    return 0;
}

int DECL(init_promise)(DECL(promise_t) * promise) {
    promise->data = NULL;
    sem_init(&promise->done, 0, 0);

    return 0;
}

DECL(promise_t) * DECL(submit_task)(DECL(thread_queue_t) * ctx, void* (*callback)(void*), void* arg) {
    DECL(promise_t)* promise = (DECL(promise_t)*)malloc(sizeof(DECL(promise_t)));
    DECL(init_promise)(promise);

    DECL(task_t)
    task = {
        .args = arg,
        .callback = callback,
        .promise = promise,
    };

    pthread_mutex_lock(&ctx->queue_mtx);
    LList_append(&ctx->queue, &task);
    pthread_mutex_unlock(&ctx->queue_mtx);

    printf("sending off signal!\n");
    pthread_cond_signal(&ctx->cv);

    return promise;
}

#endif /* implementation */
