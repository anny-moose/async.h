#ifndef ANNYLIB_H
#define ANNYLIB_H
#include <stdlib.h>
#include <string.h>

#define ANNYLIB_OUTOFRANGE -2
#define ANNYLIB_ALLOCATIONERR -1
#define ANNYLIB_SUCCESS 0
#define ANNYLIB_NULLOBJ 1

typedef struct LListNode {
    struct LListNode* prev;
    void* data;
    struct LListNode* next;
} LListNode_t;

typedef struct LList {
    LListNode_t* head;
    LListNode_t* tail;
    size_t length;
    size_t node_data_size;
    void (*free_data)(void* data);
} LList_t;

/* LList_init:           Initializes an LList_t list.
 *                       Requires LList_t pointer and sizeof(T) of the type of
 * data in your list. free_data called when destroying nodes, may be NULL if
 * specialized destruction not required Doesn't clear the list preemptively!!
 * Will leak memory if presented with initialized list!!
 */
static inline void LList_init(LList_t* list, size_t data_size, void (*free_data)(void* data)) {
    list->head = NULL;
    list->tail = NULL;
    list->length = 0;
    list->node_data_size = data_size;
    list->free_data = free_data;
}

/* LList_append:         Copies data and appends to list.
 *                       Requires list pointer and any data.
 *                       Data size is expected to be equal to
 * list->node_data_size. Returns 0 on success, 1 if list is NULL, -1 if
 * allocation fails.
 */
static inline int LList_append(LList_t* list, const void* data) {
    if (!list) return ANNYLIB_NULLOBJ;
    LListNode_t* node = (LListNode_t*)malloc(sizeof(LListNode_t));
    if (!node) return ANNYLIB_ALLOCATIONERR;

    node->data = malloc(list->node_data_size);
    if (!node->data) {
        free(node);
        return ANNYLIB_ALLOCATIONERR;
    }

    memcpy(node->data, data, list->node_data_size);

    if (list->tail != NULL) {
        node->prev = list->tail;
        list->tail->next = node;
        node->next = NULL;
        list->tail = node;
    } else {
        list->head = node;
        node->prev = NULL;
        node->next = NULL;
        list->tail = node;
    }
    list->length++;

    return ANNYLIB_SUCCESS;
}

/* LList_erase_node:     Erases a node from the list. Accepts list pointer and
 * node pointer. Doesn't check whether node belongs to list. De-allocates the
 * data along with the node. Returns 0 on success, 1 if list/node is NULL
 */
static inline int LList_erase_node(LList_t* list, LListNode_t* node) {
    if (!list || !node) return ANNYLIB_NULLOBJ;

    if (node->data) {
        if (list->free_data) list->free_data(node->data);
        free(node->data);
    }

    if (!node->prev) {
        /* we're at head */
        list->head = node->next; /* becomes either valid ptr or NULL */
        if (node->next) {
            node->next->prev = NULL;
        } else {
            list->tail = NULL;
        }
    } else if (!node->next) {
        /* we're at tail */
        list->tail = node->prev;
        node->prev->next = NULL;
    } else {
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    free(node);

    list->length--;

    return ANNYLIB_SUCCESS;
}

/* LList_erase_idx:      Erases a node via index.
 *                       Returns -2 on out of range, 1 if list is NULL.
 *                       Calls LList_erase_node and returns it's result.
 */
static inline int LList_erase_idx(LList_t* list, size_t idx) {
    if (!list) return ANNYLIB_NULLOBJ;

    if (idx >= list->length) return ANNYLIB_OUTOFRANGE;

    LListNode_t* node = list->head;
    for (size_t i = 0; i < idx; i++) node = node->next;

    return LList_erase_node(list, node);
}

/* LList_empty:          Clears out a list completely, de-allocating every node
 * along with it's content. Returns 0 on success, 1 if list is NULL. Repeatedly
 * calls LList_erase_idx on index 0.
 */
static inline int LList_empty(LList_t* list) {
    if (!list) return ANNYLIB_NULLOBJ;
    if (list->length == 0) return ANNYLIB_SUCCESS; /* list already empty */

    while (LList_erase_idx(list, 0) == ANNYLIB_SUCCESS);

    return ANNYLIB_SUCCESS;
}

/* LList_get:            Returns a node pointer at index idx. Returns NULL if
 * list is NULL or index is out of range
 */
static inline LListNode_t* LList_get(LList_t* list, size_t idx) {
    if (!list) return NULL;
    if (idx >= list->length) return NULL;

    LListNode_t* node = list->head;
    for (size_t i = 0; i < idx; i++) node = node->next;

    return node;
}

/* LList_get_data:       Returns a pointer to the data at index idx. Returns
 * NULL if list is NULL or index is out of range. Unlike LList_get,
 * LList_get_data accepts a const pointer to list, as you can not ruin the list
 * with a pointer to data. Returned data is NOT constant.
 */
static inline void* LList_get_data(const LList_t* list, size_t idx) {
    if (!list) return NULL;
    if (idx >= list->length) return NULL;

    LListNode_t* node = list->head;
    for (size_t i = 0; i < idx; i++) node = node->next;

    return node->data;
}

static inline void* LList_pop_head(LList_t* list) {
    if (!list || !list->head) return NULL;

    void* ret = list->head->data;

    LListNode_t* node = list->head;

    list->head = list->head->next;
    list->length--;
    if (!list->head) { /* sole element case */
        list->tail = NULL;
        goto end;
    }

    list->head->prev = NULL;

end:
    free(node);
    return ret;
}

#endif /* ANNYLIB_H */
