#include "mcp_arena.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Define thread_local for MSVC if not using C11 or later
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
#define thread_local __declspec(thread)
#endif
static DWORD tls_arena_key = TLS_OUT_OF_INDEXES;
static bool tls_key_initialized = false;
// Need a function to initialize the key, potentially using DllMain or a startup function
// For simplicity here, we might initialize lazily, but that requires locking.
// A dedicated init function called early is safer. Let's assume one exists or add it.
// We also need a way to destroy arenas on thread exit. TlsAlloc doesn't do this automatically.
#else // POSIX
#include <pthread.h>
static pthread_key_t tls_arena_key;
static pthread_once_t tls_key_once = PTHREAD_ONCE_INIT;
// Destructor function for pthread TLS key
static void destroy_thread_arena(void* arena_ptr) {
    if (arena_ptr) {
        mcp_arena_destroy((mcp_arena_t*)arena_ptr);
        free(arena_ptr); // Free the arena struct itself allocated in get_current
    }
}
// Function to create the key exactly once
static void make_tls_key() {
    pthread_key_create(&tls_arena_key, destroy_thread_arena);
}
#endif

/**
 * @internal
 * @brief Allocates a new memory block for the arena.
 * The allocated size includes space for the block header and the requested data size.
 * @param size The minimum required size for the data area of the block.
 * @return Pointer to the newly allocated block, or NULL on failure.
 */
static mcp_arena_block_t* mcp_arena_new_block(size_t size) {
    // Check for potential integer overflow before calculating total size
    if (SIZE_MAX - sizeof(mcp_arena_block_t) < size) {
        return NULL; // Requested size too large, would overflow
    }
    // Calculate total size needed: header + data area
    // Note: sizeof(mcp_arena_block_t) already includes the 'char data[1]'
    // so we need size bytes *in addition* to the header structure.
    size_t total_size = sizeof(mcp_arena_block_t) + size;
    mcp_arena_block_t* block = (mcp_arena_block_t*)malloc(total_size);
    if (block == NULL) {
        return NULL; // Allocation failed
    }
    // Initialize block header
    block->next = NULL;
    block->size = size; // Store the usable data size
    block->used = 0;
    // Note: block->data is implicitly the memory right after the header fields
    return block;
}


// --- Internal Core Allocation Logic ---

/**
 * @internal
 * @brief Core logic for allocating memory from a specific arena instance.
 * @param arena Pointer to the arena instance to allocate from.
 * @param size The number of bytes to allocate (already aligned).
 * @return Pointer to the allocated memory, or NULL on failure.
 */
static void* _mcp_arena_alloc_internal(mcp_arena_t* arena, size_t aligned_size) {
     // 1. Try allocating from the current block if it exists and has enough space
    if (arena->current_block != NULL &&
        (arena->current_block->size - arena->current_block->used) >= aligned_size)
    {
        void* ptr = arena->current_block->data + arena->current_block->used;
        arena->current_block->used += aligned_size;
        return ptr;
    }

    // 2. Need a new block
    size_t new_block_data_size = (aligned_size > arena->default_block_size) ? aligned_size : arena->default_block_size;
    mcp_arena_block_t* new_block = mcp_arena_new_block(new_block_data_size);
    if (new_block == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to allocate new block for arena (size: %zu)", new_block_data_size);
        return NULL; // Failed to allocate a new block
    }

    void* ptr = new_block->data;
    new_block->used = aligned_size;
    new_block->next = arena->current_block;
    arena->current_block = new_block;

    return ptr;
}


// --- Public API Implementation ---

// Initialize a manually managed arena
void mcp_arena_init(mcp_arena_t* arena, size_t default_block_size) {
    if (arena == NULL) {
        return; // Invalid argument
    }
    // Initialize arena fields
    arena->current_block = NULL; // No blocks allocated yet
    // Set the default block size, using the defined default if 0 is provided
    arena->default_block_size = (default_block_size > 0) ? default_block_size : MCP_ARENA_DEFAULT_BLOCK_SIZE;
}

void mcp_arena_destroy(mcp_arena_t* arena) {
    if (arena == NULL) {
        return; // Invalid argument
    }
    // Traverse the linked list of blocks
    mcp_arena_block_t* current = arena->current_block;
    while (current != NULL) {
        mcp_arena_block_t* next = current->next; // Store pointer to next block
        free(current);                           // Free the current block
        current = next;                          // Move to the next block
    }
    // Reset arena state to indicate it's empty/destroyed
    arena->current_block = NULL;
    // arena->default_block_size remains unchanged
    // Ensure the arena is marked as empty if manually managed
    memset(arena, 0, sizeof(mcp_arena_t));
}

// Reset a manually managed arena
void mcp_arena_reset(mcp_arena_t* arena) {
    if (arena == NULL) {
        return; // Invalid argument
    }
    // Traverse the linked list of blocks
    mcp_arena_block_t* current = arena->current_block;
    while (current != NULL) {
        // Reset the used counter for each block, making its memory available again
        current->used = 0;
        current = current->next;
    }
    // Note: This keeps the allocated blocks in memory for faster reuse.
    // If memory footprint needs to be minimized immediately, mcp_arena_destroy
    // should be used instead.
}


// --- Thread-Local API ---

// Global initialization for Windows TLS key (Needs careful handling in practice)
#ifdef _WIN32
static void initialize_tls_key_win() {
    if (!tls_key_initialized) {
        tls_arena_key = TlsAlloc();
        if (tls_arena_key == TLS_OUT_OF_INDEXES) {
             log_message(LOG_LEVEL_ERROR, "Failed to allocate TLS key for arena.");
             // Handle error appropriately - maybe exit?
        } else {
            tls_key_initialized = true;
        }
        // NOTE: Need a corresponding TlsFree, typically at process exit (e.g., DllMain DLL_PROCESS_DETACH)
    }
}
#endif


mcp_arena_t* mcp_arena_get_current(void) {
#ifdef _WIN32
    // Ensure key is initialized (simplistic lazy init - potential race condition without lock)
    if (!tls_key_initialized) initialize_tls_key_win();
    if (tls_arena_key == TLS_OUT_OF_INDEXES) return NULL;

    mcp_arena_t* arena = (mcp_arena_t*)TlsGetValue(tls_arena_key);
    if (arena == NULL) {
        // First call in this thread, create and store arena
        arena = (mcp_arena_t*)malloc(sizeof(mcp_arena_t));
        if (arena == NULL) {
            log_message(LOG_LEVEL_ERROR, "Failed to allocate memory for thread-local arena struct.");
            return NULL;
        }
        mcp_arena_init(arena, 0); // Use default block size
        if (!TlsSetValue(tls_arena_key, arena)) {
            log_message(LOG_LEVEL_ERROR, "Failed to set TLS value for arena.");
            free(arena); // Clean up allocated struct
            return NULL;
        }
        // NOTE: Windows TLS requires manual cleanup on thread exit.
        // This is often done by iterating threads or via DllMain DLL_THREAD_DETACH.
    }
    return arena;
#else // POSIX
    // Ensure the key is created exactly once across all threads
    pthread_once(&tls_key_once, make_tls_key);

    mcp_arena_t* arena = (mcp_arena_t*)pthread_getspecific(tls_arena_key);
    if (arena == NULL) {
        // First call in this thread, create and store arena
        arena = (mcp_arena_t*)malloc(sizeof(mcp_arena_t));
        if (arena == NULL) {
             log_message(LOG_LEVEL_ERROR, "Failed to allocate memory for thread-local arena struct.");
            return NULL;
        }
        mcp_arena_init(arena, 0); // Use default block size
        if (pthread_setspecific(tls_arena_key, arena) != 0) {
            log_message(LOG_LEVEL_ERROR, "Failed to set thread-specific value for arena.");
            free(arena); // Clean up allocated struct
            return NULL;
        }
        // pthreads key destructor (destroy_thread_arena) handles cleanup automatically
    }
    return arena;
#endif
}

// Allocate using the current thread's arena
void* mcp_arena_alloc(size_t size) {
     if (size == 0) {
        return NULL;
    }
     // Check for potential integer overflow before alignment calculation
    if (SIZE_MAX - (sizeof(void*) - 1) < size) {
         log_message(LOG_LEVEL_WARN, "Requested arena allocation size too large for alignment: %zu", size);
         return NULL;
    }
    size_t aligned_size = MCP_ARENA_ALIGN_UP(size);

    mcp_arena_t* arena = mcp_arena_get_current();
    if (arena == NULL) {
        log_message(LOG_LEVEL_ERROR, "Failed to get or create thread-local arena for allocation.");
        return NULL; // Failed to get/create thread's arena
    }

    return _mcp_arena_alloc_internal(arena, aligned_size);
}

// Reset the current thread's arena
void mcp_arena_reset_current_thread(void) {
#ifdef _WIN32
    if (!tls_key_initialized || tls_arena_key == TLS_OUT_OF_INDEXES) return;
    mcp_arena_t* arena = (mcp_arena_t*)TlsGetValue(tls_arena_key);
    if (arena) {
        mcp_arena_reset(arena);
    }
#else
    pthread_once(&tls_key_once, make_tls_key); // Ensure key exists
    mcp_arena_t* arena = (mcp_arena_t*)pthread_getspecific(tls_arena_key);
    if (arena) {
        mcp_arena_reset(arena);
    }
#endif
}

// Destroy the current thread's arena (MUST be called by thread before exit)
void mcp_arena_destroy_current_thread(void) {
#ifdef _WIN32
    if (!tls_key_initialized || tls_arena_key == TLS_OUT_OF_INDEXES) return;
    mcp_arena_t* arena = (mcp_arena_t*)TlsGetValue(tls_arena_key);
    if (arena) {
        mcp_arena_destroy(arena);
        free(arena); // Free the arena struct itself
        TlsSetValue(tls_arena_key, NULL); // Clear the TLS slot
    }
    // Note: TlsFree(tls_arena_key) should happen at process exit.
#else
    // For pthreads, the key destructor handles cleanup automatically when
    // pthread_setspecific is called with a non-NULL value.
    // Calling getspecific doesn't prevent the destructor.
    // We might manually trigger cleanup if needed before thread exit for some reason,
    // but typically it's automatic. Let's retrieve and clear manually for consistency.
    pthread_once(&tls_key_once, make_tls_key); // Ensure key exists
    mcp_arena_t* arena = (mcp_arena_t*)pthread_getspecific(tls_arena_key);
     if (arena) {
         // Manually call destructor logic (optional, as key destructor should handle it)
         // destroy_thread_arena(arena);
         // Clear the value to prevent double free by key destructor if called manually
         pthread_setspecific(tls_arena_key, NULL);
         // Explicitly destroy and free if not relying solely on key destructor
         mcp_arena_destroy(arena);
         free(arena);
     }
#endif
}
