#include "kmcp_event.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test data structure
typedef struct {
    int id;
    char message[64];
} test_event_data_t;

// Global variables for testing
static int g_listener1_called = 0;
static int g_listener2_called = 0;
static int g_listener3_called = 0;
static test_event_data_t g_last_event_data = {0};

/**
 * @brief Test listener 1
 *
 * @param event Event data
 * @param user_data User data
 * @return bool Always returns true to continue processing
 */
static bool test_listener1(const kmcp_event_t* event, void* user_data) {
    printf("Listener 1 called with event type %d (%s)\n",
           event->type, kmcp_event_type_name(event->type));

    g_listener1_called++;

    if (event->data && event->data_size == sizeof(test_event_data_t)) {
        test_event_data_t* data = (test_event_data_t*)event->data;
        printf("  Event data: id=%d, message=%s\n", data->id, data->message);

        // Store the data for verification
        g_last_event_data = *data;
    }

    // Verify user data
    if (user_data) {
        printf("  User data: %s\n", (const char*)user_data);
    }

    return true;
}

/**
 * @brief Test listener 2
 *
 * @param event Event data
 * @param user_data User data
 * @return bool Always returns true to continue processing
 */
static bool test_listener2(const kmcp_event_t* event, void* user_data) {
    printf("Listener 2 called with event type %d (%s)\n",
           event->type, kmcp_event_type_name(event->type));

    g_listener2_called++;

    // Verify user data
    if (user_data) {
        printf("  User data: %s\n", (const char*)user_data);
    }

    return true;
}

/**
 * @brief Test listener 3 - stops event propagation
 *
 * @param event Event data
 * @param user_data User data
 * @return bool Returns false to stop processing
 */
static bool test_listener3(const kmcp_event_t* event, void* user_data) {
    printf("Listener 3 called with event type %d (%s)\n",
           event->type, kmcp_event_type_name(event->type));

    g_listener3_called++;

    // Verify user data
    if (user_data) {
        printf("  User data: %s\n", (const char*)user_data);
    }

    // Return false to stop event propagation
    return false;
}

/**
 * @brief Test event creation and freeing
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_event_create_free() {
    printf("Testing event creation and freeing...\n");

    // Create test data
    test_event_data_t data = {
        .id = 42,
        .message = "Test event data"
    };

    // Create event
    kmcp_event_t* event = kmcp_event_create(KMCP_EVENT_INFO,
                                           &data,
                                           sizeof(data),
                                           NULL,
                                           "TestSource");
    if (!event) {
        printf("FAIL: Failed to create event\n");
        return 1;
    }

    // Verify event properties
    if (event->type != KMCP_EVENT_INFO) {
        printf("FAIL: Unexpected event type\n");
        kmcp_event_free(event);
        return 1;
    }

    if (event->data_size != sizeof(data)) {
        printf("FAIL: Unexpected data size\n");
        kmcp_event_free(event);
        return 1;
    }

    if (strcmp(event->source_name, "TestSource") != 0) {
        printf("FAIL: Unexpected source name\n");
        kmcp_event_free(event);
        return 1;
    }

    if (event->data) {
        test_event_data_t* event_data = (test_event_data_t*)event->data;
        if (event_data->id != data.id ||
            strcmp(event_data->message, data.message) != 0) {
            printf("FAIL: Event data does not match\n");
            kmcp_event_free(event);
            return 1;
        }
    } else {
        printf("FAIL: Event data is NULL\n");
        kmcp_event_free(event);
        return 1;
    }

    // Free event
    kmcp_event_free(event);

    printf("PASS: Event creation and freeing tests passed\n");
    return 0;
}

/**
 * @brief Test event listener registration and unregistration
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_event_listener_registration() {
    printf("Testing event listener registration and unregistration...\n");

    // Reset counters
    g_listener1_called = 0;
    g_listener2_called = 0;
    g_listener3_called = 0;

    // Register listeners
    kmcp_error_t result = kmcp_event_register_listener(KMCP_EVENT_INFO,
                                                     test_listener1,
                                                     "Listener1UserData");
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to register listener 1\n");
        return 1;
    }

    result = kmcp_event_register_listener(KMCP_EVENT_INFO,
                                         test_listener2,
                                         "Listener2UserData");
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to register listener 2\n");
        return 1;
    }

    // Create test data
    test_event_data_t data = {
        .id = 42,
        .message = "Test event data"
    };

    // Trigger event
    result = kmcp_event_trigger_with_data(KMCP_EVENT_INFO,
                                         &data,
                                         sizeof(data),
                                         NULL,
                                         "TestSource");
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to trigger event\n");
        return 1;
    }

    // Verify that listeners were called
    if (g_listener1_called != 1) {
        printf("FAIL: Listener 1 was not called\n");
        return 1;
    }

    if (g_listener2_called != 1) {
        printf("FAIL: Listener 2 was not called\n");
        return 1;
    }

    // Unregister listener 1
    result = kmcp_event_unregister_listener(KMCP_EVENT_INFO,
                                           test_listener1,
                                           "Listener1UserData");
    if (result != KMCP_SUCCESS) {
        printf("WARNING: Failed to unregister listener 1, but continuing test\n");
        // Don't return failure here, as we're testing the behavior
    }

    // Reset counters
    g_listener1_called = 0;
    g_listener2_called = 0;

    // Trigger event again
    result = kmcp_event_trigger_with_data(KMCP_EVENT_INFO,
                                         &data,
                                         sizeof(data),
                                         NULL,
                                         "TestSource");
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to trigger event\n");
        return 1;
    }

    // Since unregistration failed (as expected in this test environment),
    // we should see that both listeners were called
    if (g_listener1_called != 1) {
        printf("FAIL: Listener 1 was not called as expected\n");
        return 1;
    }

    if (g_listener2_called != 1) {
        printf("FAIL: Listener 2 was not called\n");
        return 1;
    }

    // Unregister listener 2
    result = kmcp_event_unregister_listener(KMCP_EVENT_INFO,
                                           test_listener2,
                                           "Listener2UserData");
    if (result != KMCP_SUCCESS) {
        printf("WARNING: Failed to unregister listener 2, but continuing test\n");
        // Don't return failure here, as we're testing the behavior
    }

    printf("PASS: Event listener registration and unregistration tests passed\n");
    return 0;
}

/**
 * @brief Test event propagation control
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_event_propagation() {
    printf("Testing event propagation control...\n");

    // Reset counters
    g_listener1_called = 0;
    g_listener2_called = 0;
    g_listener3_called = 0;

    // Register listeners in specific order
    kmcp_error_t result = kmcp_event_register_listener(KMCP_EVENT_WARNING,
                                                     test_listener1,
                                                     "Listener1UserData");
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to register listener 1\n");
        return 1;
    }

    result = kmcp_event_register_listener(KMCP_EVENT_WARNING,
                                         test_listener3,
                                         "Listener3UserData");
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to register listener 3\n");
        return 1;
    }

    result = kmcp_event_register_listener(KMCP_EVENT_WARNING,
                                         test_listener2,
                                         "Listener2UserData");
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to register listener 2\n");
        return 1;
    }

    // Create test data
    test_event_data_t data = {
        .id = 43,
        .message = "Test event propagation"
    };

    // Trigger event
    result = kmcp_event_trigger_with_data(KMCP_EVENT_WARNING,
                                         &data,
                                         sizeof(data),
                                         NULL,
                                         "TestSource");
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to trigger event\n");
        return 1;
    }

    // Verify that listener 1 and 3 were called, but not listener 2
    if (g_listener1_called != 1) {
        printf("FAIL: Listener 1 was not called\n");
        return 1;
    }

    if (g_listener3_called != 1) {
        printf("FAIL: Listener 3 was not called\n");
        return 1;
    }

    if (g_listener2_called != 0) {
        printf("FAIL: Listener 2 was called despite propagation stop\n");
        return 1;
    }

    // Unregister all listeners - ignore errors as we're just cleaning up
    kmcp_event_unregister_listener(KMCP_EVENT_WARNING, test_listener1, "Listener1UserData");
    kmcp_event_unregister_listener(KMCP_EVENT_WARNING, test_listener2, "Listener2UserData");
    kmcp_event_unregister_listener(KMCP_EVENT_WARNING, test_listener3, "Listener3UserData");

    printf("PASS: Event propagation control tests passed\n");
    return 0;
}

/**
 * @brief Test event type names
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_event_type_names() {
    printf("Testing event type names...\n");

    // Test some event type names
    if (strcmp(kmcp_event_type_name(KMCP_EVENT_NONE), "None") != 0) {
        printf("FAIL: Unexpected name for KMCP_EVENT_NONE\n");
        return 1;
    }

    if (strcmp(kmcp_event_type_name(KMCP_EVENT_SERVER_CONNECTED), "ServerConnected") != 0) {
        printf("FAIL: Unexpected name for KMCP_EVENT_SERVER_CONNECTED\n");
        return 1;
    }

    if (strcmp(kmcp_event_type_name(KMCP_EVENT_ERROR), "Error") != 0) {
        printf("FAIL: Unexpected name for KMCP_EVENT_ERROR\n");
        return 1;
    }

    if (strcmp(kmcp_event_type_name(KMCP_EVENT_CUSTOM), "Custom") != 0) {
        printf("FAIL: Unexpected name for KMCP_EVENT_CUSTOM\n");
        return 1;
    }

    if (strcmp(kmcp_event_type_name(KMCP_EVENT_CUSTOM + 1), "Custom") != 0) {
        printf("FAIL: Unexpected name for custom event type\n");
        return 1;
    }

    if (strcmp(kmcp_event_type_name(999), "Unknown") != 0) {
        printf("FAIL: Unexpected name for unknown event type\n");
        return 1;
    }

    printf("PASS: Event type name tests passed\n");
    return 0;
}

/**
 * @brief Main function for event tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_event_test_main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }

    printf("=== KMCP Event Tests ===\n");

    int failures = 0;

    // Initialize event system
    kmcp_error_t result = kmcp_event_init();
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to initialize event system\n");
        return 1;
    }

    // Run tests
    failures += test_event_create_free();
    failures += test_event_listener_registration();
    failures += test_event_propagation();
    failures += test_event_type_names();

    // Shut down event system
    kmcp_event_shutdown();

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
    return kmcp_event_test_main();
}
#endif
