#ifndef MCP_CACHE_H
#define MCP_CACHE_H

#include <mcp_types.h>
#include <stddef.h>
#include <time.h>

#ifdef _WIN32
// Forward declare HANDLE type without including windows.h in the header
typedef void* HANDLE;
#else
#include <pthread.h>
#endif

// Forward declaration
typedef struct mcp_resource_cache mcp_resource_cache_t;

/**
 * @brief Creates a new resource cache.
 *
 * @param capacity The maximum number of entries the cache can hold.
 * @param default_ttl_seconds The default time-to-live for cache entries in seconds.
 *                            0 means entries do not expire by default.
 * @return A pointer to the newly created cache, or NULL on failure.
 *         The caller is responsible for destroying the cache using mcp_cache_destroy().
 */
mcp_resource_cache_t* mcp_cache_create(size_t capacity, time_t default_ttl_seconds);

/**
 * @brief Destroys the resource cache and frees all associated memory.
 *
 * This function is thread-safe with respect to other cache operations finishing.
 *
 * @param cache The cache instance to destroy.
 */
void mcp_cache_destroy(mcp_resource_cache_t* cache);

/**
 * @brief Retrieves content items for a given URI from the cache.
 *
 * This function is thread-safe.
 *
 * @param cache The cache instance.
 * @param uri The URI of the resource to retrieve.
 * @param[out] content Pointer to a variable that will receive the allocated array
 *                     of mcp_content_item_t pointers if found and valid.
 *                     The caller receives ownership of this array and its items
 *                     (allocated via malloc) and must free them using
 *                     mcp_content_item_free() for items and free() for the array.
 * @param[out] content_count Pointer to a variable that will receive the number of
 *                           items in the `content` array.
 * @return 0 if the item was found in the cache and is still valid (not expired),
 *         -1 otherwise (not found or expired).
 */
int mcp_cache_get(mcp_resource_cache_t* cache, const char* uri, mcp_content_item_t*** content, size_t* content_count);

/**
 * @brief Stores content items for a given URI in the cache.
 *
 * This function is thread-safe. If an entry for the URI already exists, it will be overwritten.
 * If the cache is full, an existing entry might be evicted (e.g., based on LRU or simple replacement).
 * The cache makes copies of the provided content items.
 *
 * @param cache The cache instance.
 * @param uri The URI of the resource to store.
 * @param content An array of content item structs (mcp_content_item_t*) to store.
 * @param content_count The number of items in the `content` array.
 * @param ttl_seconds The time-to-live for this specific entry in seconds.
 *                    If 0, the cache's default TTL is used. If negative, the entry never expires.
 * @return 0 on success, -1 on failure (e.g., allocation error).
 */
int mcp_cache_put(mcp_resource_cache_t* cache, const char* uri, const mcp_content_item_t* content, size_t content_count, int ttl_seconds);

/**
 * @brief Removes an entry from the cache.
 *
 * This function is thread-safe.
 *
 * @param cache The cache instance.
 * @param uri The URI of the resource to invalidate/remove.
 * @return 0 if the item was found and removed, -1 otherwise.
 */
int mcp_cache_invalidate(mcp_resource_cache_t* cache, const char* uri);

/**
 * @brief Removes all expired entries from the cache.
 *
 * This function can be called periodically to clean up the cache.
 * It is thread-safe.
 *
 * @param cache The cache instance.
 * @return The number of entries removed.
 */
size_t mcp_cache_prune_expired(mcp_resource_cache_t* cache);


#endif // MCP_CACHE_H
