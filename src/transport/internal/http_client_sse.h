#ifndef HTTP_CLIENT_SSE_H
#define HTTP_CLIENT_SSE_H

#include "mcp_transport.h"
#include "mcp_socket_utils.h"
#include "mcp_http_sse_common.h"
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration of transport data structure
typedef struct http_client_transport_data http_client_transport_data_t;

socket_t connect_to_sse_endpoint(http_client_transport_data_t* data);
void process_sse_event(http_client_transport_data_t* data, const sse_event_t* event);
sse_event_t* sse_event_create(const char* id, const char* event, const char* data);
void sse_event_free(sse_event_t* event);
void* http_client_event_thread_func(void* arg);

#ifdef __cplusplus
}
#endif

#endif // HTTP_CLIENT_SSE_H
