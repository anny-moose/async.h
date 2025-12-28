#ifndef ANNYMOOSE_QUEUE_H
#define ANNYMOOSE_QUEUE_H
#include <stddef.h>
#include <sys/types.h>

typedef struct queue_node {
    void* data;
    struct queue_node* next;
} queue_node_t;

typedef struct {
    queue_node_t* head;
    queue_node_t* tail;
    size_t length;
    size_t node_data_size;
    void (*free_data)(void* data);
} queue_t;

/**
 * Initializes a queue to a state ready for usage.
 * @param queue The pointer to the queue that shall be initialized.
 * @param size_t The size of the data that will be stored in every node.
 * @param free_data Optional function pointer that will be called on every node deletion.
 */
void queue_init(queue_t* queue, size_t data_size, void (*free_data)(void* data));

/**
 * Allocates a node at the end and copies data to it.
 * @param queue The pointer to the queue that's being appended to.
 * @param data Pointer to the data that will be copied. Must be node_data_size bytes long.
 * @returns 0 on success, sets errno and returns -1 otherwise.
 *
 * ERRORS:
 * EINVAL - Invalid arguments. (queue or data is NULL)
 * See man page malloc(3) for other error codes.
 */
int queue_append(queue_t* queue, const void* data);

/**
 * Removes an arbitrary node from the queue.
 * @param queue The queue to remove the node from.
 * @param node The node that should be removed.
 * @returns 0 on success, sets errno and returns -1 otherwise.
 *
 * ERRORS:
 * EINVAL - Invalid arguments. (queue or node is NULL)
 * ENOENT - No such node found in the queue.
 */
int queue_remove(queue_t* queue, queue_node_t* node);

/**
 * Removes an nodes from a queue based on a condition.
 * @param queue The queue to remove the node(s) from.
 * @param callback The function that will be called to check whether the node should be removed. The function should return 0 if the node
 * shouldn't be removed and any non-zero value otherwise
 * @returns Count of deleted nodes on success, sets errno and returns -1 otherwise.
 *
 * ERRORS:
 * EINVAL - Invalid arguments. (queue or callback is NULL)
 */
ssize_t queue_remove_if(queue_t* queue, int (*callback)(const queue_node_t*));

/**
 * Clears a Queue, de-allocating every node and it's contents.
 * @param queue The queue to be cleared.
 * @returns 0 on success, sets errno and returns -1 otherwise.
 *
 * ERRORS:
 * EINVAL - Invalid arguments. (queue is NULL)
 */
int queue_clear(queue_t* queue);

/**
 * Erases the first node without de-allocating the data.
 * @param queue The queue that will be popped from.
 * @returns Pointer to the data of the destroyed node, sets errno and returns NULL otherwise.
 *
 * ERRORS:
 * EINVAL - Invalid arguments. (queue is NULL)
 * ENOENT - Queue is empty.
 */
void* queue_pop_head(queue_t* queue);

#endif /* ANNYMOOSE_QUEUE_H */

/*
 *
 */

// #define ANNYMOOSE_QUEUE_IMPLEMENTATION
#ifdef ANNYMOOSE_QUEUE_IMPLEMENTATION
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define ANNYMOOSE_QUEUE_ERROR -1
#define ANNYMOOSE_QUEUE_SUCCESS 0

void queue_init(queue_t* queue, size_t data_size, void (*free_data)(void* data)) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->length = 0;
    queue->node_data_size = data_size;
    queue->free_data = free_data;
}

int queue_append(queue_t* queue, const void* data) {
    if (!queue || !data) {
        errno = EINVAL;
        return ANNYMOOSE_QUEUE_ERROR;
    }
    queue_node_t* node = (queue_node_t*)malloc(sizeof(queue_node_t));
    if (!node) {
        return ANNYMOOSE_QUEUE_ERROR;
    }

    node->data = malloc(queue->node_data_size);
    if (!node->data) {
        free(node);
        return ANNYMOOSE_QUEUE_ERROR;
    }

    memcpy(node->data, data, queue->node_data_size);

    if (queue->tail)
        queue->tail->next = node;
    else
        queue->head = node;

    node->next = NULL;
    queue->tail = node;
    queue->length++;

    return ANNYMOOSE_QUEUE_SUCCESS;
}

/* unsafe deleter */
static inline queue_node_t* __queue_delete_node(queue_t* queue, queue_node_t* prev) {
    queue_node_t* node = prev ? prev->next : queue->head;

    if (!node) return NULL;

    if (prev)
        prev->next = node->next;
    else
        queue->head = node->next;

    if (node == queue->tail) queue->tail = prev;

    if (node->data) {
        if (queue->free_data) queue->free_data(node->data);
        free(node->data);
    }
    free(node);
    queue->length--;

    return prev ? prev->next : queue->head;
}

int queue_remove(queue_t* queue, queue_node_t* node) {
    if (queue == NULL || node == NULL) {
        errno = EINVAL;
        return ANNYMOOSE_QUEUE_ERROR;
    }

    if (node == queue->head) {
        __queue_delete_node(queue, NULL);
        return 0;
    }

    queue_node_t* prev = queue->head;

    while (prev != NULL) {
        if (prev->next == node) break;
        prev = prev->next;
    }

    if (prev == NULL) { /* means node isn't in this queue */
        errno = ENOENT;
        return ANNYMOOSE_QUEUE_ERROR;
    }

    __queue_delete_node(queue, prev);

    return 0;
}

ssize_t queue_remove_if(queue_t* queue, int (*callback)(const queue_node_t*)) {
    if (queue == NULL || callback == NULL) {
        errno = EINVAL;
        return ANNYMOOSE_QUEUE_ERROR;
    }

    size_t removed_nodes = 0;

    queue_node_t* next;
    queue_node_t* cursor = queue->head;
    while (cursor != NULL) {
        if (cursor == queue->head) {
            if (callback(cursor)) {
                cursor = __queue_delete_node(queue, NULL);
                removed_nodes++;
                continue;
            }
        }

        next = cursor->next;
        if (next == NULL) break;

        if (callback(next)) {
            __queue_delete_node(queue, cursor);
            removed_nodes++;
            continue;
        }

        cursor = cursor->next;
    }

    return removed_nodes;
}

int queue_clear(queue_t* queue) {
    if (!queue) {
        errno = EINVAL;
        return ANNYMOOSE_QUEUE_ERROR;
    }

    if (queue->length == 0) return ANNYMOOSE_QUEUE_SUCCESS; /* list already empty */

    queue_node_t* node = __queue_delete_node(queue, NULL);
    while (node) {
        node = __queue_delete_node(queue, NULL); /* not quite sure about this, kind of slows it down */
    }

    return ANNYMOOSE_QUEUE_SUCCESS;
}

void* queue_pop_head(queue_t* queue) {
    if (!queue) {
        errno = EINVAL;
        return NULL;
    } else if (!queue->head) {
        errno = ENOENT;
        return NULL;
    }

    queue_node_t* head = queue->head;
    void* ret = head->data;

    queue->head = queue->head->next;
    queue->length--;
    if (!queue->head) /* sole element case */
        queue->tail = NULL;

    free(head);
    return ret;
}
#endif /* implementation */
