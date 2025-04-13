#include "unity.h"

// Forward declarations for memory pool tests
extern void test_memory_pool_create_destroy(void);
extern void test_memory_pool_alloc_free(void);
extern void test_global_pool_alloc_free(void);
extern void test_thread_cache(void);
extern void test_memory_tracker(void);

// Runner function for memory pool tests
void run_memory_pool_tests(void) {
    RUN_TEST(test_memory_pool_create_destroy);
    RUN_TEST(test_memory_pool_alloc_free);
    RUN_TEST(test_global_pool_alloc_free);
    RUN_TEST(test_thread_cache);
    RUN_TEST(test_memory_tracker);
}
