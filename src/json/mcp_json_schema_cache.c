#include "internal/mcp_json_schema_cache.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Default cache capacity if not specified
#define DEFAULT_SCHEMA_CACHE_CAPACITY 100

// Helper function to calculate a simple hash for a string
static unsigned int hash_string(const char* str) {
    unsigned int hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }

    return hash;
}

// Helper function to create a schema ID from a schema string
static char* create_schema_id(const char* schema_str) {
    unsigned int hash = hash_string(schema_str);
    char id[32]; // Large enough for a hash value
    snprintf(id, sizeof(id), "schema_%u", hash);
    return mcp_strdup(id);
}

// Helper function to compile a schema
static void* compile_schema(const char* schema_str) {
    // In a real implementation, this would use a JSON Schema library
    // For now, we'll just parse the schema as JSON to validate its syntax
    mcp_json_t* schema_json = mcp_json_parse(schema_str);
    if (schema_json == NULL) {
        mcp_log_error("Failed to parse schema JSON");
        return NULL;
    }

    // In a real implementation, we would compile the schema here
    // For now, we'll just return the parsed JSON as the "compiled" schema
    return schema_json;
}

// Helper function to free a compiled schema
static void free_compiled_schema(void* compiled_schema) {
    if (compiled_schema) {
        // In a real implementation, this would use the JSON Schema library's cleanup function
        // For now, we'll just destroy the JSON object
        mcp_json_destroy((mcp_json_t*)compiled_schema);
    }
}

// Helper function to create a compiled schema structure
static mcp_compiled_schema_t* create_compiled_schema(const char* schema_str) {
    if (!schema_str) {
        return NULL;
    }

    mcp_compiled_schema_t* schema = (mcp_compiled_schema_t*)malloc(sizeof(mcp_compiled_schema_t));
    if (!schema) {
        mcp_log_error("Failed to allocate memory for compiled schema");
        return NULL;
    }

    schema->schema_id = create_schema_id(schema_str);
    if (!schema->schema_id) {
        mcp_log_error("Failed to create schema ID");
        free(schema);
        return NULL;
    }

    schema->schema_str = mcp_strdup(schema_str);
    if (!schema->schema_str) {
        mcp_log_error("Failed to duplicate schema string");
        free(schema->schema_id);
        free(schema);
        return NULL;
    }

    schema->compiled_schema = compile_schema(schema_str);
    if (!schema->compiled_schema) {
        mcp_log_error("Failed to compile schema");
        free(schema->schema_str);
        free(schema->schema_id);
        free(schema);
        return NULL;
    }

    schema->compilation_time = time(NULL);
    schema->use_count = 0;
    schema->next = NULL;
    schema->prev = NULL;

    return schema;
}

// Helper function to free a compiled schema structure
static void free_compiled_schema_struct(mcp_compiled_schema_t* schema) {
    if (schema) {
        free(schema->schema_id);
        free(schema->schema_str);
        free_compiled_schema(schema->compiled_schema);
        free(schema);
    }
}

// Adapter function for mcp_list_destroy to convert from void* to mcp_compiled_schema_t*
static void free_compiled_schema_void_adapter(void* data) {
    if (data) {
        free_compiled_schema_struct((mcp_compiled_schema_t*)data);
    }
}

// Helper function for freeing schemas in hashtable_foreach
static void free_schema_callback(const void* key, void* value, void* user_data) {
    (void)key; // Unused
    (void)user_data; // Unused
    if (value) {
        free_compiled_schema_struct((mcp_compiled_schema_t*)value);
    }
}

// Helper function to validate JSON against a compiled schema
static int validate_with_compiled_schema(void* compiled_schema, const char* json_str) {
    (void)compiled_schema; // Unused
    // In a real implementation, this would use the JSON Schema library's validation function
    // For now, we'll just check if the JSON is valid
    mcp_json_t* json = mcp_json_parse(json_str);
    if (json == NULL) {
        mcp_log_error("Failed to parse JSON for validation");
        return -1;
    }

    // In a real implementation, we would validate the JSON against the compiled schema here
    // For now, we'll just assume it's valid if we can parse it
    mcp_json_destroy(json);

    return 0; // Success
}

// Create a new JSON Schema cache
mcp_json_schema_cache_t* mcp_json_schema_cache_create(size_t capacity) {
    mcp_json_schema_cache_t* cache = (mcp_json_schema_cache_t*)malloc(sizeof(mcp_json_schema_cache_t));
    if (!cache) {
        mcp_log_error("Failed to allocate memory for schema cache");
        return NULL;
    }

    // Use default capacity if not specified
    if (capacity == 0) {
        capacity = DEFAULT_SCHEMA_CACHE_CAPACITY;
    }

    // Create hash table for storing compiled schemas
    cache->schema_cache = mcp_hashtable_create(
        capacity,
        0.75f,
        mcp_hashtable_string_hash,
        mcp_hashtable_string_compare,
        mcp_hashtable_string_dup,
        mcp_hashtable_string_free,
        free  // Use standard free for value (compiled schema)
    );
    if (!cache->schema_cache) {
        mcp_log_error("Failed to create schema hash table");
        free(cache);
        return NULL;
    }

    // Create read-write lock for thread safety
    cache->cache_lock = mcp_rwlock_create();
    if (!cache->cache_lock) {
        mcp_log_error("Failed to create schema cache lock");
        mcp_hashtable_destroy(cache->schema_cache);
        free(cache);
        return NULL;
    }

    // Create LRU list for cache eviction
    cache->lru_list = mcp_list_create(MCP_LIST_NOT_THREAD_SAFE);
    if (!cache->lru_list) {
        mcp_log_error("Failed to create LRU list");
        mcp_rwlock_destroy(cache->cache_lock);
        mcp_hashtable_destroy(cache->schema_cache);
        free(cache);
        return NULL;
    }

    cache->capacity = capacity;
    cache->size = 0;
    cache->hits = 0;
    cache->misses = 0;

    mcp_log_info("Created JSON Schema cache with capacity %zu", capacity);

    return cache;
}

// Destroy a JSON Schema cache
void mcp_json_schema_cache_destroy(mcp_json_schema_cache_t* cache) {
    if (!cache) {
        return;
    }

    // Clear all schemas from the cache
    mcp_json_schema_cache_clear(cache);

    // Destroy the LRU list
    mcp_list_destroy(cache->lru_list, free_compiled_schema_void_adapter);

    // Destroy the read-write lock
    mcp_rwlock_destroy(cache->cache_lock);

    // Destroy the hash table
    mcp_hashtable_destroy(cache->schema_cache);

    // Free the cache structure
    free(cache);

    mcp_log_info("Destroyed JSON Schema cache");
}

// Add a schema to the cache
mcp_compiled_schema_t* mcp_json_schema_cache_add(mcp_json_schema_cache_t* cache, const char* schema_str) {
    if (!cache || !schema_str) {
        return NULL;
    }

    // Create a compiled schema
    mcp_compiled_schema_t* schema = create_compiled_schema(schema_str);
    if (!schema) {
        return NULL;
    }

    // Acquire write lock
    mcp_rwlock_write_lock(cache->cache_lock);

    // Check if we need to evict an entry
    if (cache->size >= cache->capacity && cache->capacity > 0) {
        // Get the least recently used schema
        mcp_compiled_schema_t* lru_schema = NULL;
        if (cache->lru_list->tail) {
            lru_schema = (mcp_compiled_schema_t*)cache->lru_list->tail->data;
        }
        if (lru_schema) {
            // Remove from hash table
            mcp_hashtable_remove(cache->schema_cache, lru_schema->schema_id);

            // Remove from LRU list
            mcp_list_node_t* node = cache->lru_list->tail;
            mcp_list_remove(cache->lru_list, node, NULL);

            // Free the schema
            free_compiled_schema_struct(lru_schema);

            cache->size--;

            mcp_log_debug("Evicted LRU schema from cache");
        }
    }

    // Add to hash table
    if (mcp_hashtable_put(cache->schema_cache, schema->schema_id, schema) != 0) {
        mcp_log_error("Failed to insert schema into hash table");
        free_compiled_schema_struct(schema);
        mcp_rwlock_write_unlock(cache->cache_lock);
        return NULL;
    }

    // Add to front of LRU list
    mcp_list_push_front(cache->lru_list, schema);

    cache->size++;

    mcp_rwlock_write_unlock(cache->cache_lock);

    mcp_log_debug("Added schema to cache: %s", schema->schema_id);

    return schema;
}

// Find a schema in the cache
mcp_compiled_schema_t* mcp_json_schema_cache_find(mcp_json_schema_cache_t* cache, const char* schema_str) {
    if (!cache || !schema_str) {
        return NULL;
    }

    // Create a temporary schema ID
    char* schema_id = create_schema_id(schema_str);
    if (!schema_id) {
        return NULL;
    }

    // Acquire read lock
    mcp_rwlock_read_lock(cache->cache_lock);

    // Look up in hash table
    void* value = NULL;
    if (mcp_hashtable_get(cache->schema_cache, schema_id, &value) == 0) {
        mcp_compiled_schema_t* schema = (mcp_compiled_schema_t*)value;

        if (schema) {
            // Cache hit
            cache->hits++;

            // Upgrade to write lock to update LRU list
            mcp_rwlock_read_unlock(cache->cache_lock);
            mcp_rwlock_write_lock(cache->cache_lock);

            // Move to front of LRU list
            // Find the node containing this schema
            mcp_list_node_t* node = cache->lru_list->head;
            while (node) {
                if (node->data == schema) {
                    break;
                }
                node = node->next;
            }
            if (node) {
                mcp_list_move_to_front(cache->lru_list, node);
            }

            // Increment use count
            schema->use_count++;

            mcp_rwlock_write_unlock(cache->cache_lock);

            mcp_log_debug("Schema cache hit: %s", schema_id);
            return schema;
        }
    } else {
        // Cache miss
        cache->misses++;

        mcp_rwlock_read_unlock(cache->cache_lock);

        mcp_log_debug("Schema cache miss: %s", schema_id);
    }

    free(schema_id);

    return NULL; // Return NULL for cache miss
}

// Remove a schema from the cache
int mcp_json_schema_cache_remove(mcp_json_schema_cache_t* cache, const char* schema_id) {
    if (!cache || !schema_id) {
        return -1;
    }

    // Acquire write lock
    mcp_rwlock_write_lock(cache->cache_lock);

    // Look up in hash table
    void* value = NULL;
    if (mcp_hashtable_get(cache->schema_cache, schema_id, &value) != 0 || !value) {
        mcp_rwlock_write_unlock(cache->cache_lock);
        return -1;
    }
    mcp_compiled_schema_t* schema = (mcp_compiled_schema_t*)value;

    // Remove from hash table
    mcp_hashtable_remove(cache->schema_cache, schema_id);

    // Remove from LRU list
    // Find the node containing this schema
    mcp_list_node_t* node = cache->lru_list->head;
    while (node) {
        if (node->data == schema) {
            break;
        }
        node = node->next;
    }
    if (node) {
        mcp_list_remove(cache->lru_list, node, NULL);
    }

    // Free the schema
    free_compiled_schema_struct(schema);

    cache->size--;

    mcp_rwlock_write_unlock(cache->cache_lock);

    mcp_log_debug("Removed schema from cache: %s", schema_id);

    return 0;
}

// Clear all schemas from the cache
void mcp_json_schema_cache_clear(mcp_json_schema_cache_t* cache) {
    if (!cache) {
        return;
    }

    // Acquire write lock
    mcp_rwlock_write_lock(cache->cache_lock);

    // Use hashtable_foreach instead of iterator
    mcp_hashtable_foreach(cache->schema_cache,
        free_schema_callback,
        NULL);

    // Clear the hash table
    mcp_hashtable_clear(cache->schema_cache);

    // Clear the LRU list
    mcp_list_clear(cache->lru_list, NULL); // Data already freed by hashtable_foreach

    cache->size = 0;

    mcp_rwlock_write_unlock(cache->cache_lock);

    mcp_log_info("Cleared JSON Schema cache");
}

// Get cache statistics
int mcp_json_schema_cache_get_stats(mcp_json_schema_cache_t* cache, size_t* size, size_t* capacity, size_t* hits, size_t* misses) {
    if (!cache) {
        return -1;
    }

    // Acquire read lock
    mcp_rwlock_read_lock(cache->cache_lock);

    if (size) {
        *size = cache->size;
    }

    if (capacity) {
        *capacity = cache->capacity;
    }

    if (hits) {
        *hits = cache->hits;
    }

    if (misses) {
        *misses = cache->misses;
    }

    mcp_rwlock_read_unlock(cache->cache_lock);

    return 0;
}

// Validate JSON against a schema, using the cache
int mcp_json_schema_validate(mcp_json_schema_cache_t* cache, const char* json_str, const char* schema_str) {
    if (!cache || !json_str || !schema_str) {
        return -1;
    }

    // Try to find the schema in the cache
    mcp_compiled_schema_t* schema = mcp_json_schema_cache_find(cache, schema_str);
    if (!schema) {
        // Schema not found in cache, add it
        schema = mcp_json_schema_cache_add(cache, schema_str);
        if (!schema) {
            mcp_log_error("Failed to add schema to cache");
            return -1;
        }
    }

    // Validate JSON against the compiled schema
    return validate_with_compiled_schema(schema->compiled_schema, json_str);
}
