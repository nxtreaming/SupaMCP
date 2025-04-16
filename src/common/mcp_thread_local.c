#include "mcp_thread_local.h"
#include "internal/arena_internal.h"
#include "mcp_log.h"
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
static DWORD arena_tls_index = TLS_OUT_OF_INDEXES;
#else
#include <pthread.h>
static pthread_key_t arena_key;
static pthread_once_t key_once = PTHREAD_ONCE_INIT;
#endif

// Forward declarations
static void cleanup_arena(void* arena);
#ifndef _WIN32
static void make_key(void);
#endif

int mcp_arena_init_current_thread(size_t initial_size) {
#ifdef _WIN32
    if (arena_tls_index == TLS_OUT_OF_INDEXES) {
        arena_tls_index = TlsAlloc();
        if (arena_tls_index == TLS_OUT_OF_INDEXES) {
            return -1;
        }
    }
#else
    pthread_once(&key_once, make_key);
#endif

    mcp_arena_t* arena = (mcp_arena_t*)malloc(sizeof(mcp_arena_t));
    if (!arena) {
        return -1;
    }

    mcp_arena_init(arena, initial_size);
    if (!arena->default_block_size) {
        free(arena);
        return -1;
    }

#ifdef _WIN32
    if (!TlsSetValue(arena_tls_index, arena)) {
        cleanup_arena(arena);
        return -1;
    }
#else
    if (pthread_setspecific(arena_key, arena) != 0) {
        cleanup_arena(arena);
        return -1;
    }
#endif

    return 0;
}

mcp_arena_t* mcp_arena_get_current(void) {
#ifdef _WIN32
    if (arena_tls_index == TLS_OUT_OF_INDEXES) {
        return NULL;
    }
    return (mcp_arena_t*)TlsGetValue(arena_tls_index);
#else
    return (mcp_arena_t*)pthread_getspecific(arena_key);
#endif
}

void mcp_arena_reset_current_thread(void) {
    mcp_arena_t* arena = mcp_arena_get_current();
    if (arena) {
        mcp_arena_reset(arena);
    }
}

void mcp_arena_destroy_current_thread(void) {
    mcp_arena_t* arena = mcp_arena_get_current();
    if (arena) {
        cleanup_arena(arena);
#ifdef _WIN32
        TlsSetValue(arena_tls_index, NULL);
#else
        pthread_setspecific(arena_key, NULL);
#endif
    }
}

static void cleanup_arena(void* arena) {
    if (arena) {
        mcp_arena_cleanup((mcp_arena_t*)arena);
        free(arena);
    }
}

#ifndef _WIN32
static void make_key(void) {
    pthread_key_create(&arena_key, cleanup_arena);
}
#endif

/* Thread-local object cache implementation */

bool mcp_thread_cache_init_current_thread(void) {
    return mcp_object_cache_system_init();
}

bool mcp_thread_cache_init_type(mcp_object_cache_type_t type, const mcp_object_cache_config_t* config) {
    if (!mcp_object_cache_system_is_initialized()) {
        if (!mcp_thread_cache_init_current_thread()) {
            return false;
        }
    }

    return mcp_object_cache_init(type, config);
}

void* mcp_thread_cache_alloc_object(mcp_object_cache_type_t type, size_t size) {
    if (!mcp_object_cache_system_is_initialized()) {
        mcp_log_warn("Thread-local object cache system not initialized");
        return NULL;
    }

    return mcp_object_cache_alloc(type, size);
}

void mcp_thread_cache_free_object(mcp_object_cache_type_t type, void* ptr, size_t size) {
    if (!ptr) return;

    if (!mcp_object_cache_system_is_initialized()) {
        mcp_log_warn("Thread-local object cache system not initialized");
        free(ptr); // Fall back to free to avoid memory leaks
        return;
    }

    mcp_object_cache_free(type, ptr, size);
}

bool mcp_thread_cache_get_object_stats(mcp_object_cache_type_t type, mcp_object_cache_stats_t* stats) {
    if (!mcp_object_cache_system_is_initialized()) {
        return false;
    }

    return mcp_object_cache_get_stats(type, stats);
}

void mcp_thread_cache_flush_object_cache(mcp_object_cache_type_t type) {
    if (!mcp_object_cache_system_is_initialized()) {
        return;
    }

    mcp_object_cache_flush(type);
}

void mcp_thread_cache_cleanup_current_thread(void) {
    if (!mcp_object_cache_system_is_initialized()) {
        return;
    }

    mcp_object_cache_system_shutdown();
}
