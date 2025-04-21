#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "kmcp_memory.h"
#include "kmcp_common.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Maximum number of allocations to track
 */
#define KMCP_MEMORY_MAX_ALLOCATIONS 10000

/**
 * @brief Memory context structure
 */
struct kmcp_memory_context {
    char name[64];                                /**< Context name */
    kmcp_memory_stats_t stats;                    /**< Memory statistics */
    kmcp_memory_allocation_t allocations[KMCP_MEMORY_MAX_ALLOCATIONS]; /**< Tracked allocations */
    size_t allocation_count;                      /**< Number of tracked allocations */
    mcp_mutex_t* mutex;                           /**< Mutex for thread safety */
};

/**
 * @brief Global memory tracking state
 */
static struct {
    kmcp_memory_tracking_mode_t tracking_mode;    /**< Memory tracking mode */
    kmcp_memory_stats_t stats;                    /**< Memory statistics */
    kmcp_memory_allocation_t allocations[KMCP_MEMORY_MAX_ALLOCATIONS]; /**< Tracked allocations */
    size_t allocation_count;                      /**< Number of tracked allocations */
    mcp_mutex_t* mutex;                           /**< Mutex for thread safety */
    bool initialized;                             /**< Whether the memory system is initialized */
} g_memory_state = {0};

/**
 * @brief Initialize the memory management system
 *
 * @param tracking_mode Memory tracking mode
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_init(kmcp_memory_tracking_mode_t tracking_mode) {
    // Check if already initialized
    if (g_memory_state.initialized) {
        return KMCP_SUCCESS;
    }

    // Initialize mutex
    g_memory_state.mutex = mcp_mutex_create();
    if (!g_memory_state.mutex) {
        return KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION, "Failed to create memory system mutex");
    }

    // Initialize state
    g_memory_state.tracking_mode = tracking_mode;
    memset(&g_memory_state.stats, 0, sizeof(g_memory_state.stats));
    memset(g_memory_state.allocations, 0, sizeof(g_memory_state.allocations));
    g_memory_state.allocation_count = 0;
    g_memory_state.initialized = true;

    mcp_log_info("KMCP memory system initialized with tracking mode %d", tracking_mode);
    return KMCP_SUCCESS;
}

/**
 * @brief Shut down the memory management system
 *
 * @param force_cleanup If true, force cleanup of all tracked allocations
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_shutdown(bool force_cleanup) {
    if (!g_memory_state.initialized) {
        return KMCP_SUCCESS;
    }

    // Acquire mutex
    mcp_mutex_lock(g_memory_state.mutex);

    // Check for leaks
    if (g_memory_state.stats.active_allocations > 0) {
        mcp_log_warn("Memory leaks detected: %zu active allocations",
                    g_memory_state.stats.active_allocations);

        if (g_memory_state.tracking_mode == KMCP_MEMORY_TRACKING_FULL) {
            // Print leak details
            kmcp_memory_print_leaks();
        }

        // Free leaked memory if requested
        if (force_cleanup) {
            for (size_t i = 0; i < g_memory_state.allocation_count; i++) {
                if (g_memory_state.allocations[i].ptr) {
                    free(g_memory_state.allocations[i].ptr);
                    g_memory_state.allocations[i].ptr = NULL;
                }
            }
        }
    }

    // Reset state
    g_memory_state.tracking_mode = KMCP_MEMORY_TRACKING_NONE;
    memset(&g_memory_state.stats, 0, sizeof(g_memory_state.stats));
    memset(g_memory_state.allocations, 0, sizeof(g_memory_state.allocations));
    g_memory_state.allocation_count = 0;

    // Release and destroy mutex
    mcp_mutex_unlock(g_memory_state.mutex);
    mcp_mutex_destroy(g_memory_state.mutex);
    g_memory_state.mutex = NULL;
    g_memory_state.initialized = false;

    mcp_log_info("KMCP memory system shut down");
    return KMCP_SUCCESS;
}

/**
 * @brief Track a memory allocation
 *
 * @param ptr Allocated pointer
 * @param size Allocation size
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
static kmcp_error_t track_allocation(void* ptr,
                                    size_t size,
                                    const char* file,
                                    int line,
                                    const char* function,
                                    const char* tag) {
    if (!g_memory_state.initialized || !ptr) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Update statistics
    g_memory_state.stats.total_allocated += size;
    g_memory_state.stats.current_usage += size;
    g_memory_state.stats.allocation_count++;
    g_memory_state.stats.active_allocations++;

    // Update peak usage
    if (g_memory_state.stats.current_usage > g_memory_state.stats.peak_usage) {
        g_memory_state.stats.peak_usage = g_memory_state.stats.current_usage;
    }

    // Track allocation details if in full tracking mode
    if (g_memory_state.tracking_mode == KMCP_MEMORY_TRACKING_FULL) {
        // Find an empty slot or reuse a freed slot
        size_t slot = g_memory_state.allocation_count;
        for (size_t i = 0; i < g_memory_state.allocation_count; i++) {
            if (g_memory_state.allocations[i].ptr == NULL) {
                slot = i;
                break;
            }
        }

        // Check if we need to add a new slot
        if (slot == g_memory_state.allocation_count) {
            // Check if we have room for a new allocation
            if (g_memory_state.allocation_count >= KMCP_MEMORY_MAX_ALLOCATIONS) {
                mcp_log_error("Maximum number of tracked allocations reached");
                return KMCP_ERROR_RESOURCE_BUSY;
            }
            g_memory_state.allocation_count++;
        }

        // Store allocation details
        g_memory_state.allocations[slot].ptr = ptr;
        g_memory_state.allocations[slot].size = size;
        g_memory_state.allocations[slot].file = file;
        g_memory_state.allocations[slot].line = line;
        g_memory_state.allocations[slot].function = function;
        g_memory_state.allocations[slot].tag = tag;
    }

    return KMCP_SUCCESS;
}

/**
 * @brief Untrack a memory allocation
 *
 * @param ptr Pointer to untrack
 * @return size_t Returns the size of the allocation, or 0 if not found
 */
static size_t untrack_allocation(void* ptr) {
    if (!g_memory_state.initialized || !ptr) {
        return 0;
    }

    size_t size = 0;

    // Find the allocation
    if (g_memory_state.tracking_mode == KMCP_MEMORY_TRACKING_FULL) {
        for (size_t i = 0; i < g_memory_state.allocation_count; i++) {
            if (g_memory_state.allocations[i].ptr == ptr) {
                size = g_memory_state.allocations[i].size;
                g_memory_state.allocations[i].ptr = NULL;
                break;
            }
        }
    }

    // If we didn't find the allocation in our tracking, use a default size
    // This can happen if tracking was disabled when the allocation was made
    if (size == 0) {
        // Use a reasonable default size
        size = 1;
    }

    // Update statistics
    g_memory_state.stats.total_freed += size;
    g_memory_state.stats.current_usage -= size;
    g_memory_state.stats.free_count++;
    g_memory_state.stats.active_allocations--;

    return size;
}

/**
 * @brief Allocate memory
 *
 * @param size Size to allocate
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation
 * @return void* Returns a pointer to the allocated memory, or NULL on failure
 */
void* kmcp_memory_alloc_ex(size_t size,
                          const char* file,
                          int line,
                          const char* function,
                          const char* tag) {
    // Check parameters
    if (size == 0) {
        KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER, "Allocation size cannot be zero");
        return NULL;
    }

    // Allocate memory
    void* ptr = malloc(size);
    if (!ptr) {
        KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION, "Failed to allocate %zu bytes", size);
        return NULL;
    }

    // Track the allocation if memory tracking is enabled
    if (g_memory_state.initialized &&
        g_memory_state.tracking_mode != KMCP_MEMORY_TRACKING_NONE) {
        // Acquire mutex
        mcp_mutex_lock(g_memory_state.mutex);

        // Track the allocation
        kmcp_error_t result = track_allocation(ptr, size, file, line, function, tag);
        if (result != KMCP_SUCCESS) {
            // Failed to track the allocation, but the memory is still allocated
            // Just log a warning and continue
            mcp_log_warn("Failed to track memory allocation at %p", ptr);
        }

        // Release mutex
        mcp_mutex_unlock(g_memory_state.mutex);
    }

    return ptr;
}

/**
 * @brief Allocate memory and zero it
 *
 * @param count Number of elements
 * @param size Size of each element
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation
 * @return void* Returns a pointer to the allocated memory, or NULL on failure
 */
void* kmcp_memory_calloc_ex(size_t count,
                           size_t size,
                           const char* file,
                           int line,
                           const char* function,
                           const char* tag) {
    // Check parameters
    if (count == 0 || size == 0) {
        KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER, "Allocation size cannot be zero");
        return NULL;
    }

    // Allocate memory
    void* ptr = calloc(count, size);
    if (!ptr) {
        KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION,
                      "Failed to allocate %zu bytes (%zu elements of %zu bytes)",
                      count * size, count, size);
        return NULL;
    }

    // Track the allocation if memory tracking is enabled
    if (g_memory_state.initialized &&
        g_memory_state.tracking_mode != KMCP_MEMORY_TRACKING_NONE) {
        // Acquire mutex
        mcp_mutex_lock(g_memory_state.mutex);

        // Track the allocation
        kmcp_error_t result = track_allocation(ptr, count * size, file, line, function, tag);
        if (result != KMCP_SUCCESS) {
            // Failed to track the allocation, but the memory is still allocated
            // Just log a warning and continue
            mcp_log_warn("Failed to track memory allocation at %p", ptr);
        }

        // Release mutex
        mcp_mutex_unlock(g_memory_state.mutex);
    }

    return ptr;
}

/**
 * @brief Reallocate memory
 *
 * @param ptr Pointer to reallocate
 * @param size New size
 * @param file Source file where reallocation occurred
 * @param line Line number where reallocation occurred
 * @param function Function where reallocation occurred
 * @param tag Optional tag for the allocation
 * @return void* Returns a pointer to the reallocated memory, or NULL on failure
 */
void* kmcp_memory_realloc_ex(void* ptr,
                            size_t size,
                            const char* file,
                            int line,
                            const char* function,
                            const char* tag) {
    // Check parameters
    if (size == 0) {
        KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER, "Reallocation size cannot be zero");
        return NULL;
    }

    // If ptr is NULL, this is equivalent to malloc
    if (!ptr) {
        return kmcp_memory_alloc_ex(size, file, line, function, tag);
    }

    // Untrack the old allocation if memory tracking is enabled
    size_t old_size = 0;
    if (g_memory_state.initialized &&
        g_memory_state.tracking_mode != KMCP_MEMORY_TRACKING_NONE) {
        // Acquire mutex
        mcp_mutex_lock(g_memory_state.mutex);

        // Untrack the old allocation
        old_size = untrack_allocation(ptr);

        // Release mutex
        mcp_mutex_unlock(g_memory_state.mutex);
    }

    // Reallocate memory
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        // Reallocation failed, but the original memory is still valid
        // Re-track the original allocation
        if (g_memory_state.initialized &&
            g_memory_state.tracking_mode != KMCP_MEMORY_TRACKING_NONE) {
            // Acquire mutex
            mcp_mutex_lock(g_memory_state.mutex);

            // Re-track the original allocation
            kmcp_error_t result = track_allocation(ptr, old_size, file, line, function, tag);
            if (result != KMCP_SUCCESS) {
                // Failed to re-track the allocation
                mcp_log_warn("Failed to re-track memory allocation at %p", ptr);
            }

            // Release mutex
            mcp_mutex_unlock(g_memory_state.mutex);
        }

        KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION,
                      "Failed to reallocate %zu bytes",
                      size);
        return NULL;
    }

    // Track the new allocation if memory tracking is enabled
    if (g_memory_state.initialized &&
        g_memory_state.tracking_mode != KMCP_MEMORY_TRACKING_NONE) {
        // Acquire mutex
        mcp_mutex_lock(g_memory_state.mutex);

        // Track the new allocation
        kmcp_error_t result = track_allocation(new_ptr, size, file, line, function, tag);
        if (result != KMCP_SUCCESS) {
            // Failed to track the allocation, but the memory is still allocated
            // Just log a warning and continue
            mcp_log_warn("Failed to track memory allocation at %p", new_ptr);
        }

        // Release mutex
        mcp_mutex_unlock(g_memory_state.mutex);
    }

    return new_ptr;
}

/**
 * @brief Free memory
 *
 * @param ptr Pointer to free
 */
void kmcp_memory_free(void* ptr) {
    if (!ptr) {
        return;
    }

    // Untrack the allocation if memory tracking is enabled
    if (g_memory_state.initialized &&
        g_memory_state.tracking_mode != KMCP_MEMORY_TRACKING_NONE) {
        // Acquire mutex
        mcp_mutex_lock(g_memory_state.mutex);

        // Untrack the allocation
        untrack_allocation(ptr);

        // Release mutex
        mcp_mutex_unlock(g_memory_state.mutex);
    }

    // Free the memory
    free(ptr);
}

/**
 * @brief Duplicate a string
 *
 * @param str String to duplicate
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation
 * @return char* Returns a pointer to the duplicated string, or NULL on failure
 */
char* kmcp_memory_strdup_ex(const char* str,
                           const char* file,
                           int line,
                           const char* function,
                           const char* tag) {
    // Check parameters
    if (!str) {
        KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER, "String to duplicate cannot be NULL");
        return NULL;
    }

    // Get string length
    size_t len = strlen(str) + 1; // +1 for null terminator

    // Allocate memory
    char* dup = (char*)kmcp_memory_alloc_ex(len, file, line, function, tag);
    if (!dup) {
        return NULL;
    }

    // Copy the string
    memcpy(dup, str, len);

    return dup;
}

/**
 * @brief Get memory statistics
 *
 * @param stats Pointer to a kmcp_memory_stats_t structure to fill
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_get_stats(kmcp_memory_stats_t* stats) {
    // Check parameters
    if (!stats) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER, "Stats pointer cannot be NULL");
    }

    // Check if memory system is initialized
    if (!g_memory_state.initialized) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_OPERATION, "Memory system not initialized");
    }

    // Acquire mutex
    mcp_mutex_lock(g_memory_state.mutex);

    // Copy statistics
    *stats = g_memory_state.stats;

    // Release mutex
    mcp_mutex_unlock(g_memory_state.mutex);

    return KMCP_SUCCESS;
}

/**
 * @brief Reset memory statistics
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_reset_stats(void) {
    // Check if memory system is initialized
    if (!g_memory_state.initialized) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_OPERATION, "Memory system not initialized");
    }

    // Acquire mutex
    mcp_mutex_lock(g_memory_state.mutex);

    // Reset statistics
    g_memory_state.stats.total_allocated = 0;
    g_memory_state.stats.total_freed = 0;
    g_memory_state.stats.allocation_count = 0;
    g_memory_state.stats.free_count = 0;
    g_memory_state.stats.peak_usage = g_memory_state.stats.current_usage;

    // Release mutex
    mcp_mutex_unlock(g_memory_state.mutex);

    return KMCP_SUCCESS;
}

/**
 * @brief Print memory statistics
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_print_stats(void) {
    // Check if memory system is initialized
    if (!g_memory_state.initialized) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_OPERATION, "Memory system not initialized");
    }

    // Get statistics
    kmcp_memory_stats_t stats;
    kmcp_error_t result = kmcp_memory_get_stats(&stats);
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Print statistics
    mcp_log_info("Memory Statistics:");
    mcp_log_info("  Total Allocated: %zu bytes", stats.total_allocated);
    mcp_log_info("  Total Freed: %zu bytes", stats.total_freed);
    mcp_log_info("  Current Usage: %zu bytes", stats.current_usage);
    mcp_log_info("  Peak Usage: %zu bytes", stats.peak_usage);
    mcp_log_info("  Allocation Count: %zu", stats.allocation_count);
    mcp_log_info("  Free Count: %zu", stats.free_count);
    mcp_log_info("  Active Allocations: %zu", stats.active_allocations);

    return KMCP_SUCCESS;
}

/**
 * @brief Print memory leaks
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_print_leaks(void) {
    // Check if memory system is initialized
    if (!g_memory_state.initialized) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_OPERATION, "Memory system not initialized");
    }

    // Check if full tracking is enabled
    if (g_memory_state.tracking_mode != KMCP_MEMORY_TRACKING_FULL) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_OPERATION,
                             "Full memory tracking not enabled");
    }

    // Acquire mutex
    mcp_mutex_lock(g_memory_state.mutex);

    // Check if there are any leaks
    if (g_memory_state.stats.active_allocations == 0) {
        mcp_log_info("No memory leaks detected");
        mcp_mutex_unlock(g_memory_state.mutex);
        return KMCP_SUCCESS;
    }

    // Print leak information
    mcp_log_warn("Memory leaks detected: %zu active allocations",
                g_memory_state.stats.active_allocations);

    size_t leak_count = 0;
    for (size_t i = 0; i < g_memory_state.allocation_count; i++) {
        if (g_memory_state.allocations[i].ptr) {
            leak_count++;
            mcp_log_warn("Leak #%zu: %p, %zu bytes, allocated at %s:%d [%s]",
                        leak_count,
                        g_memory_state.allocations[i].ptr,
                        g_memory_state.allocations[i].size,
                        g_memory_state.allocations[i].file,
                        g_memory_state.allocations[i].line,
                        g_memory_state.allocations[i].function);

            if (g_memory_state.allocations[i].tag) {
                mcp_log_warn("  Tag: %s", g_memory_state.allocations[i].tag);
            }
        }
    }

    // Release mutex
    mcp_mutex_unlock(g_memory_state.mutex);

    return KMCP_SUCCESS;
}

/**
 * @brief Create a memory context
 *
 * @param name Name of the context
 * @return kmcp_memory_context_t* Returns a pointer to the context, or NULL on failure
 */
kmcp_memory_context_t* kmcp_memory_context_create(const char* name) {
    // Check if memory system is initialized
    if (!g_memory_state.initialized) {
        KMCP_ERROR_LOG(KMCP_ERROR_INVALID_OPERATION, "Memory system not initialized");
        return NULL;
    }

    // Allocate memory for the context
    kmcp_memory_context_t* context = (kmcp_memory_context_t*)malloc(sizeof(kmcp_memory_context_t));
    if (!context) {
        KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION, "Failed to allocate memory for context");
        return NULL;
    }

    // Initialize the context
    memset(context, 0, sizeof(kmcp_memory_context_t));
    strncpy(context->name, name ? name : "unnamed", sizeof(context->name) - 1);
    context->name[sizeof(context->name) - 1] = '\0'; // Ensure null termination

    // Create mutex
    context->mutex = mcp_mutex_create();
    if (!context->mutex) {
        KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION, "Failed to create context mutex");
        free(context);
        return NULL;
    }

    return context;
}

/**
 * @brief Destroy a memory context
 *
 * @param context Context to destroy
 */
void kmcp_memory_context_destroy(kmcp_memory_context_t* context) {
    if (!context) {
        return;
    }

    // Acquire mutex
    mcp_mutex_lock(context->mutex);

    // Check if there are any leaks
    if (context->stats.active_allocations > 0) {
        mcp_log_warn("Memory leaks detected in context '%s': %zu active allocations",
                    context->name, context->stats.active_allocations);

        // Free leaked memory
        for (size_t i = 0; i < context->allocation_count; i++) {
            if (context->allocations[i].ptr) {
                free(context->allocations[i].ptr);
                context->allocations[i].ptr = NULL;
            }
        }
    }

    // Release mutex
    mcp_mutex_unlock(context->mutex);

    // Destroy mutex
    mcp_mutex_destroy(context->mutex);

    // Free context
    free(context);
}

/**
 * @brief Allocate memory in a context
 *
 * @param context Context to allocate in
 * @param size Size to allocate
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation
 * @return void* Returns a pointer to the allocated memory, or NULL on failure
 */
void* kmcp_memory_context_alloc_ex(kmcp_memory_context_t* context,
                                  size_t size,
                                  const char* file,
                                  int line,
                                  const char* function,
                                  const char* tag) {
    // Check parameters
    if (!context || size == 0) {
        KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER,
                      "Invalid context or allocation size");
        return NULL;
    }

    // Allocate memory
    void* ptr = malloc(size);
    if (!ptr) {
        KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION,
                      "Failed to allocate %zu bytes in context '%s'",
                      size, context->name);
        return NULL;
    }

    // Acquire mutex
    mcp_mutex_lock(context->mutex);

    // Update statistics
    context->stats.total_allocated += size;
    context->stats.current_usage += size;
    context->stats.allocation_count++;
    context->stats.active_allocations++;

    // Update peak usage
    if (context->stats.current_usage > context->stats.peak_usage) {
        context->stats.peak_usage = context->stats.current_usage;
    }

    // Track allocation details
    size_t slot = context->allocation_count;
    for (size_t i = 0; i < context->allocation_count; i++) {
        if (context->allocations[i].ptr == NULL) {
            slot = i;
            break;
        }
    }

    // Check if we need to add a new slot
    if (slot == context->allocation_count) {
        // Check if we have room for a new allocation
        if (context->allocation_count >= KMCP_MEMORY_MAX_ALLOCATIONS) {
            mcp_log_error("Maximum number of tracked allocations reached in context '%s'",
                         context->name);
            mcp_mutex_unlock(context->mutex);
            free(ptr);
            return NULL;
        }
        context->allocation_count++;
    }

    // Store allocation details
    context->allocations[slot].ptr = ptr;
    context->allocations[slot].size = size;
    context->allocations[slot].file = file;
    context->allocations[slot].line = line;
    context->allocations[slot].function = function;
    context->allocations[slot].tag = tag;

    // Release mutex
    mcp_mutex_unlock(context->mutex);

    return ptr;
}

/**
 * @brief Allocate memory in a context and zero it
 *
 * @param context Context to allocate in
 * @param count Number of elements
 * @param size Size of each element
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation
 * @return void* Returns a pointer to the allocated memory, or NULL on failure
 */
void* kmcp_memory_context_calloc_ex(kmcp_memory_context_t* context,
                                   size_t count,
                                   size_t size,
                                   const char* file,
                                   int line,
                                   const char* function,
                                   const char* tag) {
    // Check parameters
    if (!context || count == 0 || size == 0) {
        KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER,
                      "Invalid context or allocation size");
        return NULL;
    }

    // Calculate total size
    size_t total_size = count * size;

    // Allocate memory
    void* ptr = kmcp_memory_context_alloc_ex(context, total_size, file, line, function, tag);
    if (!ptr) {
        return NULL;
    }

    // Zero the memory
    memset(ptr, 0, total_size);

    return ptr;
}

/**
 * @brief Duplicate a string in a context
 *
 * @param context Context to allocate in
 * @param str String to duplicate
 * @param file Source file where allocation occurred
 * @param line Line number where allocation occurred
 * @param function Function where allocation occurred
 * @param tag Optional tag for the allocation
 * @return char* Returns a pointer to the duplicated string, or NULL on failure
 */
char* kmcp_memory_context_strdup_ex(kmcp_memory_context_t* context,
                                   const char* str,
                                   const char* file,
                                   int line,
                                   const char* function,
                                   const char* tag) {
    // Check parameters
    if (!context || !str) {
        KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER,
                      "Invalid context or string");
        return NULL;
    }

    // Get string length
    size_t len = strlen(str) + 1; // +1 for null terminator

    // Allocate memory
    char* dup = (char*)kmcp_memory_context_alloc_ex(context, len, file, line, function, tag);
    if (!dup) {
        return NULL;
    }

    // Copy the string
    memcpy(dup, str, len);

    return dup;
}

/**
 * @brief Free memory in a context
 *
 * @param context Context to free from
 * @param ptr Pointer to free
 */
void kmcp_memory_context_free(kmcp_memory_context_t* context, void* ptr) {
    if (!context || !ptr) {
        return;
    }

    // Acquire mutex
    mcp_mutex_lock(context->mutex);

    // Find the allocation
    size_t size = 0;
    for (size_t i = 0; i < context->allocation_count; i++) {
        if (context->allocations[i].ptr == ptr) {
            size = context->allocations[i].size;
            context->allocations[i].ptr = NULL;
            break;
        }
    }

    // If we didn't find the allocation, use a default size
    if (size == 0) {
        // Use a reasonable default size
        size = 1;
    }

    // Update statistics
    context->stats.total_freed += size;
    context->stats.current_usage -= size;
    context->stats.free_count++;
    context->stats.active_allocations--;

    // Release mutex
    mcp_mutex_unlock(context->mutex);

    // Free the memory
    free(ptr);
}

/**
 * @brief Get memory context statistics
 *
 * @param context Context to get statistics for
 * @param stats Pointer to a kmcp_memory_stats_t structure to fill
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_context_get_stats(kmcp_memory_context_t* context,
                                          kmcp_memory_stats_t* stats) {
    // Check parameters
    if (!context || !stats) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER,
                             "Invalid context or stats pointer");
    }

    // Acquire mutex
    mcp_mutex_lock(context->mutex);

    // Copy statistics
    *stats = context->stats;

    // Release mutex
    mcp_mutex_unlock(context->mutex);

    return KMCP_SUCCESS;
}

/**
 * @brief Print memory context statistics
 *
 * @param context Context to print statistics for
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_memory_context_print_stats(kmcp_memory_context_t* context) {
    // Check parameters
    if (!context) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER, "Invalid context");
    }

    // Get statistics
    kmcp_memory_stats_t stats;
    kmcp_error_t result = kmcp_memory_context_get_stats(context, &stats);
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Print statistics
    mcp_log_info("Memory Statistics for context '%s':", context->name);
    mcp_log_info("  Total Allocated: %zu bytes", stats.total_allocated);
    mcp_log_info("  Total Freed: %zu bytes", stats.total_freed);
    mcp_log_info("  Current Usage: %zu bytes", stats.current_usage);
    mcp_log_info("  Peak Usage: %zu bytes", stats.peak_usage);
    mcp_log_info("  Allocation Count: %zu", stats.allocation_count);
    mcp_log_info("  Free Count: %zu", stats.free_count);
    mcp_log_info("  Active Allocations: %zu", stats.active_allocations);

    return KMCP_SUCCESS;
}
