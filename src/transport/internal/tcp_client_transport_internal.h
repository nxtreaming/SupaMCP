#ifndef MCP_TCP_CLIENT_TRANSPORT_INTERNAL_H
#define MCP_TCP_CLIENT_TRANSPORT_INTERNAL_H

// Include the centralized socket utilities first
#include "mcp_socket_utils.h"

// Now include other project headers and standard libraries
#include "mcp_tcp_client_transport.h"
#include "transport_internal.h" // Include base internal transport header
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_buffer_pool.h"
#include "mcp_sync.h"
#include <mcp_thread_pool.h> // Include for mcp_thread_t
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>


// 全局变量，用于控制重连逻辑
extern bool reconnection_in_progress;


// Constants (can reuse from other internal headers if appropriate, but define here for clarity)
#define MAX_MCP_MESSAGE_SIZE (1024 * 1024) // Example: 1MB limit
#define POOL_BUFFER_SIZE (1024 * 8) // 8KB buffer size
#define POOL_NUM_BUFFERS 16         // Number of buffers in the pool

// --- Internal Structures ---

// Forward declare transport struct
typedef struct mcp_transport mcp_transport_t;

// Forward declarations for reconnection functions
void* tcp_client_reconnect_thread_func(void* arg);
int start_reconnection_process(mcp_transport_t* transport);
void stop_reconnection_process(mcp_transport_t* transport);

// Internal structure for TCP client transport data
typedef struct {
    // Basic connection info
    char* host;
    uint16_t port;
    socket_t sock;
    volatile bool running; // Added volatile back
    bool connected; // Track connection state
    mcp_transport_t* transport_handle; // Pointer back to the main handle (contains callbacks)
    mcp_thread_t receive_thread; // Use abstracted thread type
    mcp_buffer_pool_t* buffer_pool; // Buffer pool for message buffers

    // Reconnection support
    mcp_reconnect_config_t reconnect_config; // Reconnection configuration
    bool reconnect_enabled; // Whether reconnection is enabled
    int reconnect_attempt; // Current reconnection attempt number
    mcp_thread_t reconnect_thread; // Thread for reconnection timer
    volatile bool reconnect_thread_running; // Flag to control reconnect thread
    mcp_mutex_t* reconnect_mutex; // Mutex to protect reconnection state

    // Connection state tracking
    mcp_connection_state_t connection_state; // Current connection state
    mcp_connection_state_callback_t state_callback; // Callback for state changes
    void* state_callback_user_data; // User data for state callback
} mcp_tcp_client_transport_data_t;

// --- Internal Function Prototypes ---

// Forward declaration for connection state update function
void mcp_tcp_client_update_connection_state(mcp_tcp_client_transport_data_t* data, mcp_connection_state_t new_state);

// From mcp_tcp_client_receiver.c
// Use the abstracted thread function signature
void* tcp_client_receive_thread_func(void* arg);

#endif // MCP_TCP_CLIENT_TRANSPORT_INTERNAL_H
