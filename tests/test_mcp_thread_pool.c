#include "unity.h"
#include "mcp_thread_pool.h"
// #include <stdatomic.h> // Remove C11 atomics
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#define THREAD_HANDLE HANDLE
#define THREAD_FUNC_RET DWORD WINAPI
#define THREAD_FUNC_ARGS LPVOID
#define THREAD_CREATE(handle, func, arg) \
    handle = CreateThread(NULL, 0, func, arg, 0, NULL); \
    TEST_ASSERT_NOT_NULL(handle)
#define THREAD_JOIN(handle) \
    WaitForSingleObject(handle, INFINITE); \
    CloseHandle(handle)
#define SLEEP_MS(ms) Sleep(ms)
// Mutex for test variables
CRITICAL_SECTION g_test_mutex;
#define TEST_MUTEX_INIT() InitializeCriticalSection(&g_test_mutex)
#define TEST_MUTEX_DESTROY() DeleteCriticalSection(&g_test_mutex)
#define TEST_MUTEX_LOCK() EnterCriticalSection(&g_test_mutex)
#define TEST_MUTEX_UNLOCK() LeaveCriticalSection(&g_test_mutex)
#else
#include <pthread.h>
#include <unistd.h> // For usleep
#define THREAD_HANDLE pthread_t
#define THREAD_FUNC_RET void*
#define THREAD_FUNC_ARGS void*
#define THREAD_CREATE(handle, func, arg) \
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&handle, NULL, func, arg))
#define THREAD_JOIN(handle) \
    pthread_join(handle, NULL)
#define SLEEP_MS(ms) usleep(ms * 1000)
// Mutex for test variables
pthread_mutex_t g_test_mutex = PTHREAD_MUTEX_INITIALIZER;
#define TEST_MUTEX_INIT() pthread_mutex_init(&g_test_mutex, NULL) // Re-init just in case
#define TEST_MUTEX_DESTROY() pthread_mutex_destroy(&g_test_mutex)
#define TEST_MUTEX_LOCK() pthread_mutex_lock(&g_test_mutex)
#define TEST_MUTEX_UNLOCK() pthread_mutex_unlock(&g_test_mutex)
#endif

// --- Test Globals ---
// Replace atomics with regular types protected by mutex
static int task_counter = 0;
static int task_execution_count = 0;
static bool task_started_global = false;
static bool allow_task_finish_global = false;

#define NUM_TASKS 100
#define NUM_THREADS 4
#define QUEUE_SIZE (NUM_TASKS * 2) // Make queue large enough for tests

// --- Task Functions ---
void simple_task(void* arg) {
    // Simulate work
    SLEEP_MS(10 + (rand() % 20)); // Sleep 10-30 ms

    TEST_MUTEX_LOCK();
    task_counter += (int)(intptr_t)arg;
    task_execution_count++;
    TEST_MUTEX_UNLOCK();
}

// Task that blocks until signaled
void blocking_task(void* arg) {
    (void)arg;
    bool should_finish = false;

    TEST_MUTEX_LOCK();
    task_started_global = true;
    TEST_MUTEX_UNLOCK();

    while (!should_finish) {
        TEST_MUTEX_LOCK();
        should_finish = allow_task_finish_global;
        TEST_MUTEX_UNLOCK();
        if (!should_finish) {
            SLEEP_MS(10); // Wait outside the lock
        }
    }

    TEST_MUTEX_LOCK();
    task_execution_count++;
    TEST_MUTEX_UNLOCK();
}

// --- Test Setup/Teardown ---
// Removed setUp and tearDown as they conflict with other tests when linked together.
// Mutex initialization/destruction will be handled in the run function.


// --- Test Cases ---

void test_thread_pool_create_destroy(void) {
    mcp_thread_pool_t* pool = mcp_thread_pool_create(NUM_THREADS, QUEUE_SIZE);
    TEST_ASSERT_NOT_NULL(pool);
    mcp_thread_pool_destroy(pool);
}

void test_thread_pool_create_invalid(void) {
    mcp_thread_pool_t* pool_zero_threads = mcp_thread_pool_create(0, QUEUE_SIZE); // Zero threads
    TEST_ASSERT_NULL(pool_zero_threads);
    mcp_thread_pool_t* pool_zero_queue = mcp_thread_pool_create(NUM_THREADS, 0); // Zero queue size
    TEST_ASSERT_NULL(pool_zero_queue);
}

void test_thread_pool_submit_tasks(void) {
    mcp_thread_pool_t* pool = mcp_thread_pool_create(NUM_THREADS, QUEUE_SIZE);
    TEST_ASSERT_NOT_NULL(pool);

    int expected_sum = 0;
    for (int i = 1; i <= NUM_TASKS; ++i) {
        expected_sum += i;
        // Pass 'i' as the argument
        TEST_ASSERT_EQUAL_INT(0, mcp_thread_pool_add_task(pool, simple_task, (void*)(intptr_t)i));
    }

    // Wait for tasks to complete - destroy waits implicitly
    mcp_thread_pool_destroy(pool);

    // Verify all tasks were executed and the counter is correct (access protected)
    TEST_MUTEX_LOCK();
    TEST_ASSERT_EQUAL_INT(NUM_TASKS, task_execution_count);
    TEST_ASSERT_EQUAL_INT(expected_sum, task_counter);
    TEST_MUTEX_UNLOCK();
}

void test_thread_pool_submit_after_destroy_start(void) {
    mcp_thread_pool_t* pool = mcp_thread_pool_create(NUM_THREADS, QUEUE_SIZE);
    TEST_ASSERT_NOT_NULL(pool);

    // Submit one task
    TEST_ASSERT_EQUAL_INT(0, mcp_thread_pool_add_task(pool, simple_task, (void*)1));

    // Start destroying the pool (signals threads to stop accepting new tasks)
    // Note: mcp_thread_pool_destroy initiates shutdown and waits.
    // We need a way to test submitting *during* shutdown if the API supported it,
    // but the current destroy likely blocks until the queue is empty.
    // So, we test submitting *after* destroy has been called.

    // Destroying the pool waits for submitted tasks
    mcp_thread_pool_destroy(pool);
    pool = NULL; // Pool is invalid now

    // Attempt to submit after destroy (should ideally fail gracefully)
    // This requires the pool pointer to be NULL or an internal state check.
    // Since we don't have the pool pointer anymore, this test isn't feasible
    // without modifying the destroy function or adding a specific shutdown function.

    // For now, just verify the first task ran.
    TEST_MUTEX_LOCK();
    TEST_ASSERT_EQUAL_INT(1, task_execution_count);
    TEST_ASSERT_EQUAL_INT(1, task_counter);
    TEST_MUTEX_UNLOCK();

    // If we had a non-blocking shutdown:
    // mcp_thread_pool_shutdown(pool); // Signal shutdown
    // TEST_ASSERT_NOT_EQUAL(0, mcp_thread_pool_add_task(pool, simple_task, (void*)2)); // Should fail
    // mcp_thread_pool_wait(pool); // Wait for completion
    // mcp_thread_pool_destroy(pool);
}

// Test submitting tasks when the queue is full
void test_thread_pool_queue_full(void) {
    size_t small_queue_size = 2;
    mcp_thread_pool_t* pool = mcp_thread_pool_create(1, small_queue_size); // 1 thread, small queue
    TEST_ASSERT_NOT_NULL(pool);

    // Reset global state for blocking task (already done in setUp)

    // Submit the blocking task (will likely start running)
    TEST_ASSERT_EQUAL_INT(0, mcp_thread_pool_add_task(pool, blocking_task, NULL));

    // Wait briefly for the task to start
    bool started = false;
    int wait_count = 0;
    while (!started && wait_count < 100) { // Add timeout
        TEST_MUTEX_LOCK();
        started = task_started_global;
        TEST_MUTEX_UNLOCK();
        if (!started) {
            SLEEP_MS(5);
            wait_count++;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(started, "Blocking task did not start in time");

    // Submit tasks to fill the queue
    for (size_t i = 0; i < small_queue_size; ++i) {
        TEST_ASSERT_EQUAL_INT(0, mcp_thread_pool_add_task(pool, simple_task, (void*)(intptr_t)(i + 1)));
    }

    // Submit one more task - should fail because the queue is full
    TEST_ASSERT_NOT_EQUAL(0, mcp_thread_pool_add_task(pool, simple_task, (void*)99));

    // Allow the blocking task to finish
    TEST_MUTEX_LOCK();
    allow_task_finish_global = true;
    TEST_MUTEX_UNLOCK();

    // Destroy the pool (waits for all tasks)
    mcp_thread_pool_destroy(pool);

    // Verify counts (1 blocking task + small_queue_size simple tasks)
    TEST_MUTEX_LOCK();
    TEST_ASSERT_EQUAL_INT(1 + small_queue_size, task_execution_count);
    TEST_MUTEX_UNLOCK();
}


// --- Test Group Runner ---
void run_mcp_thread_pool_tests(void) {
    // Initialize mutex for this test group
    TEST_MUTEX_INIT();

    // Run tests
    RUN_TEST(test_thread_pool_create_destroy);
    RUN_TEST(test_thread_pool_create_invalid);
    RUN_TEST(test_thread_pool_submit_tasks);
    RUN_TEST(test_thread_pool_queue_full);
    // RUN_TEST(test_thread_pool_submit_after_destroy_start); // Re-enable if shutdown mechanism allows testing this

    // Destroy mutex for this test group
    TEST_MUTEX_DESTROY();
}
