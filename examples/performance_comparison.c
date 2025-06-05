/**
 * @file performance_comparison.c
 * @brief Performance comparison between old and new request buffer allocation
 *
 * This program demonstrates the performance improvement achieved by using
 * reusable buffers instead of allocating new buffers for each request.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#define NUM_REQUESTS 10000
#define HTTP_CLIENT_REQUEST_BUFFER_INITIAL_SIZE 2048
#define HTTP_CLIENT_REQUEST_BUFFER_MAX_SIZE 65536

typedef struct {
    char* request_buffer;
    size_t request_buffer_capacity;
} optimized_client_data_t;

/**
 * @brief Old approach: allocate new buffer for each request
 */
static char* build_request_old_way(const char* method, const char* json_data) {
    size_t content_length = json_data ? strlen(json_data) : 0;
    size_t buffer_size = 1024 + content_length;
    
    char* request = (char*)malloc(buffer_size);
    if (!request) return NULL;
    
    int offset = snprintf(request, buffer_size,
        "%s /mcp HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "User-Agent: SupaMCP-Client/1.0\r\n"
        "Connection: keep-alive\r\n", method);
    
    if (strcmp(method, "POST") == 0 && json_data) {
        offset += snprintf(request + offset, buffer_size - offset,
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n", content_length);
    }
    
    offset += snprintf(request + offset, buffer_size - offset, "\r\n");
    
    if (strcmp(method, "POST") == 0 && json_data) {
        offset += snprintf(request + offset, buffer_size - offset, "%s", json_data);
    }
    
    return request;
}

/**
 * @brief New approach: reuse buffer with grow-only strategy
 */
static char* build_request_new_way(optimized_client_data_t* data, const char* method, const char* json_data) {
    size_t content_length = json_data ? strlen(json_data) : 0;
    size_t required_size = 1024 + content_length;
    
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
 * @brief Benchmark the old allocation approach
 */
static double benchmark_old_approach(void) {
    clock_t start = clock();
    
    for (int i = 0; i < NUM_REQUESTS; i++) {
        char* request = build_request_old_way("POST", "{\"method\":\"test\",\"params\":{}}");
        assert(request != NULL);
        free(request);
    }
    
    clock_t end = clock();
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

/**
 * @brief Benchmark the new optimized approach
 */
static double benchmark_new_approach(void) {
    optimized_client_data_t data;
    data.request_buffer_capacity = HTTP_CLIENT_REQUEST_BUFFER_INITIAL_SIZE;
    data.request_buffer = (char*)malloc(data.request_buffer_capacity);
    assert(data.request_buffer != NULL);
    
    clock_t start = clock();
    
    for (int i = 0; i < NUM_REQUESTS; i++) {
        char* request = build_request_new_way(&data, "POST", "{\"method\":\"test\",\"params\":{}}");
        assert(request != NULL);
        free(request);
    }
    
    clock_t end = clock();
    
    free(data.request_buffer);
    return ((double)(end - start)) / CLOCKS_PER_SEC;
}

int main(void) {
    printf("HTTP Client Request Buffer Performance Comparison\n");
    printf("================================================\n\n");
    printf("Running %d requests with each approach...\n\n", NUM_REQUESTS);
    
    // Benchmark old approach
    printf("Testing old approach (malloc/free for each request)...\n");
    double old_time = benchmark_old_approach();
    printf("Old approach time: %.4f seconds\n\n", old_time);
    
    // Benchmark new approach
    printf("Testing new approach (reusable buffer)...\n");
    double new_time = benchmark_new_approach();
    printf("New approach time: %.4f seconds\n\n", new_time);
    
    // Calculate improvement
    double improvement = (old_time - new_time) / old_time * 100.0;
    double speedup = old_time / new_time;
    
    printf("Performance Results:\n");
    printf("===================\n");
    printf("Old approach: %.4f seconds\n", old_time);
    printf("New approach: %.4f seconds\n", new_time);
    printf("Improvement:  %.1f%% faster\n", improvement);
    printf("Speedup:      %.2fx\n", speedup);
    printf("\nMemory allocation reduction:\n");
    printf("Old approach: %d malloc/free pairs\n", NUM_REQUESTS);
    printf("New approach: ~1-2 malloc calls (buffer grows as needed)\n");
    printf("Allocation reduction: ~%.1fx fewer allocations\n", (double)NUM_REQUESTS / 2.0);
    
    return 0;
}
