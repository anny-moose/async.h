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
/* i really like the fact that this requires THREE macros instead of one or two */

typedef struct thread_queue {
    pthread_t* threads;
    pthread_mutex_t queue_mtx;
    queue_t queue;
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

/**
 * Initializes a thread queue with n threads.
 * @param ctx Pointer to the queue to initialize. Expected to not be previously initialized.
 * @param num_threads The number of threads to be started. If for whatever reason they can't be started, the queue is not initialized.
 * @returns 0 on success, -1 if any step fails
 *
 * ERRORS:
 * EINVAL - Invalid parameters (NULL ctx or num_threads < 1)
 * See man pages pthread_cond_init(3), pthread_mutex_init(3), malloc(3), pthread_create(3) for other possible errors.
 */
int DECL(init_tq)(DECL(thread_queue_t) * ctx, int num_threads);

/**
 * Uninitializes a thread queue.
 * @param ctx Pointer to an initialized thread queue
 * @returns 0 on success, -1 on error
 *
 * ERRORS:
 * EINVAL - Invalid parameter (NULL ctx)
 *
 * All the promises that belong to this queue and haven't been previously completed are invalidated. Awaiting on them will result in
 * undefined behavior.
 */
int DECL(destroy_tq)(DECL(thread_queue_t) * ctx);

/**
 * Submits a task to the thread queue
 * @param ctx Pointer to an initialized thread queue
 * @param callback The function that the thread will execute. Any function you would call pthread_create with would work.
 * @param arg The argument that will be passed to the callback function
 * @returns 0 on success, -1 on error
 *
 * ERRORS:
 * EINVAL - Invalid parameter (NULL ctx or NULL callback)
 * See man pages malloc(3), sem_init(3) for other possible errors
 */
DECL(promise_t) * DECL(submit_task)(DECL(thread_queue_t) * ctx, void* (*callback)(void*), void* arg);

/**
 * Blocks on a promise until it is ready to be consumed, then consumes it.
 * @param promise The promise to be awaited.
 * @returns The value that was returned by the promise.
 *
 * ERRORS:
 * This function doesn't define errors. The calling thread's errno is set to the value of errno in the thread executing the promise.
 *
 * If the function is called with NULL or an invalid promise, the resulting behavior will be undefined.
 * The promise being "consumed" means that it should no longer be used after await, as it is de-allocated.
 */
void* DECL(await)(DECL(promise_t) * promise);

#endif /* ANNYMOOSE_ASYNC_H */

// #define ANNYMOOSE_ASYNC_IMPLEMENTATION /* syntax highlighting my beloved */
#ifdef ANNYMOOSE_ASYNC_IMPLEMENTATION
#include <errno.h>
#include <signal.h>

#define ANNYMOOSE_QUEUE_IMPLEMENTATION /* assume queue implementation is also required */
#include "queue.h"

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
            DECL(task_t)* task = (DECL(task_t)*)queue_pop_head(&state->queue);
            pthread_mutex_unlock(&state->queue_mtx);

            if (task) {
                task->promise->data = task->callback(task->args);
                task->promise->err_code = errno;
                sem_post(&task->promise->done);

                free(task);

                /* maybe should restore sigmask in case it gets changed by the task */
            }

            pthread_mutex_lock(&state->queue_mtx);
        }

        if (state->num_threads == -1) {
            pthread_mutex_unlock(&state->queue_mtx);
            break;
        }

        pthread_cond_wait(&state->cv, &state->queue_mtx);
    }

    return NULL;
}

int DECL(init_tq)(DECL(thread_queue_t) * ctx, int num_threads) {
    if (ctx == NULL || num_threads < 1) {
        errno = EINVAL;
        goto fail;
    }

    if (pthread_mutex_init(&ctx->queue_mtx, NULL)) goto fail;
    if (pthread_cond_init(&ctx->cv, NULL)) goto fail_post_mutex;

    ctx->num_threads = num_threads;
    queue_init(&ctx->queue, sizeof(DECL(task_t)), DECL(__free_func));

    ctx->threads = (pthread_t*)malloc(sizeof(pthread_t) * num_threads);
    if (!ctx->threads) goto fail_post_cond;

    sigset_t fullset, oldset;
    sigfillset(&fullset);
    pthread_sigmask(SIG_SETMASK, &fullset, &oldset); /* block all signals, as handling them in the worker threads would be troublesome */

    int initialized_threads;
    for (initialized_threads = 0; initialized_threads < num_threads;) {
        if (!pthread_create(&ctx->threads[initialized_threads], NULL, DECL(__worker_func), ctx))
            initialized_threads++;
        else
            goto fail_post_threads;
    }

    pthread_sigmask(SIG_SETMASK, &oldset, NULL);

    return 0;

fail_post_threads:
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
    for (int i = 0; i < initialized_threads; i++) pthread_cancel(ctx->threads[i]);
    free(ctx->threads);
fail_post_cond:
    pthread_cond_destroy(&ctx->cv);
fail_post_mutex:
    pthread_mutex_destroy(&ctx->queue_mtx);
fail:
    return -1;
}

int DECL(__init_promise)(DECL(promise_t) * promise) {
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

    if (DECL(__init_promise)(promise)) goto fail_post_alloc;

    DECL(task_t)
    task = {
        .args = arg,
        .callback = callback,
        .promise = promise,
    };

    pthread_mutex_lock(&ctx->queue_mtx);
    if (queue_append(&ctx->queue, &task)) {
        pthread_mutex_unlock(&ctx->queue_mtx);
        goto fail_post_promise_init;
    };
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

void* DECL(await)(DECL(promise_t) * promise) {
    sem_wait(&promise->done);
    sem_destroy(&promise->done);
    void* ret_val = promise->data;
    errno =
        promise->err_code; /* I'm not really sure whether i like this, as this effectively disallows adding any checks to this function */
    free(promise);

    return ret_val;
}

int DECL(destroy_tq)(DECL(thread_queue_t) * ctx) {
    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&ctx->queue_mtx);
    queue_clear(&ctx->queue);
    int nthreads = ctx->num_threads;
    ctx->num_threads = -1;
    pthread_mutex_unlock(&ctx->queue_mtx);

    pthread_cond_broadcast(&ctx->cv);

    for (int i = 0; i < nthreads; i++) {
        pthread_join(ctx->threads[i], NULL);
    }

    free(ctx->threads);

    pthread_cond_destroy(&ctx->cv);
    pthread_mutex_destroy(&ctx->queue_mtx);

    return 0;
}

#endif /* implementation */
