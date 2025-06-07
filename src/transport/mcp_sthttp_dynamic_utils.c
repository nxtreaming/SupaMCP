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

// CORS header cache constants
#define CORS_HEADER_CACHE_SIZE 1024
#define MAX_CORS_HEADER_BLOCKS 8

/**
 * @brief Pre-built CORS header block
 */
typedef struct {
    unsigned char* data;        /**< Pre-built header data */
    size_t length;             /**< Length of header data */
    bool in_use;               /**< Whether this block is currently in use */
    char config_hash[32];      /**< Hash of CORS configuration for cache validation */
} cors_header_block_t;

/**
 * @brief CORS header cache
 */
typedef struct {
    cors_header_block_t blocks[MAX_CORS_HEADER_BLOCKS];
    mcp_mutex_t* mutex;
    size_t next_block;         /**< Round-robin allocation */
} cors_header_cache_t;

// Global CORS header cache
static cors_header_cache_t g_cors_cache = {0};

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

/**
 * @brief Generate configuration hash for CORS settings
 */
static void generate_cors_config_hash(const sthttp_transport_data_t* data, char* hash_out) {
    if (data == NULL || hash_out == NULL) {
        return;
    }

    // Simple hash based on CORS configuration
    uint32_t hash = 5381;

    if (data->cors_allow_origin) {
        const char* str = data->cors_allow_origin;
        int c;
        while ((c = *str++)) {
            hash = ((hash << 5) + hash) + c;
        }
    }

    if (data->cors_allow_methods) {
        const char* str = data->cors_allow_methods;
        int c;
        while ((c = *str++)) {
            hash = ((hash << 5) + hash) + c;
        }
    }

    if (data->cors_allow_headers) {
        const char* str = data->cors_allow_headers;
        int c;
        while ((c = *str++)) {
            hash = ((hash << 5) + hash) + c;
        }
    }

    hash = ((hash << 5) + hash) + data->cors_max_age;

    snprintf(hash_out, 32, "%08x", hash);
}

/**
 * @brief Initialize CORS header cache
 */
int cors_header_cache_init(void) {
    if (g_cors_cache.mutex != NULL) {
        return 0; // Already initialized
    }

    memset(&g_cors_cache, 0, sizeof(cors_header_cache_t));

    g_cors_cache.mutex = mcp_mutex_create();
    if (g_cors_cache.mutex == NULL) {
        mcp_log_error("Failed to create CORS cache mutex");
        return -1;
    }

    mcp_log_debug("CORS header cache initialized");
    return 0;
}

/**
 * @brief Cleanup CORS header cache
 */
void cors_header_cache_cleanup(void) {
    if (g_cors_cache.mutex == NULL) {
        return;
    }

    mcp_mutex_lock(g_cors_cache.mutex);

    for (size_t i = 0; i < MAX_CORS_HEADER_BLOCKS; i++) {
        if (g_cors_cache.blocks[i].data) {
            free(g_cors_cache.blocks[i].data);
            g_cors_cache.blocks[i].data = NULL;
        }
    }

    mcp_mutex_unlock(g_cors_cache.mutex);
    mcp_mutex_destroy(g_cors_cache.mutex);

    memset(&g_cors_cache, 0, sizeof(cors_header_cache_t));
    mcp_log_debug("CORS header cache cleaned up");
}

/**
 * @brief Build CORS headers block
 */
static int build_cors_headers_block(const sthttp_transport_data_t* data,
                                   unsigned char* buffer, size_t buffer_size, size_t* length_out) {
    if (data == NULL || buffer == NULL || length_out == NULL) {
        return -1;
    }

    unsigned char* p = buffer;
    unsigned char* end = buffer + buffer_size;

    // Add CORS headers
    if (data->cors_allow_origin) {
        int result = snprintf((char*)p, end - p, "Access-Control-Allow-Origin: %s\r\n",
                             data->cors_allow_origin);
        if (result < 0 || p + result >= end) {
            return -1;
        }
        p += result;
    }

    if (data->cors_allow_methods) {
        int result = snprintf((char*)p, end - p, "Access-Control-Allow-Methods: %s\r\n",
                             data->cors_allow_methods);
        if (result < 0 || p + result >= end) {
            return -1;
        }
        p += result;
    }

    if (data->cors_allow_headers) {
        int result = snprintf((char*)p, end - p, "Access-Control-Allow-Headers: %s\r\n",
                             data->cors_allow_headers);
        if (result < 0 || p + result >= end) {
            return -1;
        }
        p += result;
    }

    if (data->cors_max_age > 0) {
        int result = snprintf((char*)p, end - p, "Access-Control-Max-Age: %d\r\n",
                             data->cors_max_age);
        if (result < 0 || p + result >= end) {
            return -1;
        }
        p += result;
    }

    *length_out = p - buffer;
    return 0;
}
/**
 * @brief Get or create cached CORS headers
 */
const unsigned char* get_cached_cors_headers(const sthttp_transport_data_t* data, size_t* length_out) {
    if (data == NULL || length_out == NULL || !data->enable_cors) {
        *length_out = 0;
        return NULL;
    }

    if (cors_header_cache_init() != 0) {
        *length_out = 0;
        return NULL;
    }

    char config_hash[32];
    generate_cors_config_hash(data, config_hash);

    mcp_mutex_lock(g_cors_cache.mutex);

    // Look for existing cached block
    for (size_t i = 0; i < MAX_CORS_HEADER_BLOCKS; i++) {
        if (g_cors_cache.blocks[i].data &&
            strcmp(g_cors_cache.blocks[i].config_hash, config_hash) == 0) {
            *length_out = g_cors_cache.blocks[i].length;
            mcp_mutex_unlock(g_cors_cache.mutex);
            return g_cors_cache.blocks[i].data;
        }
    }

    // Find available block or reuse oldest
    size_t block_index = g_cors_cache.next_block;
    cors_header_block_t* block = &g_cors_cache.blocks[block_index];

    // Free existing data if any
    if (block->data) {
        free(block->data);
        block->data = NULL;
    }

    // Allocate new block
    block->data = (unsigned char*)malloc(CORS_HEADER_CACHE_SIZE);
    if (block->data == NULL) {
        mcp_log_error("Failed to allocate CORS header cache block");
        mcp_mutex_unlock(g_cors_cache.mutex);
        *length_out = 0;
        return NULL;
    }

    // Build headers
    if (build_cors_headers_block(data, block->data, CORS_HEADER_CACHE_SIZE, &block->length) != 0) {
        mcp_log_error("Failed to build CORS headers block");
        free(block->data);
        block->data = NULL;
        mcp_mutex_unlock(g_cors_cache.mutex);
        *length_out = 0;
        return NULL;
    }

    // Update block metadata
    strncpy(block->config_hash, config_hash, sizeof(block->config_hash) - 1);
    block->config_hash[sizeof(block->config_hash) - 1] = '\0';
    block->in_use = true;

    // Update next block index (round-robin)
    g_cors_cache.next_block = (g_cors_cache.next_block + 1) % MAX_CORS_HEADER_BLOCKS;

    *length_out = block->length;
    mcp_mutex_unlock(g_cors_cache.mutex);

    mcp_log_debug("Created new CORS header cache block (index %zu, %zu bytes)",
                 block_index, block->length);
    return block->data;
}

/**
 * @brief Add optimized CORS headers to response
 */
int add_optimized_cors_headers(struct lws* wsi, const sthttp_transport_data_t* data,
                              unsigned char** p, unsigned char* end) {
    if (wsi == NULL || data == NULL || !data->enable_cors || p == NULL || *p == NULL || end == NULL) {
        return 0;
    }

    size_t cors_length;
    const unsigned char* cors_headers = get_cached_cors_headers(data, &cors_length);

    if (cors_headers == NULL || cors_length == 0) {
        // Fallback to individual header addition
        add_streamable_cors_headers(wsi, (sthttp_transport_data_t*)data, p, end);
        return 0;
    }

    // Check if we have enough space
    if (*p + cors_length > end) {
        mcp_log_error("Not enough space for CORS headers (%zu bytes needed)", cors_length);
        return -1;
    }

    // Copy cached headers directly
    memcpy(*p, cors_headers, cors_length);
    *p += cors_length;

    return 0;
}
