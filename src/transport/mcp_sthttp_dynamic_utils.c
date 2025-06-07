#include "internal/sthttp_transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libwebsockets.h>

// Forward declarations
int send_sse_event(struct lws* wsi, const char* event_id, const char* event_type, const char* data);
int send_sse_heartbeat_to_wsi(struct lws* wsi);

/**
 * @brief Hash function for event IDs
 */
static uint32_t hash_event_id(const char* event_id) {
    if (event_id == NULL) {
        return 0;
    }
    
    uint32_t hash = 5381;
    int c;
    while ((c = *event_id++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

/**
 * @brief Create dynamic SSE clients array
 */
dynamic_sse_clients_t* dynamic_sse_clients_create(size_t initial_capacity) {
    if (initial_capacity == 0) {
        initial_capacity = STHTTP_INITIAL_SSE_CLIENTS;
    }
    
    dynamic_sse_clients_t* clients = (dynamic_sse_clients_t*)calloc(1, sizeof(dynamic_sse_clients_t));
    if (clients == NULL) {
        mcp_log_error("Failed to allocate dynamic SSE clients structure");
        return NULL;
    }
    
    clients->clients = (struct lws**)calloc(initial_capacity, sizeof(struct lws*));
    if (clients->clients == NULL) {
        mcp_log_error("Failed to allocate SSE clients array for %zu clients", initial_capacity);
        free(clients);
        return NULL;
    }
    
    clients->capacity = initial_capacity;
    clients->count = 0;
    
    clients->mutex = mcp_mutex_create();
    if (clients->mutex == NULL) {
        mcp_log_error("Failed to create mutex for dynamic SSE clients");
        free(clients->clients);
        free(clients);
        return NULL;
    }
    
    mcp_log_debug("Created dynamic SSE clients array with initial capacity %zu", initial_capacity);
    return clients;
}

/**
 * @brief Destroy dynamic SSE clients array
 */
void dynamic_sse_clients_destroy(dynamic_sse_clients_t* clients) {
    if (clients == NULL) {
        return;
    }
    
    if (clients->mutex) {
        mcp_mutex_destroy(clients->mutex);
    }
    
    free(clients->clients);
    free(clients);
}

/**
 * @brief Grow the clients array when needed
 */
static int grow_clients_array(dynamic_sse_clients_t* clients) {
    size_t new_capacity = clients->capacity * STHTTP_SSE_GROWTH_FACTOR;
    struct lws** new_clients = (struct lws**)realloc(clients->clients, 
                                                     new_capacity * sizeof(struct lws*));
    if (new_clients == NULL) {
        mcp_log_error("Failed to grow SSE clients array from %zu to %zu", 
                     clients->capacity, new_capacity);
        return -1;
    }
    
    // Initialize new slots to NULL
    for (size_t i = clients->capacity; i < new_capacity; i++) {
        new_clients[i] = NULL;
    }
    
    clients->clients = new_clients;
    clients->capacity = new_capacity;
    
    mcp_log_debug("Grew SSE clients array from %zu to %zu capacity", 
                 clients->capacity / STHTTP_SSE_GROWTH_FACTOR, new_capacity);
    return 0;
}

/**
 * @brief Add client to dynamic array
 */
int dynamic_sse_clients_add(dynamic_sse_clients_t* clients, struct lws* wsi) {
    if (clients == NULL || wsi == NULL) {
        return -1;
    }
    
    mcp_mutex_lock(clients->mutex);
    
    // Find first available slot
    size_t slot = SIZE_MAX;
    for (size_t i = 0; i < clients->capacity; i++) {
        if (clients->clients[i] == NULL) {
            slot = i;
            break;
        }
    }
    
    // If no slot available, grow the array
    if (slot == SIZE_MAX) {
        if (grow_clients_array(clients) != 0) {
            mcp_mutex_unlock(clients->mutex);
            return -1;
        }
        slot = clients->count; // First slot in the new space
    }
    
    clients->clients[slot] = wsi;
    clients->count++;
    
    mcp_mutex_unlock(clients->mutex);
    
    mcp_log_debug("Added SSE client to slot %zu (total: %zu/%zu)", 
                 slot, clients->count, clients->capacity);
    return 0;
}

/**
 * @brief Remove client from dynamic array
 */
int dynamic_sse_clients_remove(dynamic_sse_clients_t* clients, struct lws* wsi) {
    if (clients == NULL || wsi == NULL) {
        return -1;
    }
    
    mcp_mutex_lock(clients->mutex);
    
    for (size_t i = 0; i < clients->capacity; i++) {
        if (clients->clients[i] == wsi) {
            clients->clients[i] = NULL;
            clients->count--;
            mcp_mutex_unlock(clients->mutex);
            
            mcp_log_debug("Removed SSE client from slot %zu (remaining: %zu/%zu)", 
                         i, clients->count, clients->capacity);
            return 0;
        }
    }
    
    mcp_mutex_unlock(clients->mutex);
    return -1; // Client not found
}

/**
 * @brief Get client count
 */
size_t dynamic_sse_clients_count(dynamic_sse_clients_t* clients) {
    if (clients == NULL) {
        return 0;
    }
    
    mcp_mutex_lock(clients->mutex);
    size_t count = clients->count;
    mcp_mutex_unlock(clients->mutex);
    
    return count;
}

/**
 * @brief Cleanup disconnected clients
 */
size_t dynamic_sse_clients_cleanup(dynamic_sse_clients_t* clients) {
    if (clients == NULL) {
        return 0;
    }
    
    mcp_mutex_lock(clients->mutex);
    
    size_t cleaned_count = 0;
    size_t active_count = 0;
    
    for (size_t i = 0; i < clients->capacity; i++) {
        if (clients->clients[i] != NULL) {
            struct lws* wsi = clients->clients[i];
            // Check if client is still connected by validating socket
            if (lws_get_socket_fd(wsi) >= 0) {
                active_count++;
            } else {
                // Client is disconnected, remove it
                clients->clients[i] = NULL;
                cleaned_count++;
            }
        }
    }
    
    clients->count = active_count;
    
    mcp_mutex_unlock(clients->mutex);
    
    if (cleaned_count > 0) {
        mcp_log_debug("Cleaned up %zu disconnected SSE clients (active: %zu)", 
                     cleaned_count, active_count);
    }
    
    return cleaned_count;
}

/**
 * @brief Send message to all connected clients
 */
int dynamic_sse_clients_broadcast(dynamic_sse_clients_t* clients, 
                                 const char* event_id, const char* event_type, const char* data) {
    if (clients == NULL || data == NULL) {
        return -1;
    }
    
    mcp_mutex_lock(clients->mutex);
    
    int sent_count = 0;
    for (size_t i = 0; i < clients->capacity; i++) {
        if (clients->clients[i] != NULL) {
            struct lws* wsi = clients->clients[i];
            // Check if the WSI is still valid before sending
            if (lws_get_socket_fd(wsi) >= 0) {
                if (send_sse_event(wsi, event_id, event_type, data) == 0) {
                    sent_count++;
                }
            }
        }
    }
    
    mcp_mutex_unlock(clients->mutex);
    
    return sent_count;
}

/**
 * @brief Send heartbeat to all connected clients
 */
int dynamic_sse_clients_broadcast_heartbeat(dynamic_sse_clients_t* clients) {
    if (clients == NULL) {
        return -1;
    }

    mcp_mutex_lock(clients->mutex);

    int sent_count = 0;
    for (size_t i = 0; i < clients->capacity; i++) {
        if (clients->clients[i] != NULL) {
            struct lws* wsi = clients->clients[i];
            // Check if the WSI is still valid before sending
            if (lws_get_socket_fd(wsi) >= 0) {
                if (send_sse_heartbeat_to_wsi(wsi) == 0) {
                    sent_count++;
                }
            }
        }
    }

    mcp_mutex_unlock(clients->mutex);

    return sent_count;
}

/**
 * @brief Create event hash map
 */
event_hash_map_t* event_hash_map_create(size_t initial_size) {
    if (initial_size == 0) {
        initial_size = STHTTP_EVENT_HASH_INITIAL_SIZE;
    }

    event_hash_map_t* map = (event_hash_map_t*)calloc(1, sizeof(event_hash_map_t));
    if (map == NULL) {
        mcp_log_error("Failed to allocate event hash map structure");
        return NULL;
    }

    map->buckets = (event_hash_entry_t**)calloc(initial_size, sizeof(event_hash_entry_t*));
    if (map->buckets == NULL) {
        mcp_log_error("Failed to allocate hash map buckets for %zu entries", initial_size);
        free(map);
        return NULL;
    }

    map->bucket_count = initial_size;
    map->entry_count = 0;

    map->mutex = mcp_mutex_create();
    if (map->mutex == NULL) {
        mcp_log_error("Failed to create mutex for event hash map");
        free(map->buckets);
        free(map);
        return NULL;
    }

    mcp_log_debug("Created event hash map with %zu buckets", initial_size);
    return map;
}

/**
 * @brief Destroy event hash map
 */
void event_hash_map_destroy(event_hash_map_t* map) {
    if (map == NULL) {
        return;
    }

    if (map->buckets) {
        for (size_t i = 0; i < map->bucket_count; i++) {
            event_hash_entry_t* entry = map->buckets[i];
            while (entry) {
                event_hash_entry_t* next = entry->next;
                free(entry->event_id);
                free(entry);
                entry = next;
            }
        }
        free(map->buckets);
    }

    if (map->mutex) {
        mcp_mutex_destroy(map->mutex);
    }

    free(map);
}

/**
 * @brief Add event to hash map
 */
int event_hash_map_put(event_hash_map_t* map, const char* event_id, size_t position) {
    if (map == NULL || event_id == NULL) {
        return -1;
    }

    mcp_mutex_lock(map->mutex);

    uint32_t hash = hash_event_id(event_id);
    size_t bucket = hash % map->bucket_count;

    // Check if entry already exists
    event_hash_entry_t* entry = map->buckets[bucket];
    while (entry) {
        if (strcmp(entry->event_id, event_id) == 0) {
            // Update existing entry
            entry->position = position;
            mcp_mutex_unlock(map->mutex);
            return 0;
        }
        entry = entry->next;
    }

    // Create new entry
    entry = (event_hash_entry_t*)malloc(sizeof(event_hash_entry_t));
    if (entry == NULL) {
        mcp_log_error("Failed to allocate hash map entry");
        mcp_mutex_unlock(map->mutex);
        return -1;
    }

    entry->event_id = mcp_strdup(event_id);
    if (entry->event_id == NULL) {
        mcp_log_error("Failed to duplicate event ID");
        free(entry);
        mcp_mutex_unlock(map->mutex);
        return -1;
    }

    entry->position = position;
    entry->next = map->buckets[bucket];
    map->buckets[bucket] = entry;
    map->entry_count++;

    mcp_mutex_unlock(map->mutex);

    mcp_log_debug("Added event ID '%s' at position %zu to hash map", event_id, position);
    return 0;
}

/**
 * @brief Find event position in hash map
 */
int event_hash_map_get(event_hash_map_t* map, const char* event_id, size_t* position) {
    if (map == NULL || event_id == NULL || position == NULL) {
        return -1;
    }

    mcp_mutex_lock(map->mutex);

    uint32_t hash = hash_event_id(event_id);
    size_t bucket = hash % map->bucket_count;

    event_hash_entry_t* entry = map->buckets[bucket];
    while (entry) {
        if (strcmp(entry->event_id, event_id) == 0) {
            *position = entry->position;
            mcp_mutex_unlock(map->mutex);
            return 0; // Found
        }
        entry = entry->next;
    }

    mcp_mutex_unlock(map->mutex);
    return -1; // Not found
}

/**
 * @brief Remove event from hash map
 */
int event_hash_map_remove(event_hash_map_t* map, const char* event_id) {
    if (map == NULL || event_id == NULL) {
        return -1;
    }

    mcp_mutex_lock(map->mutex);

    uint32_t hash = hash_event_id(event_id);
    size_t bucket = hash % map->bucket_count;

    event_hash_entry_t* entry = map->buckets[bucket];
    event_hash_entry_t* prev = NULL;

    while (entry) {
        if (strcmp(entry->event_id, event_id) == 0) {
            // Remove entry
            if (prev) {
                prev->next = entry->next;
            } else {
                map->buckets[bucket] = entry->next;
            }

            free(entry->event_id);
            free(entry);
            map->entry_count--;

            mcp_mutex_unlock(map->mutex);
            mcp_log_debug("Removed event ID '%s' from hash map", event_id);
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }

    mcp_mutex_unlock(map->mutex);
    return -1; // Not found
}
