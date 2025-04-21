#include "kmcp_test_utils.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Test fixture data structure
 */
typedef struct {
    int value;
    char* string;
} test_data_t;

/**
 * @brief Setup function for basic assertions test
 *
 * @param fixture Test fixture
 */
static void basic_assertions_setup(kmcp_test_fixture_t* fixture) {
    // Allocate test data
    test_data_t* data = (test_data_t*)KMCP_MEMORY_CONTEXT_CALLOC(
        fixture->context, 1, sizeof(test_data_t));

    // Initialize test data
    data->value = 42;
    data->string = KMCP_MEMORY_CONTEXT_STRDUP(fixture->context, "Hello, World!");

    // Store test data in fixture
    fixture->data = data;
}

/**
 * @brief Teardown function for basic assertions test
 *
 * @param fixture Test fixture
 */
static void basic_assertions_teardown(kmcp_test_fixture_t* fixture) {
    // No need to free memory, as it will be freed when the fixture is destroyed
}

/**
 * @brief Test basic assertions
 *
 * @param fixture Test fixture
 */
static void test_basic_assertions(kmcp_test_fixture_t* fixture) {
    // Get test data
    test_data_t* data = (test_data_t*)fixture->data;

    // Test assertions
    KMCP_TEST_ASSERT(fixture, true, "True should be true");
    KMCP_TEST_ASSERT(fixture, 1 == 1, "1 should equal 1");
    KMCP_TEST_ASSERT_INT_EQ(fixture, 42, data->value, "Value should be 42");
    KMCP_TEST_ASSERT_STR_EQ(fixture, "Hello, World!", data->string, "String should match");
    KMCP_TEST_ASSERT_NOT_NULL(fixture, data, "Data should not be NULL");
    KMCP_TEST_ASSERT_NULL(fixture, NULL, "NULL should be NULL");

    // Test error assertions
    kmcp_error_t success = KMCP_SUCCESS;
    kmcp_error_t error = KMCP_ERROR_INVALID_PARAMETER;

    KMCP_TEST_ASSERT_SUCCESS(fixture, success, "Success should be success");
    KMCP_TEST_ASSERT_ERROR(fixture, error, "Error should be error");
    KMCP_TEST_ASSERT_ERROR_EQ(fixture, KMCP_ERROR_INVALID_PARAMETER, error, "Error should match");
}

/**
 * @brief Setup function for memory management test
 *
 * @param fixture Test fixture
 */
static void memory_management_setup(kmcp_test_fixture_t* fixture) {
    // No setup needed
}

/**
 * @brief Teardown function for memory management test
 *
 * @param fixture Test fixture
 */
static void memory_management_teardown(kmcp_test_fixture_t* fixture) {
    // No teardown needed
}

/**
 * @brief Test memory management
 *
 * @param fixture Test fixture
 */
static void test_memory_management(kmcp_test_fixture_t* fixture) {
    // Get initial memory context statistics
    kmcp_memory_stats_t initial_stats;
    kmcp_error_t result = kmcp_memory_context_get_stats(fixture->context, &initial_stats);
    KMCP_TEST_ASSERT_SUCCESS(fixture, result, "Getting initial memory stats should succeed");

    // Allocate memory in the test context
    void* ptr = KMCP_MEMORY_CONTEXT_ALLOC(fixture->context, 100);
    KMCP_TEST_ASSERT_NOT_NULL(fixture, ptr, "Allocated memory should not be NULL");

    // Write to memory
    memset(ptr, 0xAA, 100);

    // Get memory context statistics after allocation
    kmcp_memory_stats_t after_alloc_stats;
    result = kmcp_memory_context_get_stats(fixture->context, &after_alloc_stats);

    // Test memory statistics (comparing with initial stats)
    KMCP_TEST_ASSERT_SUCCESS(fixture, result, "Getting memory stats should succeed");
    KMCP_TEST_ASSERT_INT_EQ(fixture, initial_stats.allocation_count + 1,
                           after_alloc_stats.allocation_count,
                           "Should have one more allocation");
    KMCP_TEST_ASSERT_INT_EQ(fixture, initial_stats.free_count,
                           after_alloc_stats.free_count,
                           "Free count should not change");
    KMCP_TEST_ASSERT_INT_EQ(fixture, initial_stats.active_allocations + 1,
                           after_alloc_stats.active_allocations,
                           "Should have one more active allocation");

    // Free memory
    kmcp_memory_context_free(fixture->context, ptr);

    // Get updated memory context statistics
    kmcp_memory_stats_t after_free_stats;
    result = kmcp_memory_context_get_stats(fixture->context, &after_free_stats);

    // Test updated memory statistics (comparing with after allocation stats)
    KMCP_TEST_ASSERT_SUCCESS(fixture, result, "Getting memory stats should succeed");
    KMCP_TEST_ASSERT_INT_EQ(fixture, after_alloc_stats.allocation_count,
                           after_free_stats.allocation_count,
                           "Allocation count should not change");
    KMCP_TEST_ASSERT_INT_EQ(fixture, after_alloc_stats.free_count + 1,
                           after_free_stats.free_count,
                           "Should have one more free");
    KMCP_TEST_ASSERT_INT_EQ(fixture, after_alloc_stats.active_allocations - 1,
                           after_free_stats.active_allocations,
                           "Should have one less active allocation");
}

/**
 * @brief Setup function for event system test
 *
 * @param fixture Test fixture
 */
static void event_system_setup(kmcp_test_fixture_t* fixture) {
    // Store event received flag in fixture data
    bool* event_received = (bool*)KMCP_MEMORY_CONTEXT_CALLOC(fixture->context, 1, sizeof(bool));
    *event_received = false;
    fixture->data = event_received;
}

/**
 * @brief Event listener for event system test
 *
 * @param event Event data
 * @param user_data User data
 * @return bool Returns true to continue processing
 */
static bool test_event_listener(const kmcp_event_t* event, void* user_data) {
    // Set event received flag
    bool* event_received = (bool*)user_data;
    *event_received = true;

    return true;
}

/**
 * @brief Teardown function for event system test
 *
 * @param fixture Test fixture
 */
static void event_system_teardown(kmcp_test_fixture_t* fixture) {
    // No teardown needed
}

/**
 * @brief Test event system
 *
 * @param fixture Test fixture
 */
static void test_event_system(kmcp_test_fixture_t* fixture) {
    // Get event received flag
    bool* event_received = (bool*)fixture->data;

    // Register event listener
    kmcp_error_t result = kmcp_event_register_listener(
        KMCP_EVENT_INFO, test_event_listener, event_received);
    KMCP_TEST_ASSERT_SUCCESS(fixture, result, "Registering event listener should succeed");

    // Trigger event
    result = kmcp_event_trigger_with_data(
        KMCP_EVENT_INFO, "Test event", 10, NULL, "TestSource");
    KMCP_TEST_ASSERT_SUCCESS(fixture, result, "Triggering event should succeed");

    // Test that event was received
    KMCP_TEST_ASSERT(fixture, *event_received, "Event should have been received");

    // Unregister event listener
    result = kmcp_event_unregister_listener(
        KMCP_EVENT_INFO, test_event_listener, event_received);
    KMCP_TEST_ASSERT_SUCCESS(fixture, result, "Unregistering event listener should succeed");
}

/**
 * @brief Setup function for error handling test
 *
 * @param fixture Test fixture
 */
static void error_handling_setup(kmcp_test_fixture_t* fixture) {
    // No setup needed
}

/**
 * @brief Teardown function for error handling test
 *
 * @param fixture Test fixture
 */
static void error_handling_teardown(kmcp_test_fixture_t* fixture) {
    // No teardown needed
}

/**
 * @brief Test error handling
 *
 * @param fixture Test fixture
 */
static void test_error_handling(kmcp_test_fixture_t* fixture) {
    // Create an error context
    kmcp_error_context_t* context = KMCP_ERROR_CONTEXT_CREATE(
        KMCP_ERROR_INVALID_PARAMETER, "Test error with value %d", 42);
    KMCP_TEST_ASSERT_NOT_NULL(fixture, context, "Error context should not be NULL");

    // Test error context properties
    KMCP_TEST_ASSERT_ERROR_EQ(fixture, KMCP_ERROR_INVALID_PARAMETER, context->error_code,
                             "Error code should match");
    KMCP_TEST_ASSERT_INT_EQ(fixture, KMCP_ERROR_CATEGORY_SYSTEM, context->category,
                           "Error category should match");
    KMCP_TEST_ASSERT_INT_EQ(fixture, KMCP_ERROR_SEVERITY_ERROR, context->severity,
                           "Error severity should match");

    // Create a nested error context
    kmcp_error_context_t* nested_context = KMCP_ERROR_CONTEXT_CREATE(
        KMCP_ERROR_MEMORY_ALLOCATION, "Nested error");
    KMCP_TEST_ASSERT_NOT_NULL(fixture, nested_context, "Nested error context should not be NULL");

    // Add nested error to the main context
    kmcp_error_context_add_nested(context, nested_context);

    // Test nested error context
    KMCP_TEST_ASSERT_NOT_NULL(fixture, context->next, "Nested error context should be added");
    KMCP_TEST_ASSERT_ERROR_EQ(fixture, KMCP_ERROR_MEMORY_ALLOCATION, context->next->error_code,
                             "Nested error code should match");

    // Format the error context
    char buffer[1024];
    size_t written = kmcp_error_context_format(context, buffer, sizeof(buffer));
    KMCP_TEST_ASSERT(fixture, written > 0, "Error context formatting should succeed");

    // Log the error context
    kmcp_error_context_log(context);

    // Free the error context
    kmcp_error_context_free(context);
}

/**
 * @brief Main function for test framework tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_test_framework_test_main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }

    printf("=== KMCP Test Framework Tests ===\n");

    int failures = 0;

    // Initialize test framework
    kmcp_error_t result = kmcp_test_init();
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to initialize test framework\n");
        return 1;
    }

    // Run tests
    failures += kmcp_test_run("Basic Assertions", test_basic_assertions,
                             basic_assertions_setup, basic_assertions_teardown);
    failures += kmcp_test_run("Memory Management", test_memory_management,
                             memory_management_setup, memory_management_teardown);
    failures += kmcp_test_run("Event System", test_event_system,
                             event_system_setup, event_system_teardown);
    failures += kmcp_test_run("Error Handling", test_error_handling,
                             error_handling_setup, error_handling_teardown);

    // Shut down test framework
    result = kmcp_test_shutdown();
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to shut down test framework\n");
        failures++;
    }

    // Print summary
    printf("\n=== Test Summary ===\n");
    if (failures == 0) {
        printf("All tests PASSED\n");
    } else {
        printf("%d tests FAILED\n", failures);
    }

    return failures;
}

#ifdef STANDALONE_TEST
/**
 * @brief Main function for standalone test
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int main() {
    return kmcp_test_framework_test_main();
}
#endif
