/*
 *
 * If you are reading this beast i highly advise you to expand all preprocessor
 * macros beforehand, as in this file they don't help readibility in the
 * slightest
 *
 */
#ifndef ANNYMOOSE_ASYNC_H
#define ANNYMOOSE_ASYNC_H
#include <pthread.h>
#include <semaphore.h>

#include "queue.h"

/* define SYMBOL_APPEND before including to resolve name clashing */

#ifndef SYMBOL_APPEND
#define SYMBOL_APPEND
#endif

#define GLUE(pref, sym) pref##sym
#define JOIN(pref, sym) GLUE(pref, sym)
#define DECL(sym) JOIN(SYMBOL_APPEND, sym)
/* why does C require three macros for this? hell if i know. */

typedef struct thread_queue {
    pthread_t* threads;
    pthread_mutex_t queue_mtx;
    sem_t thread_available; /* this is really ugly and there's probably a better way to do this */
    Queue_t queue;
    pthread_cond_t cv;
    int num_threads;
} DECL(thread_queue_t);

typedef struct promise {
    void* data;
    int err_code;
    sem_t done; /* should be waited before accessing data */
} DECL(promise_t);

typedef struct task {
    void* args;
    void* (*callback)(void*);
    DECL(promise_t) * promise;
} DECL(task_t);

int DECL(init_queue)(DECL(thread_queue_t) * ctx, int num_threads);
int DECL(destroy_queue)(DECL(thread_queue_t) * ctx);

void* DECL(__worker_func)(void* arg);
int DECL(init_promise)(DECL(promise_t) * promise);

DECL(promise_t) * DECL(submit_task)(DECL(thread_queue_t) * ctx, void* (*callback)(void*), void* arg);
inline void* DECL(await)(DECL(promise_t) * promise);

/*
 *
 */

#endif /* ANNYMOOSE_ASYNC_H */

#define ANNYMOOSE_ASYNC_IMPLEMENTATION
#ifdef ANNYMOOSE_ASYNC_IMPLEMENTATION
#include <err.h>
#include <errno.h>
#include <stdio.h>
/* todo: handle and check errors for all funcs */

void* DECL(__worker_func)(void* arg) {
    DECL(thread_queue_t)* state = (DECL(thread_queue_t)*)arg;

    fprintf(stderr, "Thread started!!\n");
    pthread_mutex_lock(&state->queue_mtx);

    sem_post(&state->thread_available);
    while (1) {
        if (pthread_cond_wait(&state->cv, &state->queue_mtx) != 0) err(EXIT_FAILURE, "failed waiting on CV");

        while (state->queue.length > 0) {
            DECL(task_t)* task = (DECL(task_t)*)Queue_pop_head(&state->queue);
            pthread_mutex_unlock(&state->queue_mtx);
            if (!task) break;

            printf("doing work!\n");
            task->promise->data = task->callback(task->args);
            task->promise->err_code = errno;
            sem_post(&task->promise->done);

            free(task);
            pthread_mutex_lock(&state->queue_mtx);
        }

        if (state->num_threads == -1) {
            pthread_mutex_unlock(&state->queue_mtx);
            break;
        }
    }

    fprintf(stderr, "Thread exiting!!\n");

    return NULL;
}

/* todo: figure out something to support errno while allowing awaits normally */
inline void* DECL(await)(DECL(promise_t) * promise) {
    sem_wait(&promise->done);
    void* ret_val = promise->data;
    free(promise);

    return ret_val;
}

int DECL(init_queue)(DECL(thread_queue_t) * ctx, int num_threads) {
    pthread_mutex_init(&ctx->queue_mtx, NULL);
    pthread_cond_init(&ctx->cv, NULL);
    sem_init(&ctx->thread_available, 0, 0);
    ctx->num_threads = num_threads;
    Queue_init(&ctx->queue, sizeof(DECL(task_t)), NULL);

    ctx->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&ctx->threads[i], NULL, DECL(__worker_func), ctx);
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
    Queue_append(&ctx->queue, &task);
    pthread_mutex_unlock(&ctx->queue_mtx);

    for (int i = 0; i < ctx->num_threads; i++)
        sem_wait(&ctx->thread_available); /* hack: this sucks (surely if all threads have posted the semaphore there is at LEAST one that's
                                             either working or waiting on the cv.) */
    pthread_cond_signal(&ctx->cv);
    for (int i = 0; i < ctx->num_threads; i++) sem_post(&ctx->thread_available);

    return promise;
}

int DECL(destroy_queue)(DECL(thread_queue_t) * ctx) {
    pthread_mutex_lock(&ctx->queue_mtx);
    Queue_clear(&ctx->queue);
    int nthreads = ctx->num_threads;
    ctx->num_threads = -1;
    pthread_mutex_unlock(&ctx->queue_mtx);

    pthread_cond_broadcast(&ctx->cv);

    for (int i = 0; i < nthreads; i++) {
        pthread_join(ctx->threads[i], NULL);
    }

    free(ctx->threads);

    return 0;
}

#endif /* implementation */
