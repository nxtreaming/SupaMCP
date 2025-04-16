/**
 * @file mcp_list.c
 * @brief Implementation of generic doubly linked list
 */

#include "mcp_list.h"
#include <stdlib.h>
#include <assert.h>

mcp_list_t* mcp_list_create(void) {
    mcp_list_t* list = (mcp_list_t*)malloc(sizeof(mcp_list_t));
    if (list) {
        list->head = NULL;
        list->tail = NULL;
        list->size = 0;
    }
    return list;
}

void mcp_list_destroy(mcp_list_t* list, void (*free_data)(void*)) {
    if (!list) return;
    
    mcp_list_clear(list, free_data);
    free(list);
}

mcp_list_node_t* mcp_list_push_front(mcp_list_t* list, void* data) {
    if (!list) return NULL;
    
    mcp_list_node_t* node = (mcp_list_node_t*)malloc(sizeof(mcp_list_node_t));
    if (!node) return NULL;
    
    node->data = data;
    node->prev = NULL;
    node->next = list->head;
    
    if (list->head) {
        list->head->prev = node;
    } else {
        // List was empty, so this is also the tail
        list->tail = node;
    }
    
    list->head = node;
    list->size++;
    
    return node;
}

mcp_list_node_t* mcp_list_push_back(mcp_list_t* list, void* data) {
    if (!list) return NULL;
    
    mcp_list_node_t* node = (mcp_list_node_t*)malloc(sizeof(mcp_list_node_t));
    if (!node) return NULL;
    
    node->data = data;
    node->next = NULL;
    node->prev = list->tail;
    
    if (list->tail) {
        list->tail->next = node;
    } else {
        // List was empty, so this is also the head
        list->head = node;
    }
    
    list->tail = node;
    list->size++;
    
    return node;
}

void* mcp_list_remove(mcp_list_t* list, mcp_list_node_t* node, void (*free_data)(void*)) {
    if (!list || !node) return NULL;
    
    void* data = node->data;
    
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
    
    // Free data if requested
    if (free_data && data) {
        free_data(data);
        data = NULL;
    }
    
    free(node);
    list->size--;
    
    return data;
}

void* mcp_list_pop_front(mcp_list_t* list) {
    if (!list || !list->head) return NULL;
    
    mcp_list_node_t* node = list->head;
    void* data = node->data;
    
    list->head = node->next;
    if (list->head) {
        list->head->prev = NULL;
    } else {
        // List is now empty
        list->tail = NULL;
    }
    
    free(node);
    list->size--;
    
    return data;
}

void* mcp_list_pop_back(mcp_list_t* list) {
    if (!list || !list->tail) return NULL;
    
    mcp_list_node_t* node = list->tail;
    void* data = node->data;
    
    list->tail = node->prev;
    if (list->tail) {
        list->tail->next = NULL;
    } else {
        // List is now empty
        list->head = NULL;
    }
    
    free(node);
    list->size--;
    
    return data;
}

void mcp_list_move_to_front(mcp_list_t* list, mcp_list_node_t* node) {
    if (!list || !node || node == list->head) return;
    
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
}

size_t mcp_list_size(const mcp_list_t* list) {
    return list ? list->size : 0;
}

bool mcp_list_is_empty(const mcp_list_t* list) {
    return list ? list->size == 0 : true;
}

void mcp_list_clear(mcp_list_t* list, void (*free_data)(void*)) {
    if (!list) return;
    
    mcp_list_node_t* current = list->head;
    while (current) {
        mcp_list_node_t* next = current->next;
        
        if (free_data && current->data) {
            free_data(current->data);
        }
        
        free(current);
        current = next;
    }
    
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}
