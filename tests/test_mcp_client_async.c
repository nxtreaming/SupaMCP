#include "unity.h"
#include "mcp_client.h"
#include "mock_transport.h"
#include "mcp_types.h"
#include "mcp_json_rpc.h"
#include <stdlib.h>
#include <string.h>

// Threading includes
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

// Helper function for cross-platform sleep
static void test_sleep_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}


// --- Test Globals / Setup / Teardown ---

static mcp_client_t* client = NULL;
static mcp_transport_t* current_mock_transport = NULL; // Keep a pointer accessible to tests

void setUp_client_async(void) {
    // Create mock transport
    current_mock_transport = mock_transport_create();
    TEST_ASSERT_NOT_NULL_MESSAGE(current_mock_transport, "Failed to create mock transport");

    // Create client with mock transport
    // Use a longer timeout for debugging threaded tests if needed
    mcp_client_config_t config = { .request_timeout_ms = 5000 };
    client = mcp_client_create(&config, current_mock_transport);
    TEST_ASSERT_NOT_NULL_MESSAGE(client, "Failed to create client with mock transport");
}

void tearDown_client_async(void) {
    mcp_client_destroy(client);
    client = NULL;
    current_mock_transport = NULL;
}

// --- Thread Function Data Structures ---

// Structure to pass data to/from the list_resources thread
typedef struct {
    mcp_client_t* client_ptr;
    mcp_resource_t*** resources_ptr; // Correct level of indirection
    size_t* count_ptr;
    int return_value;
} list_resources_thread_args_t;

// --- Thread Functions ---

// Thread function to call mcp_client_list_resources
#ifdef _WIN32
static DWORD WINAPI list_resources_thread_func(LPVOID lpParam) {
#else
static void* list_resources_thread_func(void* lpParam) {
#endif
    list_resources_thread_args_t* args = (list_resources_thread_args_t*)lpParam;
    args->return_value = mcp_client_list_resources(args->client_ptr, args->resources_ptr, args->count_ptr);
    return 0;
}


// --- Test Cases ---

// Test successful list_resources call using threads
void test_client_list_resources_success_threaded(void) {
    mcp_resource_t** resources = NULL;
    size_t count = 0;
    int client_call_result = -1; // Initialize to error

    // Prepare arguments for the thread
    list_resources_thread_args_t thread_args = {
        .client_ptr = client,
        .resources_ptr = &resources, // Pass address of the local variable
        .count_ptr = &count,
        .return_value = -1 // Initialize to error
    };

    // 1. Start the client call in a separate thread
#ifdef _WIN32
    HANDLE thread_handle = CreateThread(NULL, 0, list_resources_thread_func, &thread_args, 0, NULL);
    TEST_ASSERT_NOT_NULL(thread_handle);
#else
    pthread_t thread_id;
    int create_status = pthread_create(&thread_id, NULL, list_resources_thread_func, &thread_args);
    TEST_ASSERT_EQUAL_INT(0, create_status);
#endif

    // 2. Wait briefly for the client thread to likely send the request and start waiting
    test_sleep_ms(100); // Adjust sleep time if needed

    // 3. Check what the client sent (optional but good)
    size_t sent_size = 0;
    const uint8_t* sent_data = (const uint8_t*)mock_transport_get_last_sent_data(current_mock_transport, &sent_size);
    TEST_ASSERT_NOT_NULL_MESSAGE(sent_data, "Client should have sent data");
    // TODO: Parse sent_data to verify method ("list_resources") and ID (should be 1)

    // 4. Simulate receiving the correct response
    // Assuming first request ID is 1
    const char* mock_response_json = "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"resources\":[{\"uri\":\"res:/a\",\"name\":\"A\"}]}}";
    int sim_ret = mock_transport_simulate_receive(current_mock_transport, mock_response_json, strlen(mock_response_json)); // Don't include null terminator
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, sim_ret, "Simulating receive failed");

    // 5. Wait for the client thread to finish
#ifdef _WIN32
    if (thread_handle != NULL) { // Add check for static analysis
        WaitForSingleObject(thread_handle, INFINITE);
        CloseHandle(thread_handle);
    }
#else
    // pthread_join doesn't have the same issue with NULL handle (behavior is undefined)
    // but the create status check should prevent joining an invalid thread_id.
    pthread_join(thread_id, NULL);
#endif

    // 6. Check results stored by the thread
    client_call_result = thread_args.return_value;
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, client_call_result, "mcp_client_list_resources should have returned success");
    TEST_ASSERT_EQUAL_size_t(1, count);
    TEST_ASSERT_NOT_NULL(resources);
    if (resources) {
        TEST_ASSERT_NOT_NULL(resources[0]);
        if (resources[0]) {
            TEST_ASSERT_EQUAL_STRING("res:/a", resources[0]->uri);
            TEST_ASSERT_EQUAL_STRING("A", resources[0]->name);
        }
    }

    // 7. Cleanup
    mcp_free_resources(resources, count);
}


// --- Test Group Runner ---
// extern void test_client_list_resources_success(void); // Old non-threaded version
extern void test_client_list_resources_success_threaded(void);

void run_mcp_client_async_tests(void) {
    // RUN_TEST(test_client_list_resources_success); // Run the threaded version instead
    RUN_TEST(test_client_list_resources_success_threaded);
    // Add more tests later
}
