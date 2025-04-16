#include "mcp_thread_local.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Define a custom buffer object
typedef struct {
    char* data;
    size_t size;
    size_t capacity;
} buffer_t;

// Constructor for buffer objects
void buffer_constructor(void* ptr) {
    buffer_t* buf = (buffer_t*)ptr;
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

// Destructor for buffer objects
void buffer_destructor(void* ptr) {
    buffer_t* buf = (buffer_t*)ptr;
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    buf->size = 0;
    buf->capacity = 0;
}

// Initialize buffer with capacity
void buffer_init(buffer_t* buf, size_t capacity) {
    if (buf->data) {
        free(buf->data);
    }
    
    buf->data = (char*)malloc(capacity);
    if (buf->data) {
        buf->capacity = capacity;
        buf->size = 0;
    } else {
        buf->capacity = 0;
        buf->size = 0;
    }
}

// Append data to buffer
void buffer_append(buffer_t* buf, const char* data, size_t len) {
    if (!buf->data || buf->size + len > buf->capacity) {
        size_t new_capacity = buf->capacity == 0 ? 64 : buf->capacity * 2;
        while (new_capacity < buf->size + len) {
            new_capacity *= 2;
        }
        
        char* new_data = (char*)realloc(buf->data, new_capacity);
        if (!new_data) {
            return;
        }
        
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
    
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
}

// Print cache statistics
void print_cache_stats(mcp_object_cache_type_t type) {
    mcp_object_cache_stats_t stats;
    if (mcp_thread_cache_get_object_stats(type, &stats)) {
        printf("Cache stats for %s:\n", mcp_object_cache_type_name(type));
        printf("  Count: %zu / %zu\n", stats.cache_count, stats.max_size);
        printf("  Hits: %zu, Misses: %zu, Hit ratio: %.2f%%\n", 
               stats.cache_hits, stats.cache_misses, stats.hit_ratio * 100.0);
        printf("  Adaptive sizing: %s\n", stats.adaptive_sizing ? "enabled" : "disabled");
        printf("  Flushes: %zu\n", stats.cache_flushes);
    } else {
        printf("Failed to get cache stats for %s\n", mcp_object_cache_type_name(type));
    }
}

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    mcp_log_info("Thread cache test started");
    
    // Initialize thread-local object cache
    if (!mcp_thread_cache_init_current_thread()) {
        mcp_log_error("Failed to initialize thread-local object cache");
        return 1;
    }
    
    // Register buffer object type
    if (!mcp_object_cache_register_type(MCP_OBJECT_CACHE_BUFFER, 
                                       buffer_constructor, 
                                       buffer_destructor)) {
        mcp_log_error("Failed to register buffer object type");
        return 1;
    }
    
    // Configure buffer object cache with adaptive sizing
    mcp_object_cache_config_t config = {
        .max_size = 16,
        .adaptive_sizing = true,
        .growth_threshold = 0.8,
        .shrink_threshold = 0.3,
        .min_cache_size = 4,
        .max_cache_size = 32,
        .constructor = buffer_constructor,
        .destructor = buffer_destructor
    };
    
    if (!mcp_thread_cache_init_type(MCP_OBJECT_CACHE_BUFFER, &config)) {
        mcp_log_error("Failed to initialize buffer object cache");
        return 1;
    }
    
    // Test buffer object cache
    mcp_log_info("Testing buffer object cache...");
    
    // Allocate and free buffer objects
    const int num_iterations = 1000;
    buffer_t* buffers[100] = {NULL};
    
    // Seed random number generator
    srand((unsigned int)time(NULL));
    
    for (int i = 0; i < num_iterations; i++) {
        // Randomly allocate or free buffer objects
        int index = rand() % 100;
        
        if (buffers[index] == NULL) {
            // Allocate a new buffer object
            buffers[index] = (buffer_t*)mcp_thread_cache_alloc_object(
                MCP_OBJECT_CACHE_BUFFER, sizeof(buffer_t));
            
            if (buffers[index]) {
                // Initialize with random capacity
                size_t capacity = 64 + (rand() % 256);
                buffer_init(buffers[index], capacity);
                
                // Add some random data
                char data[64];
                snprintf(data, sizeof(data), "Buffer %d", rand() % 1000);
                buffer_append(buffers[index], data, strlen(data));
            }
        } else {
            // Free the buffer object
            mcp_thread_cache_free_object(MCP_OBJECT_CACHE_BUFFER, buffers[index], sizeof(buffer_t));
            buffers[index] = NULL;
        }
        
        // Periodically print cache statistics
        if ((i + 1) % 200 == 0) {
            printf("\nAfter %d iterations:\n", i + 1);
            print_cache_stats(MCP_OBJECT_CACHE_BUFFER);
        }
    }
    
    // Free any remaining buffer objects
    for (int i = 0; i < 100; i++) {
        if (buffers[i]) {
            mcp_thread_cache_free_object(MCP_OBJECT_CACHE_BUFFER, buffers[i], sizeof(buffer_t));
            buffers[i] = NULL;
        }
    }
    
    // Print final cache statistics
    printf("\nFinal cache statistics:\n");
    print_cache_stats(MCP_OBJECT_CACHE_BUFFER);
    
    // Test arena allocation with thread-local arena
    mcp_log_info("Testing thread-local arena...");
    
    // Initialize thread-local arena
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }
    
    // Allocate some memory from the thread-local arena
    mcp_arena_t* arena = mcp_arena_get_current();
    if (!arena) {
        mcp_log_error("Failed to get thread-local arena");
        return 1;
    }
    
    // Allocate and use memory from the arena
    for (int i = 0; i < 10; i++) {
        size_t size = 32 + (rand() % 128);
        void* ptr = mcp_arena_alloc(arena, size);
        if (ptr) {
            // Fill with some data
            memset(ptr, (char)(i & 0xFF), size);
            printf("Allocated %zu bytes from thread-local arena\n", size);
        } else {
            mcp_log_error("Failed to allocate memory from thread-local arena");
        }
    }
    
    // Reset the arena
    mcp_arena_reset_current_thread();
    mcp_log_info("Thread-local arena reset");
    
    // Allocate more memory after reset
    for (int i = 0; i < 5; i++) {
        size_t size = 64 + (rand() % 256);
        void* ptr = mcp_arena_alloc(arena, size);
        if (ptr) {
            // Fill with some data
            memset(ptr, (char)(i & 0xFF), size);
            printf("Allocated %zu bytes from thread-local arena after reset\n", size);
        } else {
            mcp_log_error("Failed to allocate memory from thread-local arena after reset");
        }
    }
    
    // Clean up
    mcp_arena_destroy_current_thread();
    mcp_thread_cache_cleanup_current_thread();
    mcp_log_info("Thread cache test completed");
    mcp_log_close();
    
    return 0;
}
