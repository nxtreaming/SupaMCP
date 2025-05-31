#include "mcp_thread_pool.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Simple test task
void test_task(void* arg) {
    int* task_id = (int*)arg;
    printf("Executing task %d on thread\n", *task_id);
    
    // Simulate some work
#ifdef _WIN32
    Sleep(100);  // 100ms
#else
    usleep(100000);  // 100ms
#endif
    
    printf("Task %d completed\n", *task_id);
    free(task_id);
}

int main() {
    printf("=== Thread Pool Exit Enhancement Test ===\n");
    
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    
    // Test 1: Create thread pool with optimal thread count
    printf("\n1. Creating thread pool with optimal thread count...\n");
    size_t optimal_threads = mcp_get_optimal_thread_count();
    printf("Optimal thread count: %zu\n", optimal_threads);
    
    mcp_thread_pool_t* pool = mcp_thread_pool_create(optimal_threads, 100);
    if (!pool) {
        printf("Failed to create thread pool\n");
        return 1;
    }
    
    printf("Thread pool created with %zu threads\n", mcp_thread_pool_get_thread_count(pool));
    
    // Test 2: Add some tasks
    printf("\n2. Adding tasks to thread pool...\n");
    for (int i = 0; i < 10; i++) {
        int* task_id = malloc(sizeof(int));
        *task_id = i;
        
        if (mcp_thread_pool_add_task(pool, test_task, task_id) != 0) {
            printf("Failed to add task %d\n", i);
            free(task_id);
        }
    }
    
    // Wait a bit for tasks to start
#ifdef _WIN32
    Sleep(500);
#else
    usleep(500000);
#endif
    
    // Test 3: Resize pool down (test explicit exit signaling)
    printf("\n3. Testing pool shrinking (explicit exit signaling)...\n");
    size_t new_size = optimal_threads > 2 ? optimal_threads - 2 : 2;
    printf("Shrinking pool from %zu to %zu threads...\n", 
           mcp_thread_pool_get_thread_count(pool), new_size);
    
    if (mcp_thread_pool_resize(pool, new_size) == 0) {
        printf("Pool successfully resized to %zu threads\n", mcp_thread_pool_get_thread_count(pool));
    } else {
        printf("Failed to resize pool\n");
    }
    
    // Wait for resize to complete
#ifdef _WIN32
    Sleep(1000);
#else
    usleep(1000000);
#endif
    
    // Test 4: Resize pool up
    printf("\n4. Testing pool expansion...\n");
    size_t expand_size = new_size + 3;
    printf("Expanding pool from %zu to %zu threads...\n", 
           mcp_thread_pool_get_thread_count(pool), expand_size);
    
    if (mcp_thread_pool_resize(pool, expand_size) == 0) {
        printf("Pool successfully expanded to %zu threads\n", mcp_thread_pool_get_thread_count(pool));
    } else {
        printf("Failed to expand pool\n");
    }
    
    // Test 5: Add more tasks to test new threads
    printf("\n5. Adding more tasks to test new threads...\n");
    for (int i = 10; i < 20; i++) {
        int* task_id = malloc(sizeof(int));
        *task_id = i;
        
        if (mcp_thread_pool_add_task(pool, test_task, task_id) != 0) {
            printf("Failed to add task %d\n", i);
            free(task_id);
        }
    }
    
    // Test 6: Auto-adjust
    printf("\n6. Testing basic auto-adjustment...\n");
    if (mcp_thread_pool_auto_adjust(pool) == 0) {
        printf("Pool auto-adjusted to %zu threads\n", mcp_thread_pool_get_thread_count(pool));
    } else {
        printf("Failed to auto-adjust pool\n");
    }

    // Test 7: Smart adjustment
    printf("\n7. Testing smart adjustment...\n");
    if (mcp_thread_pool_smart_adjust(pool, NULL) == 0) {
        printf("Pool smart-adjusted to %zu threads\n", mcp_thread_pool_get_thread_count(pool));
    } else {
        printf("Failed to smart-adjust pool\n");
    }
    
    // Wait for all tasks to complete
    printf("\n8. Waiting for tasks to complete...\n");
    if (mcp_thread_pool_wait(pool, 5000) == 0) {
        printf("All tasks completed successfully\n");
    } else {
        printf("Timeout waiting for tasks to complete\n");
    }

    // Test 8: Get final statistics
    printf("\n9. Final statistics:\n");
    size_t submitted, completed, failed, active;
    if (mcp_thread_pool_get_stats(pool, &submitted, &completed, &failed, &active) == 0) {
        printf("  Submitted: %zu\n", submitted);
        printf("  Completed: %zu\n", completed);
        printf("  Failed: %zu\n", failed);
        printf("  Active: %zu\n", active);
    }
    
    // Test 9: Cleanup
    printf("\n10. Destroying thread pool...\n");
    if (mcp_thread_pool_destroy(pool) == 0) {
        printf("Thread pool destroyed successfully\n");
    } else {
        printf("Failed to destroy thread pool\n");
    }
    
    printf("\n=== Test completed ===\n");
    return 0;
}
