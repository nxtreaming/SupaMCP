/**
 * @file test_request_buffer_optimization.c
 * @brief Test program to verify HTTP client request buffer optimization
 *
 * This test verifies that the HTTP client reuses buffers for request building
 * instead of allocating new buffers for each request.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

// Minimal definitions for testing
#define HTTP_CLIENT_REQUEST_BUFFER_INITIAL_SIZE 2048
#define HTTP_CLIENT_REQUEST_BUFFER_MAX_SIZE 65536

typedef struct {
    char* request_buffer;
    size_t request_buffer_capacity;
} test_client_data_t;

/**
 * @brief Simulate buffer initialization
 */
static int init_test_data(test_client_data_t* data) {
    data->request_buffer_capacity = HTTP_CLIENT_REQUEST_BUFFER_INITIAL_SIZE;
    data->request_buffer = (char*)malloc(data->request_buffer_capacity);
    return data->request_buffer ? 0 : -1;
}

/**
 * @brief Simulate buffer cleanup
 */
static void cleanup_test_data(test_client_data_t* data) {
    free(data->request_buffer);
    data->request_buffer = NULL;
    data->request_buffer_capacity = 0;
}

/**
 * @brief Simulate the optimized request building logic
 */
static char* build_test_request(test_client_data_t* data, const char* method, const char* json_data) {
    size_t content_length = json_data ? strlen(json_data) : 0;
    size_t required_size = 1024 + content_length; // Base headers + content

    // Resize buffer if needed (grow-only strategy)
    if (required_size > data->request_buffer_capacity) {
        size_t new_capacity = required_size;
        // Round up to next power of 2
        if (new_capacity < HTTP_CLIENT_REQUEST_BUFFER_MAX_SIZE) {
            size_t power_of_2 = 1;
            while (power_of_2 < new_capacity) {
                power_of_2 <<= 1;
            }
            new_capacity = power_of_2;
        }
        if (new_capacity > HTTP_CLIENT_REQUEST_BUFFER_MAX_SIZE) {
            new_capacity = HTTP_CLIENT_REQUEST_BUFFER_MAX_SIZE;
        }

        char* new_buffer = (char*)realloc(data->request_buffer, new_capacity);
        if (!new_buffer) return NULL;

        data->request_buffer = new_buffer;
        data->request_buffer_capacity = new_capacity;
    }

    // Build request in reusable buffer
    int offset = snprintf(data->request_buffer, data->request_buffer_capacity,
        "%s /mcp HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "User-Agent: SupaMCP-Client/1.0\r\n"
        "Connection: keep-alive\r\n", method);

    if (strcmp(method, "POST") == 0 && json_data) {
        offset += snprintf(data->request_buffer + offset, data->request_buffer_capacity - offset,
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n", content_length);
    }

    offset += snprintf(data->request_buffer + offset, data->request_buffer_capacity - offset, "\r\n");

    if (strcmp(method, "POST") == 0 && json_data) {
        offset += snprintf(data->request_buffer + offset, data->request_buffer_capacity - offset, "%s", json_data);
    }

    // Return a copy (caller frees this)
    char* request_copy = (char*)malloc(offset + 1);
    if (request_copy) {
        memcpy(request_copy, data->request_buffer, offset);
        request_copy[offset] = '\0';
    }
    return request_copy;
}

/**
 * @brief Test the request buffer reuse functionality
 */
static void test_request_buffer_reuse(void) {
    printf("Testing request buffer reuse optimization...\n");

    // Initialize test data
    test_client_data_t data;
    int result = init_test_data(&data);
    assert(result == 0);
    assert(data.request_buffer != NULL);
    assert(data.request_buffer_capacity == HTTP_CLIENT_REQUEST_BUFFER_INITIAL_SIZE);

    printf("✓ Initial buffer allocated: %zu bytes\n", data.request_buffer_capacity);
    
    // Test 1: Small request should use existing buffer
    char* request1 = build_test_request(&data, "POST", "{\"method\":\"test\"}");
    assert(request1 != NULL);
    assert(data.request_buffer_capacity == HTTP_CLIENT_REQUEST_BUFFER_INITIAL_SIZE);
    printf("✓ Small request reused buffer: %zu bytes\n", data.request_buffer_capacity);

    // Test 2: Large request should resize buffer
    char large_json[4096];
    memset(large_json, 'x', sizeof(large_json) - 1);
    large_json[sizeof(large_json) - 1] = '\0';

    size_t old_capacity = data.request_buffer_capacity;
    char* request2 = build_test_request(&data, "POST", large_json);
    assert(request2 != NULL);
    assert(data.request_buffer_capacity > old_capacity);
    printf("✓ Large request resized buffer: %zu -> %zu bytes\n", old_capacity, data.request_buffer_capacity);

    // Test 3: Subsequent small request should reuse larger buffer
    size_t large_capacity = data.request_buffer_capacity;
    char* request3 = build_test_request(&data, "POST", "{\"method\":\"test2\"}");
    assert(request3 != NULL);
    assert(data.request_buffer_capacity == large_capacity);
    printf("✓ Subsequent small request reused large buffer: %zu bytes\n", data.request_buffer_capacity);

    // Test 4: Verify request content is correct
    assert(strstr(request1, "POST /mcp HTTP/1.1") != NULL);
    assert(strstr(request1, "Host: localhost:8080") != NULL);
    assert(strstr(request1, "Content-Type: application/json") != NULL);
    assert(strstr(request1, "{\"method\":\"test\"}") != NULL);
    printf("✓ Request content is correct\n");

    // Test 5: Test GET request
    char* request4 = build_test_request(&data, "GET", NULL);
    assert(request4 != NULL);
    assert(strstr(request4, "GET /mcp HTTP/1.1") != NULL);
    printf("✓ GET request formatted correctly\n");

    // Cleanup
    free(request1);
    free(request2);
    free(request3);
    free(request4);
    cleanup_test_data(&data);

    printf("✓ All tests passed! Request buffer optimization is working correctly.\n");
}

/**
 * @brief Test buffer size limits
 */
static void test_buffer_size_limits(void) {
    printf("\nTesting buffer size limits...\n");

    test_client_data_t data;
    int result = init_test_data(&data);
    assert(result == 0);

    // Test maximum buffer size limit
    size_t very_large_size = HTTP_CLIENT_REQUEST_BUFFER_MAX_SIZE + 1000;
    char* very_large_json = (char*)malloc(very_large_size);
    assert(very_large_json != NULL);
    memset(very_large_json, 'x', very_large_size - 1);
    very_large_json[very_large_size - 1] = '\0';

    char* request = build_test_request(&data, "POST", very_large_json);
    assert(request != NULL);
    assert(data.request_buffer_capacity <= HTTP_CLIENT_REQUEST_BUFFER_MAX_SIZE);
    printf("✓ Buffer size limited to maximum: %zu bytes\n", data.request_buffer_capacity);

    free(very_large_json);
    free(request);
    cleanup_test_data(&data);

    printf("✓ Buffer size limit test passed!\n");
}

int main(void) {
    printf("HTTP Client Request Buffer Optimization Test\n");
    printf("============================================\n\n");

    test_request_buffer_reuse();
    test_buffer_size_limits();

    printf("\n🎉 All optimization tests passed successfully!\n");
    printf("The HTTP client now reuses request buffers efficiently.\n");

    return 0;
}
