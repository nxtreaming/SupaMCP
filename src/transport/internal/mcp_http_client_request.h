#ifndef MCP_HTTP_CLIENT_REQUEST_H
#define MCP_HTTP_CLIENT_REQUEST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// HTTP response structure
typedef struct {
    char* data;                  // Response data
    size_t size;                 // Response size
    int status_code;             // HTTP status code
    char* content_type;          // Content type
} http_response_t;

// Function declarations
http_response_t* http_post_request(const char* url, const char* content_type,
                                  const void* data, size_t size,
                                  const char* api_key, uint32_t timeout_ms);

void http_response_free(http_response_t* response);

#ifdef __cplusplus
}
#endif

#endif // MCP_HTTP_CLIENT_REQUEST_H
