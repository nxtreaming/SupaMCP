#include "mcp_object_cache.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Define a custom string object
typedef struct {
    char* data;
    size_t length;
} string_object_t;

// Constructor for string objects
void string_constructor(void* ptr) {
    string_object_t* str = (string_object_t*)ptr;
    str->data = NULL;
    str->length = 0;
}

// Destructor for string objects
void string_destructor(void* ptr) {
    string_object_t* str = (string_object_t*)ptr;
    if (str->data) {
        free(str->data);
        str->data = NULL;
    }
    str->length = 0;
}

// Set string value
void string_set(string_object_t* str, const char* value) {
    if (str->data) {
        free(str->data);
    }
    
    if (value) {
        str->length = strlen(value);
        str->data = (char*)malloc(str->length + 1);
        strcpy(str->data, value);
    } else {
        str->data = NULL;
        str->length = 0;
    }
}

// Print cache statistics
void print_cache_stats(mcp_object_cache_type_t type) {
    mcp_object_cache_stats_t stats;
    if (mcp_object_cache_get_stats(type, &stats)) {
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
    mcp_log_info("Object cache test started");
    
    // Initialize object cache system
    if (!mcp_object_cache_system_init()) {
        mcp_log_error("Failed to initialize object cache system");
        return 1;
    }
    
    // Register string object type
    if (!mcp_object_cache_register_type(MCP_OBJECT_CACHE_STRING, 
                                       string_constructor, 
                                       string_destructor)) {
        mcp_log_error("Failed to register string object type");
        return 1;
    }
    
    // Configure string object cache with adaptive sizing
    mcp_object_cache_config_t config = {
        .max_size = 16,
        .adaptive_sizing = true,
        .growth_threshold = 0.8,
        .shrink_threshold = 0.3,
        .min_cache_size = 4,
        .max_cache_size = 32,
        .constructor = string_constructor,
        .destructor = string_destructor
    };
    
    if (!mcp_object_cache_init(MCP_OBJECT_CACHE_STRING, &config)) {
        mcp_log_error("Failed to initialize string object cache");
        return 1;
    }
    
    // Test string object cache
    mcp_log_info("Testing string object cache...");
    
    // Allocate and free string objects
    const int num_iterations = 1000;
    string_object_t* strings[100] = {NULL};
    
    // Seed random number generator
    srand((unsigned int)time(NULL));
    
    for (int i = 0; i < num_iterations; i++) {
        // Randomly allocate or free string objects
        int index = rand() % 100;
        
        if (strings[index] == NULL) {
            // Allocate a new string object
            strings[index] = (string_object_t*)mcp_object_cache_alloc(
                MCP_OBJECT_CACHE_STRING, sizeof(string_object_t));
            
            if (strings[index]) {
                // Set a random string value
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "String %d", rand() % 1000);
                string_set(strings[index], buffer);
            }
        } else {
            // Free the string object
            mcp_object_cache_free(MCP_OBJECT_CACHE_STRING, strings[index], sizeof(string_object_t));
            strings[index] = NULL;
        }
        
        // Periodically print cache statistics
        if ((i + 1) % 200 == 0) {
            printf("\nAfter %d iterations:\n", i + 1);
            print_cache_stats(MCP_OBJECT_CACHE_STRING);
        }
    }
    
    // Free any remaining string objects
    for (int i = 0; i < 100; i++) {
        if (strings[i]) {
            mcp_object_cache_free(MCP_OBJECT_CACHE_STRING, strings[i], sizeof(string_object_t));
            strings[i] = NULL;
        }
    }
    
    // Print final cache statistics
    printf("\nFinal cache statistics:\n");
    print_cache_stats(MCP_OBJECT_CACHE_STRING);
    
    // Test generic object cache
    mcp_log_info("Testing generic object cache...");
    
    // Configure generic object cache
    config.max_size = 8;
    config.adaptive_sizing = false;
    config.constructor = NULL;
    config.destructor = NULL;
    
    if (!mcp_object_cache_init(MCP_OBJECT_CACHE_GENERIC, &config)) {
        mcp_log_error("Failed to initialize generic object cache");
        return 1;
    }
    
    // Allocate and free generic objects
    void* objects[20] = {NULL};
    
    for (int i = 0; i < 50; i++) {
        int index = rand() % 20;
        
        if (objects[index] == NULL) {
            // Allocate a new object
            size_t size = 64 + (rand() % 64);
            objects[index] = mcp_object_cache_alloc(MCP_OBJECT_CACHE_GENERIC, size);
            
            if (objects[index]) {
                // Fill with some data
                memset(objects[index], (char)(i & 0xFF), size);
            }
        } else {
            // Free the object
            mcp_object_cache_free(MCP_OBJECT_CACHE_GENERIC, objects[index], 0);
            objects[index] = NULL;
        }
    }
    
    // Free any remaining objects
    for (int i = 0; i < 20; i++) {
        if (objects[i]) {
            mcp_object_cache_free(MCP_OBJECT_CACHE_GENERIC, objects[i], 0);
            objects[i] = NULL;
        }
    }
    
    // Print generic cache statistics
    printf("\nGeneric cache statistics:\n");
    print_cache_stats(MCP_OBJECT_CACHE_GENERIC);
    
    // Flush all caches
    mcp_log_info("Flushing all caches...");
    mcp_object_cache_flush(MCP_OBJECT_CACHE_STRING);
    mcp_object_cache_flush(MCP_OBJECT_CACHE_GENERIC);
    
    // Print cache statistics after flush
    printf("\nCache statistics after flush:\n");
    print_cache_stats(MCP_OBJECT_CACHE_STRING);
    print_cache_stats(MCP_OBJECT_CACHE_GENERIC);
    
    // Shutdown object cache system
    mcp_object_cache_system_shutdown();
    mcp_log_info("Object cache test completed");
    mcp_log_close();
    
    return 0;
}
