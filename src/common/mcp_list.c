/**
 * @file mcp_list.c
 * @brief Implementation of generic doubly linked list
 */

#include "mcp_list.h"
#include <stdlib.h>
#include <assert.h>
#include "mcp_log.h"
#include "mcp_buffer_pool.h"
#include "mcp_sync.h"

// Memory pool size definition
#define MCP_LIST_NODE_POOL_SIZE 64

// Thread safety helper macros
#define MCP_LIST_LOCK(list) \
    do { \
        if ((list)->thread_safety == MCP_LIST_THREAD_SAFE && (list)->mutex) { \
            mcp_mutex_lock((list)->mutex); \
        } \
    } while (0)

#define MCP_LIST_UNLOCK(list) \
    do { \
        if ((list)->thread_safety == MCP_LIST_THREAD_SAFE && (list)->mutex) { \
            mcp_mutex_unlock((list)->mutex); \
        } \
    } while (0)

// Internal structure for tracking list memory pool
typedef struct {
    mcp_buffer_pool_t* node_pool;  // Node memory pool
    size_t pool_refs;              // Reference count
} mcp_list_pool_t;

// Global memory pool
static mcp_list_pool_t* g_list_pool = NULL;

// Initialize global memory pool
static mcp_list_pool_t* mcp_list_pool_get(void) {
    if (!g_list_pool) {
        g_list_pool = (mcp_list_pool_t*)malloc(sizeof(mcp_list_pool_t));
        if (!g_list_pool) {
            mcp_log_error("Failed to allocate list pool structure");
            return NULL;
        }

        g_list_pool->node_pool = mcp_buffer_pool_create(sizeof(mcp_list_node_t), MCP_LIST_NODE_POOL_SIZE);
        if (!g_list_pool->node_pool) {
            mcp_log_error("Failed to create list node pool");
            free(g_list_pool);
            g_list_pool = NULL;
            return NULL;
        }

        g_list_pool->pool_refs = 0;
    }

    g_list_pool->pool_refs++;
    return g_list_pool;
}

// Release reference to global memory pool
static void mcp_list_pool_release(void) {
    if (g_list_pool && g_list_pool->pool_refs > 0) {
        g_list_pool->pool_refs--;

        if (g_list_pool->pool_refs == 0) {
            mcp_buffer_pool_destroy(g_list_pool->node_pool);
            free(g_list_pool);
            g_list_pool = NULL;
        }
    }
}

// Allocate node from memory pool
static mcp_list_node_t* mcp_list_node_alloc(void) {
    if (!g_list_pool || !g_list_pool->node_pool) {
        return (mcp_list_node_t*)malloc(sizeof(mcp_list_node_t));
    }

    mcp_list_node_t* node = (mcp_list_node_t*)mcp_buffer_pool_acquire(g_list_pool->node_pool);
    if (!node) {
        // If memory pool is full, fall back to using malloc
        return (mcp_list_node_t*)malloc(sizeof(mcp_list_node_t));
    }

    return node;
}

// Free node back to memory pool
static void mcp_list_node_free(mcp_list_node_t* node) {
    if (!node) return;

    if (g_list_pool && g_list_pool->node_pool) {
        // Try to release to memory pool
        mcp_buffer_pool_release(g_list_pool->node_pool, node);
    } else {
        // If no memory pool, free directly
        free(node);
    }
}

mcp_list_t* mcp_list_create(mcp_list_thread_safety_t thread_safety) {
    // Initialize memory pool
    if (!mcp_list_pool_get()) {
        mcp_log_error("Failed to initialize list memory pool");
        // Continue execution, will fall back to using malloc
    }

    mcp_list_t* list = (mcp_list_t*)malloc(sizeof(mcp_list_t));
    if (!list) {
        mcp_log_error("Failed to allocate list structure");
        mcp_list_pool_release();
        return NULL;
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    list->thread_safety = thread_safety;

    // If thread safety is required, create mutex
    if (thread_safety == MCP_LIST_THREAD_SAFE) {
        list->mutex = mcp_mutex_create();
        if (!list->mutex) {
            mcp_log_error("Failed to create list mutex");
            free(list);
            mcp_list_pool_release();
            return NULL;
        }
    } else {
        list->mutex = NULL;
    }

    return list;
}

void mcp_list_destroy(mcp_list_t* list, void (*free_data)(void*)) {
    if (!list) return;

    // Directly clear the list. mcp_list_clear() handles its own locking,
    // so taking the lock here would result in a deadlock with non-recursive
    // mutexes.
    mcp_list_clear(list, free_data);

    // Destroy mutex if it exists
    if (list->thread_safety == MCP_LIST_THREAD_SAFE && list->mutex) {
        mcp_mutex_destroy(list->mutex);
    }

    free(list);

    // Release memory pool reference
    mcp_list_pool_release();
}

mcp_list_node_t* mcp_list_push_front(mcp_list_t* list, void* data) {
    if (!list) return NULL;

    mcp_list_node_t* node = mcp_list_node_alloc();
    if (!node) {
        mcp_log_error("Failed to allocate list node");
        return NULL;
    }

    node->data = data;
    node->prev = NULL;

    // Lock
    MCP_LIST_LOCK(list);

    node->next = list->head;

    if (list->head) {
        list->head->prev = node;
    } else {
        // List was empty, so this is also the tail
        list->tail = node;
    }

    list->head = node;
    list->size++;

    // Unlock
    MCP_LIST_UNLOCK(list);

    return node;
}

mcp_list_node_t* mcp_list_push_back(mcp_list_t* list, void* data) {
    if (!list) return NULL;

    mcp_list_node_t* node = mcp_list_node_alloc();
    if (!node) {
        mcp_log_error("Failed to allocate list node");
        return NULL;
    }

    node->data = data;
    node->next = NULL;

    // Lock
    MCP_LIST_LOCK(list);

    node->prev = list->tail;

    if (list->tail) {
        list->tail->next = node;
    } else {
        // List was empty, so this is also the head
        list->head = node;
    }

    list->tail = node;
    list->size++;

    // Unlock
    MCP_LIST_UNLOCK(list);

    return node;
}

void* mcp_list_remove(mcp_list_t* list, mcp_list_node_t* node, void (*free_data)(void*)) {
    if (!list || !node) return NULL;

    void* data = node->data;

    // Lock
    MCP_LIST_LOCK(list);

    // Update adjacent nodes
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        // Node was the head
        list->head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        // Node was the tail
        list->tail = node->prev;
    }

    // Update list size
    list->size--;

    // Unlock
    MCP_LIST_UNLOCK(list);

    // Free data if requested
    if (free_data && data) {
        free_data(data);
        data = NULL;
    }

    // Free node back to memory pool
    mcp_list_node_free(node);

    return data;
}

void* mcp_list_pop_front(mcp_list_t* list) {
    if (!list || !list->head) return NULL;

    // Lock
    MCP_LIST_LOCK(list);

    mcp_list_node_t* node = list->head;
    void* data = node->data;

    list->head = node->next;
    if (list->head) {
        list->head->prev = NULL;
    } else {
        // List is now empty
        list->tail = NULL;
    }

    // Update list size
    list->size--;

    // Unlock
    MCP_LIST_UNLOCK(list);

    // Free node back to memory pool
    mcp_list_node_free(node);

    return data;
}

void* mcp_list_pop_back(mcp_list_t* list) {
    if (!list || !list->tail) return NULL;

    // Lock
    MCP_LIST_LOCK(list);

    mcp_list_node_t* node = list->tail;
    void* data = node->data;

    list->tail = node->prev;
    if (list->tail) {
        list->tail->next = NULL;
    } else {
        // List is now empty
        list->head = NULL;
    }

    // Update list size
    list->size--;

    // Unlock
    MCP_LIST_UNLOCK(list);

    // Free node back to memory pool
    mcp_list_node_free(node);

    return data;
}

void mcp_list_move_to_front(mcp_list_t* list, mcp_list_node_t* node) {
    if (!list || !node || node == list->head) return;

    // Lock
    MCP_LIST_LOCK(list);

    // Remove node from its current position
    if (node->prev) {
        node->prev->next = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        // Node was the tail
        list->tail = node->prev;
    }

    // Insert at the front
    node->prev = NULL;
    node->next = list->head;

    if (list->head) {
        list->head->prev = node;
    } else {
        // List was empty
        list->tail = node;
    }

    list->head = node;

    // Unlock
    MCP_LIST_UNLOCK(list);
}

size_t mcp_list_size(const mcp_list_t* list) {
    return list ? list->size : 0;
}

bool mcp_list_is_empty(const mcp_list_t* list) {
    return list ? list->size == 0 : true;
}

void mcp_list_clear(mcp_list_t* list, void (*free_data)(void*)) {
    if (!list) return;

    // Lock
    MCP_LIST_LOCK(list);

    mcp_list_node_t* current = list->head;
    mcp_list_node_t* nodes_to_free = current; // Save the list of nodes to free

    // Quickly clear the list
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;

    // Unlock
    MCP_LIST_UNLOCK(list);

    // Free nodes and data outside the lock to avoid holding the lock for too long
    while (nodes_to_free) {
        mcp_list_node_t* next = nodes_to_free->next;

        if (free_data && nodes_to_free->data) {
            free_data(nodes_to_free->data);
        }

        // Free node back to memory pool
        mcp_list_node_free(nodes_to_free);
        nodes_to_free = next;
    }
}

// Iterator interface
mcp_list_iterator_t mcp_list_iterator_begin(const mcp_list_t* list) {
    mcp_list_iterator_t it = { NULL };
    if (list) {
        // Lock
        if (list->thread_safety == MCP_LIST_THREAD_SAFE && list->mutex) {
            mcp_mutex_lock(list->mutex);
        }

        it.node = list->head;

        // Unlock
        if (list->thread_safety == MCP_LIST_THREAD_SAFE && list->mutex) {
            mcp_mutex_unlock(list->mutex);
        }
    }
    return it;
}

mcp_list_iterator_t mcp_list_iterator_end(const mcp_list_t* list) {
    (void)list; // Suppress unused parameter warning
    mcp_list_iterator_t it = { NULL };
    return it;
}

bool mcp_list_iterator_is_valid(const mcp_list_iterator_t* it) {
    return it && it->node != NULL;
}

void mcp_list_iterator_next(mcp_list_iterator_t* it) {
    if (it && it->node) {
        it->node = it->node->next;
    }
}

void* mcp_list_iterator_get_data(const mcp_list_iterator_t* it) {
    return (it && it->node) ? it->node->data : NULL;
}

// Find functionality
mcp_list_node_t* mcp_list_find(const mcp_list_t* list, const void* data, mcp_compare_func_t compare) {
    if (!list || !compare) return NULL;

    // Lock
    if (list->thread_safety == MCP_LIST_THREAD_SAFE && list->mutex) {
        mcp_mutex_lock(list->mutex);
    }

    mcp_list_node_t* result = NULL;
    for (mcp_list_node_t* node = list->head; node != NULL; node = node->next) {
        if (compare(node->data, data) == 0) {
            result = node;
            break;
        }
    }

    // Unlock
    if (list->thread_safety == MCP_LIST_THREAD_SAFE && list->mutex) {
        mcp_mutex_unlock(list->mutex);
    }

    return result;
}

// Insert node at specified position
mcp_list_node_t* mcp_list_insert_after(mcp_list_t* list, mcp_list_node_t* pos, void* data) {
    if (!list) return NULL;

    // If position is NULL, insert at the front
    if (!pos) {
        return mcp_list_push_front(list, data);
    }

    // Lock
    MCP_LIST_LOCK(list);

    // If position is the tail node, insert at the back
    if (pos == list->tail) {
        // Unlock
        MCP_LIST_UNLOCK(list);
        return mcp_list_push_back(list, data);
    }

    // Allocate new node
    mcp_list_node_t* node = mcp_list_node_alloc();
    if (!node) {
        mcp_log_error("Failed to allocate list node");
        // Unlock
        MCP_LIST_UNLOCK(list);
        return NULL;
    }

    // Initialize new node
    node->data = data;
    node->prev = pos;
    node->next = pos->next;

    // Update adjacent nodes
    if (pos->next) {
        pos->next->prev = node;
    }
    pos->next = node;

    // Update list size
    list->size++;

    // Unlock
    MCP_LIST_UNLOCK(list);

    return node;
}
