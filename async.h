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
 * All the promises that belong to this queue and haven't been previously completed/started are cancelled. A cancelled promise will set
 * errno to ECANCELED, and the data pointer will be set to NULL. You must await the canceled promises if you don't want to leak memory
 */
int DECL(destroy_tq)(DECL(thread_queue_t) * ctx);

/**
 * Submits a task to the thread queue
 * @param ctx Pointer to an initialized thread queue
 * @param callback The function that the thread will execute. Any function you would call pthread_create with would work.
 * @param arg The argument that will be passed to the callback function
 * @returns The pointer to the initialized promise on success. NULL on failure.
 *
 * ERRORS:
 * EINVAL - Invalid parameter (NULL ctx or NULL callback)
 * See man pages malloc(3), sem_init(3) for other possible errors
 */
DECL(promise_t) * DECL(submit_task)(DECL(thread_queue_t) * ctx, void* (*callback)(void*), void* arg);

/**
 * Submits a detached task to the thread queue. A detached task executes, and it's return value is then discarded.
 * See the documentation for submit_task for more info.
 * @returns 0 on success, -1 on failure.
 */
int DECL(submit_task_detached)(DECL(thread_queue_t) * ctx, void* (*callback)(void*), void* arg);

/**
 * Blocks on a promise until it is ready to be consumed, then consumes it.
 * @param promise The promise to be awaited.
 * @returns The value that was returned by the promise, or NULL if promise counldn't be consumed
 *
 * ERRORS:
 * ECANCELED - The promise was canceled.
 * If consumption succeeds, the calling thread's errno is set to the value of errno in the thread that executed the promise. This may result
 * in overlapping errno codes, where if your promise also sets errno to EINTR or ECANCELED and returns NULL, you can't differentiate between
 * success and failure.
 *
 * If the function is called with NULL or an invalid promise, the resulting behavior will be undefined.
 * The promise being "consumed" means that it should no longer be used after await, as it is de-allocated.
 */
void* DECL(await)(DECL(promise_t) * promise);

/**
 * Blocks on a promise until it is ready to be consumed or timeout is expired. The promise is consumed if timeout isn't reached
 * @param promise The promise to be awaited.
 * @param timeout The amount of time to be waited for at most. May be NULL to poll.
 *
 * See documentation for await for additional info.
 *
 * ADDITIONAL ERRORS:
 * ETIMEDOUT - Promise could not be consumed before the timeout expired.
 * EAGAIN - Promise could not be consumed immediatelly (in the case that timeout is NULL).
 */
void* DECL(timed_await)(DECL(promise_t) * promise, const struct timespec* timeout);

/**
 * Removes a task if a condition on a predicate succeeds.
 * @param ctx The thread queue to remove the task from.
 * @param pred The function that decides whether a task should be removed. Should return 0 if not, any non-zero value if yes.
 * @returns The amounts of removed elements on success, -1 on failure.
 *
 * ERRORS:
 * EINVAL - Invalid parameters (ctx or pred is NULL)
 *
 * Example of a predicate function:
 *
 * ~~~{.c}
 * extern void* work_function(void* arg);
 *
 * int pred(const void* ptr) {
 *     const task_t* task = (const task_t*)ptr;
 *
 *     if (task->callback == task) return 1; // remove task
 *     return 0; // don't
 * }
 * ~~~
 * Please note that if your function interacts with the `promise` field of a task, it should always validate whether the promise exists. (it
 * doesn't for detached tasks.)
 *
 * Important notice:
 * The promises of tasks removed are cancelled, you should still await them.
 * This function will not cancel any tasks that are ongoing, and those promises should still be collected.
 */
ssize_t DECL(remove_tasks)(DECL(thread_queue_t) * ctx, int (*pred)(const void*));

#endif /* ANNYMOOSE_ASYNC_H */

// #define ANNYMOOSE_ASYNC_IMPLEMENTATION /* syntax highlighting my beloved */
#ifdef ANNYMOOSE_ASYNC_IMPLEMENTATION
#include <errno.h>
#include <signal.h>

#define ANNYMOOSE_QUEUE_IMPLEMENTATION /* assume queue implementation is also required */
#include "queue.h"

/* the promise in the task is not freed, but set to an awaitable state. */
void DECL(__free_func)(void* task) {
    if (task == NULL) return;
    DECL(task_t)* t = (DECL(task_t)*)task;

    if (t->promise) {
        t->promise->err_code = ECANCELED;
        t->promise->data = NULL;
        sem_post(&t->promise->done);
    }
}

void* DECL(__worker_func)(void* arg) {
    DECL(thread_queue_t)* state = (DECL(thread_queue_t)*)arg;

    pthread_mutex_lock(&state->queue_mtx);

    while (1) {
        while (state->queue.length > 0) {
            DECL(task_t)* task = (DECL(task_t)*)queue_pop_head(&state->queue);
            pthread_mutex_unlock(&state->queue_mtx);

            if (task) {
                if (task->promise) {
                    task->promise->data = task->callback(task->args);
                    task->promise->err_code = errno;
                    sem_post(&task->promise->done);
                } else
                    task->callback(task->args);

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

static inline DECL(promise_t) * DECL(__init_promise)() {
    DECL(promise_t)* promise = (DECL(promise_t)*)malloc(sizeof(DECL(promise_t)));
    if (!promise) return NULL;

    promise->data = NULL;
    promise->err_code = 0;
    if (sem_init(&promise->done, 0, 0)) {
        free(promise);
        return NULL;
    }

    return promise;
}

/* if detached, returns 1 on success. if not, it returns the pointer to the promise. in both cases NULL is returned if fails. */
static inline DECL(promise_t) * DECL(__submit_task)(DECL(thread_queue_t) * ctx, void* (*callback)(void*), void* arg, int detached) {
    if (ctx == NULL || callback == NULL) {
        errno = EINVAL;
        goto fail;
    }

    DECL(promise_t)* promise = NULL;
    if (!detached) {
        promise = DECL(__init_promise)();
        if (!promise) goto fail;
    }

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

    return !detached ? promise : (DECL(promise_t)*)1;

fail_post_promise_init:
    if (!detached) {
        sem_destroy(&promise->done);
        free(promise);
    }
fail:
    return NULL;
}

int DECL(submit_task_detached)(DECL(thread_queue_t) * ctx, void* (*callback)(void*), void* arg) {
    if (!DECL(__submit_task)(ctx, callback, arg, 1)) return -1;

    return 0;
}

DECL(promise_t) * DECL(submit_task)(DECL(thread_queue_t) * ctx, void* (*callback)(void*), void* arg) {
    return DECL(__submit_task)(ctx, callback, arg, 0);
}

ssize_t DECL(remove_tasks)(DECL(thread_queue_t) * ctx, int (*pred)(const void*)) {
    if (ctx == NULL || pred == NULL) {
        errno = EINVAL;
        return -1;
    }
    pthread_mutex_lock(&ctx->queue_mtx);
    /* life could be a dream if i could have the callback expect task_t* and cast it to a function that expects void* and it would just
     * work, but no, the language doesn't allow that. i must require the user to manually cast void* to task_t* in their callbacks, even
     * though it will always be task_t* in this case, which is sucky design. day ruined. */
    ssize_t retval = queue_remove_if(&ctx->queue, pred);
    pthread_mutex_unlock(&ctx->queue_mtx);
    return retval;
}

static inline void* DECL(__consume_promise)(DECL(promise_t) * promise) {
    sem_destroy(&promise->done);
    void* ret_val = promise->data;
    errno = promise->err_code;
    free(promise);

    return ret_val;
}

void* DECL(timed_await)(DECL(promise_t) * promise, const struct timespec* abstime) {
    if (abstime) {
        while (sem_timedwait(&promise->done, abstime))
            if (errno != EINTR) return NULL;
    } else {
        if (sem_trywait(&promise->done)) return NULL;
    }

    return DECL(__consume_promise)(promise);
}

void* DECL(await)(DECL(promise_t) * promise) {
    while (sem_wait(&promise->done)); /* wait until waited */
    return DECL(__consume_promise)(promise);
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
