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

int DECL(init_promise)(DECL(promise_t) * promise);

DECL(promise_t) * DECL(submit_task)(DECL(thread_queue_t) * ctx, void* (*callback)(void*), void* arg);
void* DECL(await)(DECL(promise_t) * promise);

/*
 *
 */

#endif /* ANNYMOOSE_ASYNC_H */

#ifdef ANNYMOOSE_ASYNC_IMPLEMENTATION
#include <err.h>
#include <errno.h>

void DECL(__free_func)(void* task) {
    if (task == NULL) return;
    DECL(task_t)* t = (DECL(task_t)*)task;

    free(t->promise);
}

void* DECL(__worker_func)(void* arg) {
    DECL(thread_queue_t)* state = (DECL(thread_queue_t)*)arg;

    pthread_mutex_lock(&state->queue_mtx);

    while (1) {
        while (state->queue.length > 0) {
            DECL(task_t)* task = (DECL(task_t)*)Queue_pop_head(&state->queue);
            pthread_mutex_unlock(&state->queue_mtx);
            if (!task) break;

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

        if (pthread_cond_wait(&state->cv, &state->queue_mtx) != 0) err(EXIT_FAILURE, "failed waiting on CV");
    }

    return NULL;
}

void* DECL(await)(DECL(promise_t) * promise) {
    sem_wait(&promise->done);
    sem_destroy(&promise->done);
    void* ret_val = promise->data;
    errno = promise->err_code;
    free(promise);

    return ret_val;
}

int DECL(init_queue)(DECL(thread_queue_t) * ctx, int num_threads) {
    if (ctx == NULL || num_threads < 1) {
        errno = EINVAL;
        goto fail;
    }

    if (pthread_mutex_init(&ctx->queue_mtx, NULL)) goto fail;
    if (pthread_cond_init(&ctx->cv, NULL)) goto fail_post_mutex;

    ctx->num_threads = num_threads;
    Queue_init(&ctx->queue, sizeof(DECL(task_t)), DECL(__free_func));

    ctx->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    if (!ctx->threads) goto fail_post_cond;

    int initialized_threads;
    for (initialized_threads = 0; initialized_threads < num_threads;) {
        if (!pthread_create(&ctx->threads[initialized_threads], NULL, DECL(__worker_func), ctx))
            initialized_threads++;
        else
            goto fail_post_threads;
    }

    return 0;

fail_post_threads:
    for (int i = 0; i < initialized_threads; i++) pthread_cancel(ctx->threads[i]);
    free(ctx->threads);
fail_post_cond:
    pthread_cond_destroy(&ctx->cv);
fail_post_mutex:
    pthread_mutex_destroy(&ctx->queue_mtx);
fail:
    return -1;
}

int DECL(init_promise)(DECL(promise_t) * promise) {
    if (promise == NULL) {
        errno = EINVAL;
        return -1;
    }

    promise->data = NULL;
    if (sem_init(&promise->done, 0, 0)) return -1;

    return 0;
}

DECL(promise_t) * DECL(submit_task)(DECL(thread_queue_t) * ctx, void* (*callback)(void*), void* arg) {
    if (ctx == NULL || callback == NULL) {
        errno = EINVAL;
        goto fail;
    }

    DECL(promise_t)* promise = (DECL(promise_t)*)malloc(sizeof(DECL(promise_t)));
    if (!promise) goto fail;

    if (DECL(init_promise)(promise)) goto fail_post_alloc;

    DECL(task_t)
    task = {
        .args = arg,
        .callback = callback,
        .promise = promise,
    };

    pthread_mutex_lock(&ctx->queue_mtx);
    if (Queue_append(&ctx->queue, &task)) goto fail_post_promise_init;
    pthread_mutex_unlock(&ctx->queue_mtx);

    pthread_cond_signal(&ctx->cv);

    return promise;

fail_post_promise_init:
    sem_destroy(&promise->done);
fail_post_alloc:
    free(promise);
fail:
    return NULL;
}

int DECL(destroy_queue)(DECL(thread_queue_t) * ctx) {
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

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
