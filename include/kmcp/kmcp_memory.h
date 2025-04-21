/**
 * @file kmcp_memory.h
 * @brief Memory management utilities for KMCP module
 *
 * This file defines memory management utilities for the KMCP module,
 * including memory allocation, tracking, and cleanup functions.
 */

#ifndef KMCP_MEMORY_H
#define KMCP_MEMORY_H

#include "kmcp_error.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Memory allocation tracking mode
 */
typedef enum {
    KMCP_MEMORY_TRACKING_NONE = 0,    /**< No memory tracking */
    KMCP_MEMORY_TRACKING_STATS = 1,   /**< Track allocation statistics only */
    KMCP_MEMORY_TRACKING_FULL = 2     /**< Track all allocations with details */
} kmcp_memory_tracking_mode_t;

/**
 * @brief Memory allocation statistics
 */
typedef struct {
    size_t total_allocated;           /**< Total bytes allocated */
    size_t total_freed;               /**< Total bytes freed */
    size_t current_usage;             /**< Current memory usage */
    size_t peak_usage;                /**< Peak memory usage */
    size_t allocation_count;          /**< Number of allocations */
    size_t free_count;                /**< Number of frees */
    size_t active_allocations;        /**< Number of active allocations */
} kmcp_memory_stats_t;

/**
 * @brief Memory allocation entry
 */
typedef struct {
    void* ptr;                        /**< Allocated pointer */
    size_t size;                      /**< Allocation size */
    const char* file;                 /**< Source file where allocation occurred */
    int line;                         /**< Line number where allocation occurred */
    const char* function;             /**< Function where allocation occurred */
    const char* tag;                  /**< Optional tag for the allocation */
} kmcp_memory_allocation_t;

/**
 * @brief Memory context
 *
 * A memory context is a container for related allocations. It can be used to
 * track and free groups of allocations together.
 */
typedef struct kmcp_memory_context kmcp_memory_context_t;

/**
 * @brief Initialize the memory management system
 *
 * This function initializes the memory management system with the specified
 * tracking mode.
 *
 * @param tracking_mode Memory tracking mode
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_init(kmcp_memory_tracking_mode_t tracking_mode);

/**
 * @brief Shut down the memory management system
 *
 * This function shuts down the memory management system and frees all tracked
 * allocations.
 *
 * @param force_cleanup If true, force cleanup of all tracked allocations
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_shutdown(bool force_cleanup);

/**
 * @brief Allocate memory
 *
 * This function allocates memory and optionally tracks the allocation.
 *
 * @param size Size to allocate
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation (can be NULL)
 * @return void* Returns a pointer to the allocated memory, or NULL on failure
 */
void* kmcp_memory_alloc_ex(size_t size, 
                          const char* file, 
                          int line, 
                          const char* function, 
                          const char* tag);

/**
 * @brief Allocate memory and zero it
 *
 * This function allocates memory, zeros it, and optionally tracks the allocation.
 *
 * @param size Size to allocate
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation (can be NULL)
 * @return void* Returns a pointer to the allocated memory, or NULL on failure
 */
void* kmcp_memory_calloc_ex(size_t count, 
                           size_t size, 
                           const char* file, 
                           int line, 
                           const char* function, 
                           const char* tag);

/**
 * @brief Reallocate memory
 *
 * This function reallocates memory and optionally tracks the allocation.
 *
 * @param ptr Pointer to reallocate
 * @param size New size
 * @param file Source file where reallocation occurred
 * @param line Line number where reallocation occurred
 * @param function Function where reallocation occurred
 * @param tag Optional tag for the allocation (can be NULL)
 * @return void* Returns a pointer to the reallocated memory, or NULL on failure
 */
void* kmcp_memory_realloc_ex(void* ptr, 
                            size_t size, 
                            const char* file, 
                            int line, 
                            const char* function, 
                            const char* tag);

/**
 * @brief Free memory
 *
 * This function frees memory and updates tracking information.
 *
 * @param ptr Pointer to free
 */
void kmcp_memory_free(void* ptr);

/**
 * @brief Duplicate a string
 *
 * This function duplicates a string and optionally tracks the allocation.
 *
 * @param str String to duplicate
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation (can be NULL)
 * @return char* Returns a pointer to the duplicated string, or NULL on failure
 */
char* kmcp_memory_strdup_ex(const char* str, 
                           const char* file, 
                           int line, 
                           const char* function, 
                           const char* tag);

/**
 * @brief Get memory statistics
 *
 * This function gets the current memory statistics.
 *
 * @param stats Pointer to a kmcp_memory_stats_t structure to fill
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_get_stats(kmcp_memory_stats_t* stats);

/**
 * @brief Reset memory statistics
 *
 * This function resets the memory statistics.
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_reset_stats(void);

/**
 * @brief Print memory statistics
 *
 * This function prints the current memory statistics to the log.
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_print_stats(void);

/**
 * @brief Print memory leaks
 *
 * This function prints information about memory leaks to the log.
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_print_leaks(void);

/**
 * @brief Create a memory context
 *
 * This function creates a new memory context.
 *
 * @param name Name of the context
 * @return kmcp_memory_context_t* Returns a pointer to the context, or NULL on failure
 */
kmcp_memory_context_t* kmcp_memory_context_create(const char* name);

/**
 * @brief Destroy a memory context
 *
 * This function destroys a memory context and frees all allocations associated with it.
 *
 * @param context Context to destroy
 */
void kmcp_memory_context_destroy(kmcp_memory_context_t* context);

/**
 * @brief Allocate memory in a context
 *
 * This function allocates memory and associates it with the specified context.
 *
 * @param context Context to allocate in
 * @param size Size to allocate
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation (can be NULL)
 * @return void* Returns a pointer to the allocated memory, or NULL on failure
 */
void* kmcp_memory_context_alloc_ex(kmcp_memory_context_t* context, 
                                  size_t size, 
                                  const char* file, 
                                  int line, 
                                  const char* function, 
                                  const char* tag);

/**
 * @brief Allocate memory in a context and zero it
 *
 * This function allocates memory, zeros it, and associates it with the specified context.
 *
 * @param context Context to allocate in
 * @param count Number of elements
 * @param size Size of each element
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation (can be NULL)
 * @return void* Returns a pointer to the allocated memory, or NULL on failure
 */
void* kmcp_memory_context_calloc_ex(kmcp_memory_context_t* context, 
                                   size_t count, 
                                   size_t size, 
                                   const char* file, 
                                   int line, 
                                   const char* function, 
                                   const char* tag);

/**
 * @brief Duplicate a string in a context
 *
 * This function duplicates a string and associates it with the specified context.
 *
 * @param context Context to allocate in
 * @param str String to duplicate
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation (can be NULL)
 * @return char* Returns a pointer to the duplicated string, or NULL on failure
 */
char* kmcp_memory_context_strdup_ex(kmcp_memory_context_t* context, 
                                   const char* str, 
                                   const char* file, 
                                   int line, 
                                   const char* function, 
                                   const char* tag);

/**
 * @brief Free memory in a context
 *
 * This function frees memory associated with the specified context.
 *
 * @param context Context to free from
 * @param ptr Pointer to free
 */
void kmcp_memory_context_free(kmcp_memory_context_t* context, void* ptr);

/**
 * @brief Get memory context statistics
 *
 * This function gets the memory statistics for the specified context.
 *
 * @param context Context to get statistics for
 * @param stats Pointer to a kmcp_memory_stats_t structure to fill
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_context_get_stats(kmcp_memory_context_t* context, 
                                          kmcp_memory_stats_t* stats);

/**
 * @brief Print memory context statistics
 *
 * This function prints the memory statistics for the specified context to the log.
 *
 * @param context Context to print statistics for
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_context_print_stats(kmcp_memory_context_t* context);

/**
 * @brief Convenience macro for allocating memory
 */
#define KMCP_MEMORY_ALLOC(size) \
    kmcp_memory_alloc_ex((size), __FILE__, __LINE__, __func__, NULL)

/**
 * @brief Convenience macro for allocating memory with a tag
 */
#define KMCP_MEMORY_ALLOC_TAG(size, tag) \
    kmcp_memory_alloc_ex((size), __FILE__, __LINE__, __func__, (tag))

/**
 * @brief Convenience macro for allocating and zeroing memory
 */
#define KMCP_MEMORY_CALLOC(count, size) \
    kmcp_memory_calloc_ex((count), (size), __FILE__, __LINE__, __func__, NULL)

/**
 * @brief Convenience macro for allocating and zeroing memory with a tag
 */
#define KMCP_MEMORY_CALLOC_TAG(count, size, tag) \
    kmcp_memory_calloc_ex((count), (size), __FILE__, __LINE__, __func__, (tag))

/**
 * @brief Convenience macro for reallocating memory
 */
#define KMCP_MEMORY_REALLOC(ptr, size) \
    kmcp_memory_realloc_ex((ptr), (size), __FILE__, __LINE__, __func__, NULL)

/**
 * @brief Convenience macro for reallocating memory with a tag
 */
#define KMCP_MEMORY_REALLOC_TAG(ptr, size, tag) \
    kmcp_memory_realloc_ex((ptr), (size), __FILE__, __LINE__, __func__, (tag))

/**
 * @brief Convenience macro for duplicating a string
 */
#define KMCP_MEMORY_STRDUP(str) \
    kmcp_memory_strdup_ex((str), __FILE__, __LINE__, __func__, NULL)

/**
 * @brief Convenience macro for duplicating a string with a tag
 */
#define KMCP_MEMORY_STRDUP_TAG(str, tag) \
    kmcp_memory_strdup_ex((str), __FILE__, __LINE__, __func__, (tag))

/**
 * @brief Convenience macro for allocating memory in a context
 */
#define KMCP_MEMORY_CONTEXT_ALLOC(context, size) \
    kmcp_memory_context_alloc_ex((context), (size), __FILE__, __LINE__, __func__, NULL)

/**
 * @brief Convenience macro for allocating memory in a context with a tag
 */
#define KMCP_MEMORY_CONTEXT_ALLOC_TAG(context, size, tag) \
    kmcp_memory_context_alloc_ex((context), (size), __FILE__, __LINE__, __func__, (tag))

/**
 * @brief Convenience macro for allocating and zeroing memory in a context
 */
#define KMCP_MEMORY_CONTEXT_CALLOC(context, count, size) \
    kmcp_memory_context_calloc_ex((context), (count), (size), __FILE__, __LINE__, __func__, NULL)

/**
 * @brief Convenience macro for allocating and zeroing memory in a context with a tag
 */
#define KMCP_MEMORY_CONTEXT_CALLOC_TAG(context, count, size, tag) \
    kmcp_memory_context_calloc_ex((context), (count), (size), __FILE__, __LINE__, __func__, (tag))

/**
 * @brief Convenience macro for duplicating a string in a context
 */
#define KMCP_MEMORY_CONTEXT_STRDUP(context, str) \
    kmcp_memory_context_strdup_ex((context), (str), __FILE__, __LINE__, __func__, NULL)

/**
 * @brief Convenience macro for duplicating a string in a context with a tag
 */
#define KMCP_MEMORY_CONTEXT_STRDUP_TAG(context, str, tag) \
    kmcp_memory_context_strdup_ex((context), (str), __FILE__, __LINE__, __func__, (tag))

#ifdef __cplusplus
}
#endif

#endif /* KMCP_MEMORY_H */
