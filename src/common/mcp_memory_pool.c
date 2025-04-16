#include "mcp_memory_pool.h"
#include "mcp_memory_constants.h"
#include "mcp_thread_cache.h"
#include "mcp_sync.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Magic value to identify blocks allocated from pools
#define MCP_POOL_MAGIC 0xABCD1234

// Structure to track memory block metadata
typedef struct mcp_block_header {
    uint32_t magic;              // Magic value to identify pool blocks
    mcp_memory_pool_t* pool;     // Pointer back to the owning pool
    struct mcp_block_header* next; // Next block in free list
} mcp_block_header_t;

// Memory pool implementation
struct mcp_memory_pool {
    size_t block_size;           // Size of each block (including header)
    size_t user_block_size;      // Size available to the user
    size_t initial_blocks;       // Number of blocks initially allocated
    size_t max_blocks;           // Maximum number of blocks (0 = unlimited)
    size_t total_blocks;         // Total number of blocks allocated
    size_t free_blocks;          // Number of blocks in the free list
    size_t peak_usage;           // Peak number of blocks in use
    mcp_block_header_t* free_list; // List of free blocks
    mcp_mutex_t* lock;           // Mutex for thread safety
    void* memory_block;          // Pointer to the pre-allocated memory block (if any)
};

// Global memory pools
static mcp_memory_pool_t* g_pools[MCP_POOL_SIZE_COUNT] = {NULL};
static bool g_pools_initialized = false;

// Thread-local cache for each size class
#ifdef _WIN32
__declspec(thread) static void* tls_cache[MCP_POOL_SIZE_COUNT][8] = {{NULL}};
__declspec(thread) static size_t tls_cache_count[MCP_POOL_SIZE_COUNT] = {0};
#else
__thread static void* tls_cache[MCP_POOL_SIZE_COUNT][8] = {{NULL}};
__thread static size_t tls_cache_count[MCP_POOL_SIZE_COUNT] = {0};
#endif

// Forward declarations
static void* allocate_from_pool(mcp_memory_pool_t* pool);
static bool return_to_pool(mcp_memory_pool_t* pool, void* block);
static mcp_pool_size_class_t get_size_class(size_t size);
static size_t get_block_size_for_class(mcp_pool_size_class_t size_class);

mcp_memory_pool_t* mcp_memory_pool_create(size_t block_size, size_t initial_blocks, size_t max_blocks) {
    if (block_size < sizeof(mcp_block_header_t)) {
        mcp_log_error("Block size too small: %zu bytes (minimum: %zu bytes)",
                     block_size, sizeof(mcp_block_header_t));
        return NULL;
    }

    // Allocate the pool structure
    mcp_memory_pool_t* pool = (mcp_memory_pool_t*)malloc(sizeof(mcp_memory_pool_t));
    if (!pool) {
        mcp_log_error("Failed to allocate memory pool structure");
        return NULL;
    }

    // Initialize pool properties
    pool->block_size = block_size + sizeof(mcp_block_header_t);
    pool->user_block_size = block_size;
    pool->initial_blocks = initial_blocks;
    pool->max_blocks = max_blocks;
    pool->total_blocks = 0;
    pool->free_blocks = 0;
    pool->peak_usage = 0;
    pool->free_list = NULL;
    pool->memory_block = NULL;

    // Create mutex for thread safety
    pool->lock = mcp_mutex_create();
    if (!pool->lock) {
        mcp_log_error("Failed to create mutex for memory pool");
        free(pool);
        return NULL;
    }

    // Pre-allocate initial blocks if requested
    if (initial_blocks > 0) {
        // Allocate a single contiguous block for better memory locality
        size_t total_size = initial_blocks * pool->block_size;
        pool->memory_block = malloc(total_size);

        if (pool->memory_block) {
            // Initialize each block and add to free list
            char* block_ptr = (char*)pool->memory_block;

            for (size_t i = 0; i < initial_blocks; i++) {
                mcp_block_header_t* header = (mcp_block_header_t*)block_ptr;
                header->magic = MCP_POOL_MAGIC;
                header->pool = pool;
                header->next = pool->free_list;
                pool->free_list = header;

                block_ptr += pool->block_size;
            }

            pool->total_blocks = initial_blocks;
            pool->free_blocks = initial_blocks;

            mcp_log_debug("Memory pool created with %zu blocks of %zu bytes each (%zu user bytes)",
                         initial_blocks, pool->block_size, pool->user_block_size);
        } else {
            // Fall back to allocating blocks individually
            mcp_log_warn("Failed to allocate contiguous memory block, falling back to individual allocations");

            for (size_t i = 0; i < initial_blocks; i++) {
                void* block = malloc(pool->block_size);
                if (block) {
                    mcp_block_header_t* header = (mcp_block_header_t*)block;
                    header->magic = MCP_POOL_MAGIC;
                    header->pool = pool;
                    header->next = pool->free_list;
                    pool->free_list = header;

                    pool->total_blocks++;
                    pool->free_blocks++;
                } else {
                    mcp_log_error("Failed to allocate block %zu/%zu", i + 1, initial_blocks);
                    break;
                }
            }

            if (pool->total_blocks == 0) {
                mcp_log_error("Failed to allocate any blocks for memory pool");
                mcp_mutex_destroy(pool->lock);
                free(pool);
                return NULL;
            }

            mcp_log_debug("Memory pool created with %zu blocks of %zu bytes each (%zu user bytes)",
                         pool->total_blocks, pool->block_size, pool->user_block_size);
        }
    }

    return pool;
}

void mcp_memory_pool_destroy(mcp_memory_pool_t* pool) {
    if (!pool) return;

    mcp_mutex_lock(pool->lock);

    // If we allocated a contiguous memory block, free it
    if (pool->memory_block) {
        free(pool->memory_block);
    } else {
        // Otherwise, free each block individually
        mcp_block_header_t* current = pool->free_list;
        while (current) {
            mcp_block_header_t* next = current->next;
            free(current);
            current = next;
        }
    }

    mcp_mutex_unlock(pool->lock);
    mcp_mutex_destroy(pool->lock);

    free(pool);
}

void* mcp_memory_pool_alloc(mcp_memory_pool_t* pool) {
    if (!pool) return NULL;

    return allocate_from_pool(pool);
}

bool mcp_memory_pool_free(mcp_memory_pool_t* pool, void* block) {
    if (!pool || !block) return false;

    return return_to_pool(pool, block);
}

bool mcp_memory_pool_get_stats(mcp_memory_pool_t* pool, mcp_memory_pool_stats_t* stats) {
    if (!pool || !stats) return false;

    mcp_mutex_lock(pool->lock);

    stats->total_blocks = pool->total_blocks;
    stats->free_blocks = pool->free_blocks;
    stats->allocated_blocks = pool->total_blocks - pool->free_blocks;
    stats->block_size = pool->user_block_size;
    stats->total_memory = pool->total_blocks * pool->block_size;
    stats->peak_usage = pool->peak_usage;

    mcp_mutex_unlock(pool->lock);

    return true;
}

bool mcp_memory_pool_system_init(size_t small_initial, size_t medium_initial, size_t large_initial) {
    if (g_pools_initialized) {
        mcp_log_warn("Memory pool system already initialized");
        return true;
    }

    // Create the small object pool
    g_pools[MCP_POOL_SIZE_SMALL] = mcp_memory_pool_create(SMALL_BLOCK_SIZE, small_initial, 0);
    if (!g_pools[MCP_POOL_SIZE_SMALL]) {
        mcp_log_error("Failed to create small object pool");
        return false;
    }

    // Create the medium object pool
    g_pools[MCP_POOL_SIZE_MEDIUM] = mcp_memory_pool_create(MEDIUM_BLOCK_SIZE, medium_initial, 0);
    if (!g_pools[MCP_POOL_SIZE_MEDIUM]) {
        mcp_log_error("Failed to create medium object pool");
        mcp_memory_pool_destroy(g_pools[MCP_POOL_SIZE_SMALL]);
        g_pools[MCP_POOL_SIZE_SMALL] = NULL;
        return false;
    }

    // Create the large object pool
    g_pools[MCP_POOL_SIZE_LARGE] = mcp_memory_pool_create(LARGE_BLOCK_SIZE, large_initial, 0);
    if (!g_pools[MCP_POOL_SIZE_LARGE]) {
        mcp_log_error("Failed to create large object pool");
        mcp_memory_pool_destroy(g_pools[MCP_POOL_SIZE_SMALL]);
        mcp_memory_pool_destroy(g_pools[MCP_POOL_SIZE_MEDIUM]);
        g_pools[MCP_POOL_SIZE_SMALL] = NULL;
        g_pools[MCP_POOL_SIZE_MEDIUM] = NULL;
        return false;
    }

    g_pools_initialized = true;
    mcp_log_info("Memory pool system initialized");

    return true;
}

void mcp_memory_pool_system_cleanup(void) {
    if (!g_pools_initialized) {
        return;
    }

    // Destroy all pools
    for (int i = 0; i < MCP_POOL_SIZE_COUNT; i++) {
        if (g_pools[i]) {
            mcp_memory_pool_destroy(g_pools[i]);
            g_pools[i] = NULL;
        }
    }

    g_pools_initialized = false;
    mcp_log_info("Memory pool system cleaned up");
}

bool mcp_memory_pool_system_is_initialized(void) {
    return g_pools_initialized;
}

void* mcp_pool_alloc(size_t size) {
    if (size == 0) return NULL;

    // Check if we need to initialize the pool system
    if (!g_pools_initialized) {
        if (!mcp_memory_pool_system_init(64, 32, 16)) {
            // Fall back to malloc if initialization fails
            return malloc(size);
        }
    }

    // IMPORTANT: We've removed the thread cache allocation here to break the circular dependency
    // The thread cache will call mcp_pool_alloc, so we can't call back into the thread cache
    // This ensures we don't get into an infinite recursion

    // Determine the appropriate pool based on size
    mcp_pool_size_class_t size_class = get_size_class(size);

    // If the size is too large for our pools, fall back to malloc
    if (size_class == MCP_POOL_SIZE_COUNT) {
        return malloc(size);
    }

    // Allocate from the appropriate pool
    void* block = mcp_memory_pool_alloc(g_pools[size_class]);
    if (!block) {
        // Fall back to malloc if pool allocation fails
        return malloc(size);
    }

    return block;
}

void mcp_pool_free(void* ptr) {
    if (!ptr) return;

    // Check if this is a pool-allocated block by looking at the header
    mcp_block_header_t* header = (mcp_block_header_t*)((char*)ptr - sizeof(mcp_block_header_t));

    // If the magic value doesn't match, assume it was allocated with malloc
    if (header->magic != MCP_POOL_MAGIC) {
        free(ptr);
        return;
    }

    // Get the pool from the header
    mcp_memory_pool_t* pool = header->pool;

    // Return directly to the pool - no thread cache dependency
    return_to_pool(pool, ptr);
}

bool mcp_pool_get_stats(mcp_pool_size_class_t size_class, mcp_memory_pool_stats_t* stats) {
    if (size_class >= MCP_POOL_SIZE_COUNT || !stats || !g_pools_initialized) {
        return false;
    }

    return mcp_memory_pool_get_stats(g_pools[size_class], stats);
}

// Helper function to allocate a block from a pool
static void* allocate_from_pool(mcp_memory_pool_t* pool) {
    mcp_mutex_lock(pool->lock);

    // Check if we have a free block
    if (pool->free_list) {
        // Get a block from the free list
        mcp_block_header_t* header = pool->free_list;
        pool->free_list = header->next;
        pool->free_blocks--;

        // Update peak usage statistics
        size_t current_usage = pool->total_blocks - pool->free_blocks;
        if (current_usage > pool->peak_usage) {
            pool->peak_usage = current_usage;
        }

        mcp_mutex_unlock(pool->lock);

        // Return the user portion of the block (after the header)
        return (char*)header + sizeof(mcp_block_header_t);
    }

    // No free blocks, check if we can allocate more
    if (pool->max_blocks > 0 && pool->total_blocks >= pool->max_blocks) {
        // Pool is at capacity
        mcp_mutex_unlock(pool->lock);
        mcp_log_warn("Memory pool at capacity (%zu blocks)", pool->max_blocks);
        return NULL;
    }

    // Allocate a new block
    mcp_block_header_t* header = (mcp_block_header_t*)malloc(pool->block_size);
    if (!header) {
        mcp_mutex_unlock(pool->lock);
        mcp_log_error("Failed to allocate new block for memory pool");
        return NULL;
    }

    // Initialize the header
    header->magic = MCP_POOL_MAGIC;
    header->pool = pool;
    header->next = NULL;

    // Update pool statistics
    pool->total_blocks++;

    // Update peak usage statistics
    size_t current_usage = pool->total_blocks - pool->free_blocks;
    if (current_usage > pool->peak_usage) {
        pool->peak_usage = current_usage;
    }

    mcp_mutex_unlock(pool->lock);

    // Return the user portion of the block
    return (char*)header + sizeof(mcp_block_header_t);
}

// Helper function to return a block to a pool
static bool return_to_pool(mcp_memory_pool_t* pool, void* block) {
    if (!pool || !block) return false;

    // Get the header from the user block
    mcp_block_header_t* header = (mcp_block_header_t*)((char*)block - sizeof(mcp_block_header_t));

    // Verify that this block belongs to this pool
    if (header->magic != MCP_POOL_MAGIC || header->pool != pool) {
        mcp_log_error("Attempt to return invalid block to memory pool");
        return false;
    }

    mcp_mutex_lock(pool->lock);

    // Add the block to the free list
    header->next = pool->free_list;
    pool->free_list = header;
    pool->free_blocks++;

    mcp_mutex_unlock(pool->lock);

    return true;
}

// Helper function to determine the size class for a given size
static mcp_pool_size_class_t get_size_class(size_t size) {
    if (size <= SMALL_BLOCK_SIZE) {
        return MCP_POOL_SIZE_SMALL;
    } else if (size <= MEDIUM_BLOCK_SIZE) {
        return MCP_POOL_SIZE_MEDIUM;
    } else if (size <= LARGE_BLOCK_SIZE) {
        return MCP_POOL_SIZE_LARGE;
    } else {
        return MCP_POOL_SIZE_COUNT; // Indicates size is too large for pools
    }
}

// Helper function to get the block size for a given size class
static size_t get_block_size_for_class(mcp_pool_size_class_t size_class) {
    switch (size_class) {
        case MCP_POOL_SIZE_SMALL:
            return SMALL_BLOCK_SIZE;
        case MCP_POOL_SIZE_MEDIUM:
            return MEDIUM_BLOCK_SIZE;
        case MCP_POOL_SIZE_LARGE:
            return LARGE_BLOCK_SIZE;
        default:
            return 0;
    }
}

size_t mcp_pool_get_block_size(void* ptr) {
    if (!ptr) return 0;

    // Check if this is a pool-allocated block by looking at the header
    mcp_block_header_t* header = (mcp_block_header_t*)((char*)ptr - sizeof(mcp_block_header_t));

    // If the magic value doesn't match, it's not a pool-allocated block
    if (header->magic != MCP_POOL_MAGIC) {
        return 0;
    }

    // Get the pool from the header
    mcp_memory_pool_t* pool = header->pool;

    // Return the user block size
    return pool->user_block_size;
}
