/**
 * @file mcp_list.h
 * @brief Generic doubly linked list implementation
 */

#ifndef MCP_LIST_H
#define MCP_LIST_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Comparison function type definition
 *
 * @param a First object to compare
 * @param b Second object to compare
 * @return 0 if equal, negative if a < b, positive if a > b
 */
typedef int (*mcp_compare_func_t)(const void* a, const void* b);

/**
 * @brief Doubly linked list node structure
 */
typedef struct mcp_list_node {
    struct mcp_list_node* prev;  /**< Pointer to previous node */
    struct mcp_list_node* next;  /**< Pointer to next node */
    void* data;                  /**< Pointer to node data */
} mcp_list_node_t;

/**
 * @brief Thread safety options
 */
typedef enum mcp_list_thread_safety {
    MCP_LIST_NOT_THREAD_SAFE = 0,  /**< Non-thread-safe mode */
    MCP_LIST_THREAD_SAFE           /**< Thread-safe mode */
} mcp_list_thread_safety_t;

/**
 * @brief Doubly linked list structure
 */
typedef struct mcp_list {
    mcp_list_node_t* head;       /**< Pointer to first node */
    mcp_list_node_t* tail;       /**< Pointer to last node */
    size_t size;                 /**< Number of nodes in the list */
    void* mutex;                 /**< Mutex for thread safety (NULL if not thread-safe) */
    mcp_list_thread_safety_t thread_safety; /**< Thread safety mode */
} mcp_list_t;

/**
 * @brief Creates a new empty list
 *
 * @param thread_safety Thread safety option
 * @return Pointer to the new list, or NULL on allocation failure
 */
mcp_list_t* mcp_list_create(mcp_list_thread_safety_t thread_safety);

/**
 * @brief Destroys a list and frees its memory
 *
 * @param list The list to destroy
 * @param free_data Function to free node data, or NULL to not free data
 */
void mcp_list_destroy(mcp_list_t* list, void (*free_data)(void*));

/**
 * @brief Adds a new node to the front of the list
 *
 * @param list The list to add to
 * @param data The data to add
 * @return Pointer to the new node, or NULL on allocation failure
 */
mcp_list_node_t* mcp_list_push_front(mcp_list_t* list, void* data);

/**
 * @brief Adds a new node to the back of the list
 *
 * @param list The list to add to
 * @param data The data to add
 * @return Pointer to the new node, or NULL on allocation failure
 */
mcp_list_node_t* mcp_list_push_back(mcp_list_t* list, void* data);

/**
 * @brief Removes a node from the list
 *
 * @param list The list to remove from
 * @param node The node to remove
 * @param free_data Function to free node data, or NULL to not free data
 * @return The data pointer from the removed node
 */
void* mcp_list_remove(mcp_list_t* list, mcp_list_node_t* node, void (*free_data)(void*));

/**
 * @brief Removes and returns the first node's data from the list
 *
 * @param list The list to remove from
 * @return The data from the first node, or NULL if the list is empty
 */
void* mcp_list_pop_front(mcp_list_t* list);

/**
 * @brief Removes and returns the last node's data from the list
 *
 * @param list The list to remove from
 * @return The data from the last node, or NULL if the list is empty
 */
void* mcp_list_pop_back(mcp_list_t* list);

/**
 * @brief Moves a node to the front of the list
 *
 * @param list The list to modify
 * @param node The node to move
 */
void mcp_list_move_to_front(mcp_list_t* list, mcp_list_node_t* node);

/**
 * @brief Returns the size of the list
 *
 * @param list The list to get the size of
 * @return The number of nodes in the list
 */
size_t mcp_list_size(const mcp_list_t* list);

/**
 * @brief Checks if the list is empty
 *
 * @param list The list to check
 * @return true if the list is empty, false otherwise
 */
bool mcp_list_is_empty(const mcp_list_t* list);

/**
 * @brief Clears all nodes from the list
 *
 * @param list The list to clear
 * @param free_data Function to free node data, or NULL to not free data
 */
void mcp_list_clear(mcp_list_t* list, void (*free_data)(void*));

/**
 * @brief List iterator structure
 */
typedef struct mcp_list_iterator {
    mcp_list_node_t* node;  /**< Pointer to current node */
} mcp_list_iterator_t;

/**
 * @brief Get an iterator to the beginning of the list
 *
 * @param list The list to iterate
 * @return Iterator pointing to the first node of the list
 */
mcp_list_iterator_t mcp_list_iterator_begin(const mcp_list_t* list);

/**
 * @brief Get an iterator to the end of the list
 *
 * @param list The list to iterate
 * @return Iterator representing the end of the list (invalid iterator)
 */
mcp_list_iterator_t mcp_list_iterator_end(const mcp_list_t* list);

/**
 * @brief Check if an iterator is valid
 *
 * @param it The iterator to check
 * @return true if the iterator is valid, false otherwise
 */
bool mcp_list_iterator_is_valid(const mcp_list_iterator_t* it);

/**
 * @brief Move the iterator to the next node
 *
 * @param it The iterator to move
 */
void mcp_list_iterator_next(mcp_list_iterator_t* it);

/**
 * @brief Get the data pointed to by the iterator
 *
 * @param it The iterator
 * @return The node data, or NULL if the iterator is invalid
 */
void* mcp_list_iterator_get_data(const mcp_list_iterator_t* it);

/**
 * @brief Find data in the list
 *
 * @param list The list to search
 * @param data The data to find
 * @param compare The comparison function
 * @return The node containing matching data, or NULL if not found
 */
mcp_list_node_t* mcp_list_find(const mcp_list_t* list, const void* data, mcp_compare_func_t compare);

/**
 * @brief Insert a new node after the specified position
 *
 * @param list The list to insert into
 * @param pos The position to insert after (if NULL, insert at the front)
 * @param data The data to insert
 * @return The newly created node, or NULL on allocation failure
 */
mcp_list_node_t* mcp_list_insert_after(mcp_list_t* list, mcp_list_node_t* pos, void* data);

#ifdef __cplusplus
}
#endif

#endif /* MCP_LIST_H */
