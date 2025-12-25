#ifndef ANNYMOOSE_QUEUE_H
#define ANNYMOOSE_QUEUE_H
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define ANNYMOOSE_QUEUE_ERROR -1
#define ANNYMOOSE_QUEUE_SUCCESS 0

typedef struct QueueNode {
    void* data;
    struct QueueNode* next;
} QueueNode_t;

typedef struct {
    QueueNode_t* head;
    QueueNode_t* tail;
    size_t length;
    size_t node_data_size;
    void (*free_data)(void* data);
} Queue_t;

/**
 * Initializes a queue to a state ready for usage.
 * @param queue The pointer to the queue that shall be initialized.
 * @param size_t The size of the data that will be stored in every node.
 * @param free_data Optional function pointer that will be called on every node deletion.
 */
static inline void Queue_init(Queue_t* queue, size_t data_size, void (*free_data)(void* data)) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->length = 0;
    queue->node_data_size = data_size;
    queue->free_data = free_data;
}

/**
 * Allocates a node at the end and copies data to it.
 * @param queue The pointer to the queue that's being appended to.
 * @param data Pointer to the data that will be copied. Must be node_data_size bytes long.
 * @returns 0 on success, sets errno and returns -1 otherwise.
 */
static inline int Queue_append(Queue_t* queue, const void* data) {
    if (!queue) {
        errno = EINVAL;
        return ANNYMOOSE_QUEUE_ERROR;
    }
    QueueNode_t* node = (QueueNode_t*)malloc(sizeof(QueueNode_t));
    if (!node) {
        errno = ENOMEM;
        return ANNYMOOSE_QUEUE_ERROR;
    }

    node->data = malloc(queue->node_data_size);
    if (!node->data) {
        free(node);
        errno = ENOMEM;
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

/**
 * Clears a Queue, de-allocating every node and it's contents.
 * @param queue The queue to be cleared.
 * @returns 0 on success, sets errno and returns -1 otherwise.
 */
static inline int Queue_clear(Queue_t* queue) {
    if (!queue) {
        errno = EINVAL;
        return ANNYMOOSE_QUEUE_ERROR;
    }

    if (queue->length == 0) return ANNYMOOSE_QUEUE_SUCCESS; /* list already empty */

    QueueNode_t *current_node = queue->head, *next_node = current_node;
    while (next_node) {
        current_node = next_node;
        next_node = current_node->next;

        if (current_node->data) {
            if (queue->free_data) queue->free_data(current_node->data);
            free(current_node->data);
        }

        free(current_node);
        queue->length--;
    }

    queue->head = NULL;
    queue->tail = NULL;

    return ANNYMOOSE_QUEUE_SUCCESS;
}

/**
 * Erases the first node without de-allocating the data.
 * @param queue The queue that will be popped from.
 * @returns Pointer to the data of the destroyed node, sets errno and returns NULL otherwise.
 */
static inline void* Queue_pop_head(Queue_t* queue) {
    if (!queue || !queue->head) {
        errno = EINVAL;
        return NULL;
    }

    QueueNode_t* head = queue->head;
    void* ret = head->data;

    queue->head = queue->head->next;
    queue->length--;
    if (!queue->head) /* sole element case */
        queue->tail = NULL;

    free(head);
    return ret;
}

#endif /* ANNYMOOSE_QUEUE_H */
