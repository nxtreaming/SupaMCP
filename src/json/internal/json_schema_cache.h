#ifndef JSON_SCHEMA_CACHE_H
#define JSON_SCHEMA_CACHE_H

#include "mcp_hashtable.h"
#include "mcp_rwlock.h"
#include "mcp_list.h"
#include <time.h>
#include <stdio.h>

/**
 * @brief Compiled JSON Schema structure.
 */
typedef struct mcp_compiled_schema {
    char* schema_id;             /**< Schema identifier (hash of schema string) */
    char* schema_str;            /**< Original schema string */
    void* compiled_schema;       /**< Compiled schema representation (implementation-specific) */
    time_t compilation_time;     /**< Time when the schema was compiled */
    size_t use_count;            /**< Number of times this schema has been used */
    struct mcp_compiled_schema* next; /**< Next schema in LRU list */
    struct mcp_compiled_schema* prev; /**< Previous schema in LRU list */
} mcp_compiled_schema_t;

/**
 * @brief JSON Schema cache structure.
 */
typedef struct mcp_json_schema_cache {
    mcp_hashtable_t* schema_cache;     /**< Hash table for storing compiled schemas */
    mcp_rwlock_t* cache_lock;          /**< Read-write lock for thread safety */
    mcp_list_t* lru_list;              /**< LRU list for cache eviction */
    size_t capacity;                   /**< Maximum number of schemas in the cache */
    size_t size;                       /**< Current number of schemas in the cache */
    size_t hits;                       /**< Number of cache hits */
    size_t misses;                     /**< Number of cache misses */
} mcp_json_schema_cache_t;

/**
 * @brief Create a new JSON Schema cache.
 *
 * @param capacity Maximum number of schemas to store in the cache (0 for unlimited).
 * @return Pointer to the created cache, or NULL on failure.
 */
mcp_json_schema_cache_t* mcp_json_schema_cache_create(size_t capacity);

/**
 * @brief Destroy a JSON Schema cache.
 *
 * @param cache The cache to destroy.
 */
void mcp_json_schema_cache_destroy(mcp_json_schema_cache_t* cache);

/**
 * @brief Add a schema to the cache.
 *
 * @param cache The cache to add the schema to.
 * @param schema_str The schema string.
 * @return Pointer to the compiled schema, or NULL on failure.
 */
mcp_compiled_schema_t* mcp_json_schema_cache_add(mcp_json_schema_cache_t* cache, const char* schema_str);

/**
 * @brief Find a schema in the cache.
 *
 * @param cache The cache to search.
 * @param schema_str The schema string to find.
 * @return Pointer to the compiled schema, or NULL if not found.
 */
mcp_compiled_schema_t* mcp_json_schema_cache_find(mcp_json_schema_cache_t* cache, const char* schema_str);

/**
 * @brief Remove a schema from the cache.
 *
 * @param cache The cache to remove the schema from.
 * @param schema_id The schema identifier.
 * @return 0 on success, -1 on failure.
 */
int mcp_json_schema_cache_remove(mcp_json_schema_cache_t* cache, const char* schema_id);

/**
 * @brief Clear all schemas from the cache.
 *
 * @param cache The cache to clear.
 */
void mcp_json_schema_cache_clear(mcp_json_schema_cache_t* cache);

/**
 * @brief Get cache statistics.
 *
 * @param cache The cache to get statistics for.
 * @param size Pointer to store the current cache size.
 * @param capacity Pointer to store the cache capacity.
 * @param hits Pointer to store the number of cache hits.
 * @param misses Pointer to store the number of cache misses.
 * @return 0 on success, -1 on failure.
 */
int mcp_json_schema_cache_get_stats(mcp_json_schema_cache_t* cache, size_t* size, size_t* capacity, size_t* hits, size_t* misses);

/**
 * @brief Validate JSON against a schema, using the cache.
 *
 * @param cache The schema cache.
 * @param json_str The JSON string to validate.
 * @param schema_str The schema string.
 * @return 0 for validation success, -1 for validation failure or error.
 */
int mcp_json_schema_validate(mcp_json_schema_cache_t* cache, const char* json_str, const char* schema_str);

#endif /* JSON_SCHEMA_CACHE_H */
