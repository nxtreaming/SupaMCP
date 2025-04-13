# Cache Implementation Documentation

## Overview

This document describes the resource cache implementation in the MCP Server. The cache is used to store and retrieve content items, such as API responses, to improve performance and reduce load on backend systems.

## Key Features

1. **Time-Based Expiration**: Cache entries can have a time-to-live (TTL) after which they are automatically expired.
2. **Capacity Management**: The cache has a configurable maximum capacity and implements an eviction strategy when full.
3. **Thread Safety**: The cache is thread-safe, allowing concurrent access from multiple threads.
4. **Memory Pooling**: The cache supports using memory pools for content items, improving memory efficiency and reducing fragmentation.

## API Reference

### Cache Creation and Destruction

```c
/**
 * @brief Create a new resource cache
 *
 * @param capacity Maximum number of entries the cache can hold
 * @param default_ttl_seconds Default time-to-live for entries (0 for never expires)
 * @return A newly created cache, or NULL on error
 */
mcp_resource_cache_t* mcp_cache_create(size_t capacity, time_t default_ttl_seconds);

/**
 * @brief Destroy a resource cache
 *
 * @param cache The cache to destroy
 */
void mcp_cache_destroy(mcp_resource_cache_t* cache);
```

### Cache Operations

```c
/**
 * @brief Put an item in the cache
 *
 * @param cache The cache to put the item in
 * @param uri The URI to use as the key
 * @param pool The memory pool to use for content items
 * @param content Array of content items to store
 * @param content_count Number of content items
 * @param ttl_seconds Time-to-live in seconds (0 for default, -1 for never expires)
 * @return 0 on success, non-zero on error
 */
int mcp_cache_put(mcp_resource_cache_t* cache, const char* uri, mcp_object_pool_t* pool, mcp_content_item_t** content, size_t content_count, int ttl_seconds);

/**
 * @brief Get an item from the cache
 *
 * @param cache The cache to get the item from
 * @param uri The URI to use as the key
 * @param pool The memory pool to use for content items
 * @param content Pointer to store the array of content items
 * @param content_count Pointer to store the number of content items
 * @return 0 on success, non-zero on error (including not found or expired)
 */
int mcp_cache_get(mcp_resource_cache_t* cache, const char* uri, mcp_object_pool_t* pool, mcp_content_item_t*** content, size_t* content_count);

/**
 * @brief Invalidate an item in the cache
 *
 * @param cache The cache to invalidate the item in
 * @param uri The URI to use as the key
 * @return 0 on success, non-zero on error (including not found)
 */
int mcp_cache_invalidate(mcp_resource_cache_t* cache, const char* uri);

/**
 * @brief Prune expired items from the cache
 *
 * @param cache The cache to prune
 * @return Number of items pruned
 */
size_t mcp_cache_prune_expired(mcp_resource_cache_t* cache);
```

## Usage Examples

### Basic Usage

```c
// Create a cache with capacity 100 and default TTL 60 seconds
mcp_resource_cache_t* cache = mcp_cache_create(100, 60);

// Create a memory pool for content items
mcp_object_pool_t* pool = mcp_object_pool_create(sizeof(mcp_content_item_t), 32, 0);

// Create a content item
mcp_content_item_t* item = mcp_content_item_create(MCP_CONTENT_TYPE_TEXT, "text/plain", "Hello, World!", 13);
mcp_content_item_t* items[] = { item };

// Put the item in the cache
mcp_cache_put(cache, "example://hello", pool, items, 1, 0);

// Get the item from the cache
mcp_content_item_t** retrieved_items = NULL;
size_t retrieved_count = 0;
if (mcp_cache_get(cache, "example://hello", pool, &retrieved_items, &retrieved_count) == 0) {
    // Use the retrieved items
    // ...
    
    // Free the retrieved items
    for (size_t i = 0; i < retrieved_count; i++) {
        mcp_content_item_free(retrieved_items[i]);
    }
    free(retrieved_items);
}

// Invalidate the item
mcp_cache_invalidate(cache, "example://hello");

// Prune expired items
size_t pruned = mcp_cache_prune_expired(cache);

// Destroy the cache and pool
mcp_cache_destroy(cache);
mcp_object_pool_destroy(pool);
```

### Using Pooled Memory

```c
// Create a cache and a memory pool
mcp_resource_cache_t* cache = mcp_cache_create(100, 60);
mcp_object_pool_t* pool = mcp_object_pool_create(sizeof(mcp_content_item_t), 32, 0);

// Acquire a pooled content item
mcp_content_item_t* item = mcp_content_item_acquire_pooled(pool, MCP_CONTENT_TYPE_TEXT, "text/plain", "Hello, World!", 13);
mcp_content_item_t* items[] = { item };

// Put the item in the cache
mcp_cache_put(cache, "example://hello", pool, items, 1, 0);

// Get the item from the cache
mcp_content_item_t** retrieved_items = NULL;
size_t retrieved_count = 0;
if (mcp_cache_get(cache, "example://hello", pool, &retrieved_items, &retrieved_count) == 0) {
    // Use the retrieved items
    // ...
    
    // Release the retrieved items back to the pool
    for (size_t i = 0; i < retrieved_count; i++) {
        // Free the internal data first
        free(retrieved_items[i]->mime_type);
        free(retrieved_items[i]->data);
        retrieved_items[i]->mime_type = NULL;
        retrieved_items[i]->data = NULL;
        retrieved_items[i]->data_size = 0;
        
        // Release the item back to the pool
        mcp_object_pool_release(pool, retrieved_items[i]);
    }
    free(retrieved_items);
}

// Destroy the cache and pool
mcp_cache_destroy(cache);
mcp_object_pool_destroy(pool);
```

## Implementation Details

The cache implementation is in `src/server/mcp_cache_optimized_fix.c`. It includes the following components:

1. **Cache Structure**: A structure to hold the cache state, including a hash table for entries, a mutex for thread safety, and configuration parameters.
2. **Cache Entry Structure**: A structure to hold a cache entry, including content items, expiration time, and access time.
3. **Hash Table**: A hash table to store cache entries, with string keys (URIs) and cache entry values.
4. **Thread Safety**: A mutex to ensure thread safety for cache operations.
5. **Eviction Strategy**: A simple eviction strategy that removes the first entry found when the cache is full.
6. **Memory Pooling**: Support for using memory pools for content items, improving memory efficiency and reducing fragmentation.

## Memory Management

The cache implementation carefully manages memory to avoid leaks and use memory efficiently:

1. **Content Item Copying**: When putting an item in the cache, the implementation creates a copy of the content item to ensure the cache has its own copy.
2. **Pooled Memory**: The implementation supports using memory pools for content items, which can improve memory efficiency and reduce fragmentation.
3. **Cleanup Functions**: The implementation includes custom cleanup functions to properly release pooled memory back to the pool.
4. **Hash Table Ownership**: The hash table takes ownership of the keys and values it stores, and frees them when they are removed or the table is destroyed.

## Thread Safety

The cache implementation is thread-safe, allowing concurrent access from multiple threads:

1. **Mutex**: The cache uses a mutex to ensure that only one thread can modify the cache at a time.
2. **Read-Write Operations**: The mutex is locked during both read and write operations to ensure consistency.
3. **Atomic Operations**: The implementation uses atomic operations where appropriate to improve performance.

## Eviction Strategy

The cache implementation includes a simple eviction strategy that removes the first entry found when the cache is full:

1. **Capacity Check**: When putting an item in the cache, the implementation checks if the cache is at capacity.
2. **Eviction**: If the cache is at capacity, the implementation evicts the first entry it finds to make room for the new entry.
3. **Logging**: The implementation logs a warning when an entry is evicted.

## Limitations

1. **Eviction Strategy**: The current eviction strategy is simple and may not be optimal for all use cases. A more sophisticated strategy, such as LRU (Least Recently Used) or LFU (Least Frequently Used), could be implemented.
2. **Memory Usage**: The cache stores copies of content items, which can use a significant amount of memory for large items or many items.
3. **Concurrency**: The mutex-based thread safety can limit concurrency, especially for read-heavy workloads. A more sophisticated concurrency strategy, such as reader-writer locks, could be implemented.

## Future Improvements

1. **Improved Eviction Strategy**: Implement a more sophisticated eviction strategy, such as LRU or LFU.
2. **Reader-Writer Locks**: Use reader-writer locks to improve concurrency for read-heavy workloads.
3. **Cache Statistics**: Add functions to get statistics about the cache, such as hit rate, miss rate, and eviction count.
4. **Cache Persistence**: Add support for persisting the cache to disk and loading it on startup.
5. **Distributed Cache**: Add support for distributed caching across multiple servers.
