#ifndef MCP_OBJECT_CACHE_H
#define MCP_OBJECT_CACHE_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Object cache type identifier
 */
typedef enum {
    MCP_OBJECT_CACHE_GENERIC = 0,  /**< Generic object cache */
    MCP_OBJECT_CACHE_STRING,       /**< String object cache */
    MCP_OBJECT_CACHE_JSON,         /**< JSON object cache */
    MCP_OBJECT_CACHE_ARENA,        /**< Arena object cache */
    MCP_OBJECT_CACHE_BUFFER,       /**< Buffer object cache */
    MCP_OBJECT_CACHE_CUSTOM1,      /**< Custom object type 1 */
    MCP_OBJECT_CACHE_CUSTOM2,      /**< Custom object type 2 */
    MCP_OBJECT_CACHE_CUSTOM3,      /**< Custom object type 3 */
    MCP_OBJECT_CACHE_CUSTOM4,      /**< Custom object type 4 */
    MCP_OBJECT_CACHE_TYPE_COUNT    /**< Number of object cache types */
} mcp_object_cache_type_t;

/**
 * @brief Object cache configuration
 */
typedef struct {
    size_t max_size;               /**< Maximum number of objects in cache */
    bool adaptive_sizing;          /**< Whether to enable adaptive cache sizing */
    double growth_threshold;       /**< Hit ratio threshold for growing cache (0.0-1.0) */
    double shrink_threshold;       /**< Hit ratio threshold for shrinking cache (0.0-1.0) */
    size_t min_cache_size;         /**< Minimum cache size for adaptive sizing */
    size_t max_cache_size;         /**< Maximum cache size for adaptive sizing */
    void (*constructor)(void*);    /**< Optional constructor function */
    void (*destructor)(void*);     /**< Optional destructor function */
} mcp_object_cache_config_t;

/**
 * @brief Object cache statistics
 */
typedef struct {
    size_t cache_count;            /**< Number of objects in cache */
    size_t max_size;               /**< Maximum number of objects in cache */
    bool adaptive_sizing;          /**< Whether adaptive cache sizing is enabled */
    size_t cache_hits;             /**< Number of cache hits */
    size_t cache_misses;           /**< Number of cache misses */
    size_t cache_flushes;          /**< Number of cache flushes */
    double hit_ratio;              /**< Cache hit ratio (0.0-1.0) */
} mcp_object_cache_stats_t;

/**
 * @brief Initialize the object cache system
 * 
 * @return true if initialization was successful, false otherwise
 */
bool mcp_object_cache_system_init(void);

/**
 * @brief Shutdown the object cache system
 */
void mcp_object_cache_system_shutdown(void);

/**
 * @brief Check if the object cache system is initialized
 * 
 * @return true if the object cache system is initialized, false otherwise
 */
bool mcp_object_cache_system_is_initialized(void);

/**
 * @brief Initialize an object cache for a specific type
 * 
 * @param type The type of objects to cache
 * @param config Configuration for the cache, or NULL for default configuration
 * @return true if initialization was successful, false otherwise
 */
bool mcp_object_cache_init(mcp_object_cache_type_t type, const mcp_object_cache_config_t* config);

/**
 * @brief Clean up an object cache for a specific type
 * 
 * @param type The type of objects to clean up
 */
void mcp_object_cache_cleanup(mcp_object_cache_type_t type);

/**
 * @brief Allocate an object from the cache
 * 
 * @param type The type of object to allocate
 * @param size Size of the object to allocate
 * @return Pointer to the allocated object, or NULL if allocation failed
 */
void* mcp_object_cache_alloc(mcp_object_cache_type_t type, size_t size);

/**
 * @brief Free an object to the cache
 * 
 * @param type The type of object to free
 * @param ptr Pointer to the object to free
 * @param size Size of the object (optional, can be 0 if unknown)
 */
void mcp_object_cache_free(mcp_object_cache_type_t type, void* ptr, size_t size);

/**
 * @brief Get statistics for an object cache
 * 
 * @param type The type of object cache to get statistics for
 * @param stats Pointer to a statistics structure to fill
 * @return true if statistics were successfully retrieved, false otherwise
 */
bool mcp_object_cache_get_stats(mcp_object_cache_type_t type, mcp_object_cache_stats_t* stats);

/**
 * @brief Configure an object cache
 * 
 * @param type The type of object cache to configure
 * @param config Configuration for the cache
 * @return true if configuration was successful, false otherwise
 */
bool mcp_object_cache_configure(mcp_object_cache_type_t type, const mcp_object_cache_config_t* config);

/**
 * @brief Enable or disable adaptive sizing for an object cache
 * 
 * @param type The type of object cache to configure
 * @param enable Whether to enable adaptive sizing
 * @return true if the operation was successful, false otherwise
 */
bool mcp_object_cache_enable_adaptive_sizing(mcp_object_cache_type_t type, bool enable);

/**
 * @brief Flush an object cache, returning all objects to the global pools
 * 
 * @param type The type of object cache to flush
 */
void mcp_object_cache_flush(mcp_object_cache_type_t type);

/**
 * @brief Register a custom object type with constructor and destructor functions
 * 
 * @param type The custom object type to register (must be one of MCP_OBJECT_CACHE_CUSTOM*)
 * @param constructor Optional constructor function to call when allocating objects
 * @param destructor Optional destructor function to call when freeing objects
 * @return true if registration was successful, false otherwise
 */
bool mcp_object_cache_register_type(mcp_object_cache_type_t type, 
                                   void (*constructor)(void*), 
                                   void (*destructor)(void*));

/**
 * @brief Get the name of an object cache type
 * 
 * @param type The object cache type
 * @return The name of the object cache type
 */
const char* mcp_object_cache_type_name(mcp_object_cache_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* MCP_OBJECT_CACHE_H */
