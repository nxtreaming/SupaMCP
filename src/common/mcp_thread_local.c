#include "mcp_thread_local.h"
#include "internal/arena_internal.h"
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
