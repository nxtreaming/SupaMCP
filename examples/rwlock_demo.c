/**
 * @file rwlock_demo.c
 * @brief Demonstrates the use of read-write locks
 */
#include "mcp_rwlock.h"
#include "mcp_log.h"
#include "mcp_thread_pool.h"
#include "mcp_sys_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Shared resource protected by the read-write lock
typedef struct {
    mcp_rwlock_t* rwlock;
    int value;
    int read_count;
    int write_count;
} shared_resource_t;

// Task arguments for reader and writer tasks
typedef struct {
    shared_resource_t* resource;
    int id;
    int iterations;
    int sleep_ms;
} task_args_t;

// Function to simulate some work (sleep for a while)
void simulate_work(int ms) {
    mcp_sleep_ms(ms);
}

// Reader task function
void reader_task(void* arg) {
    task_args_t* args = (task_args_t*)arg;
    shared_resource_t* resource = args->resource;
    int id = args->id;
    int iterations = args->iterations;
    int sleep_ms = args->sleep_ms;

    for (int i = 0; i < iterations; i++) {
        // Acquire read lock
        if (!mcp_rwlock_read_lock(resource->rwlock)) {
            mcp_log_error("Reader %d failed to acquire read lock", id);
            continue;
        }

        // Read the shared resource
        int value = resource->value;
        resource->read_count++;

        mcp_log_info("Reader %d read value: %d (read count: %d)",
                    id, value, resource->read_count);

        // Simulate some work while holding the read lock
        simulate_work(sleep_ms);

        // Release read lock
        if (!mcp_rwlock_read_unlock(resource->rwlock)) {
            mcp_log_error("Reader %d failed to release read lock", id);
        }

        // Simulate some work between operations
        simulate_work(rand() % 10);
    }

    free(args);
}

// Writer task function
void writer_task(void* arg) {
    task_args_t* args = (task_args_t*)arg;
    shared_resource_t* resource = args->resource;
    int id = args->id;
    int iterations = args->iterations;
    int sleep_ms = args->sleep_ms;

    for (int i = 0; i < iterations; i++) {
        // Acquire write lock
        if (!mcp_rwlock_write_lock(resource->rwlock)) {
            mcp_log_error("Writer %d failed to acquire write lock", id);
            continue;
        }

        // Modify the shared resource
        resource->value++;
        resource->write_count++;

        mcp_log_info("Writer %d updated value to: %d (write count: %d)",
                    id, resource->value, resource->write_count);

        // Simulate some work while holding the write lock
        simulate_work(sleep_ms);

        // Release write lock
        if (!mcp_rwlock_write_unlock(resource->rwlock)) {
            mcp_log_error("Writer %d failed to release write lock", id);
        }

        // Simulate some work between operations
        simulate_work(rand() % 20);
    }

    free(args);
}

// Function to create and submit a reader task
void submit_reader(mcp_thread_pool_t* pool, shared_resource_t* resource,
                  int id, int iterations, int sleep_ms) {
    task_args_t* args = (task_args_t*)malloc(sizeof(task_args_t));
    if (!args) {
        mcp_log_error("Failed to allocate memory for reader task arguments");
        return;
    }

    args->resource = resource;
    args->id = id;
    args->iterations = iterations;
    args->sleep_ms = sleep_ms;

    if (mcp_thread_pool_add_task(pool, reader_task, args) != 0) {
        mcp_log_error("Failed to submit reader task to thread pool");
        free(args);
    }
}

// Function to create and submit a writer task
void submit_writer(mcp_thread_pool_t* pool, shared_resource_t* resource,
                  int id, int iterations, int sleep_ms) {
    task_args_t* args = (task_args_t*)malloc(sizeof(task_args_t));
    if (!args) {
        mcp_log_error("Failed to allocate memory for writer task arguments");
        return;
    }

    args->resource = resource;
    args->id = id;
    args->iterations = iterations;
    args->sleep_ms = sleep_ms;

    if (mcp_thread_pool_add_task(pool, writer_task, args) != 0) {
        mcp_log_error("Failed to submit writer task to thread pool");
        free(args);
    }
}

int main(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);
    mcp_log_info("Read-Write Lock Demo starting");

    // Seed random number generator
    srand((unsigned int)time(NULL));

    // Create shared resource
    shared_resource_t resource = {0};

    // Create read-write lock
    resource.rwlock = mcp_rwlock_create();
    if (!resource.rwlock) {
        mcp_log_error("Failed to create read-write lock");
        return 1;
    }

    // Create thread pool
    mcp_thread_pool_t* pool = mcp_thread_pool_create(8, 100);
    if (!pool) {
        mcp_log_error("Failed to create thread pool");
        mcp_rwlock_destroy(resource.rwlock);
        free(resource.rwlock);
        return 1;
    }

    printf("Read-Write Lock Demo\n");
    printf("====================\n\n");

    printf("This demo demonstrates the use of read-write locks to protect a shared resource.\n");
    printf("Multiple readers can access the resource simultaneously, but writers need exclusive access.\n\n");

    printf("Starting readers and writers...\n\n");

    // Submit reader and writer tasks
    for (int i = 0; i < 5; i++) {
        submit_reader(pool, &resource, i, 10, 5);
    }

    for (int i = 0; i < 2; i++) {
        submit_writer(pool, &resource, i, 5, 10);
    }

    // Wait for all tasks to complete
    printf("Waiting for all tasks to complete...\n");
    mcp_thread_pool_wait(pool, 5000);

    // Print final statistics
    printf("\nFinal Statistics:\n");
    printf("  Value: %d\n", resource.value);
    printf("  Read operations: %d\n", resource.read_count);
    printf("  Write operations: %d\n", resource.write_count);

    // Clean up
    mcp_thread_pool_destroy(pool);
    mcp_rwlock_free(resource.rwlock);

    mcp_log_info("Read-Write Lock Demo completed");

    return 0;
}
