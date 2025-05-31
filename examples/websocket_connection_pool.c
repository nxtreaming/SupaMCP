#include "mcp_websocket_connection_pool.h"
#include "mcp_log.h"
#include "mcp_socket_utils.h"
#include "mcp_sys_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#   include <windows.h>
#else
#   include <unistd.h>
#   include <signal.h>
#endif

// Global variables
static volatile bool g_running = true;
static mcp_ws_connection_pool_t* g_pool = NULL;

// Signal handler for graceful shutdown
#ifdef _WIN32
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        printf("Shutting down...\n");
        g_running = false;
        return TRUE;
    }
    return FALSE;
}
#else
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("Shutting down...\n");
        g_running = false;
    }
}
#endif

// Worker thread function
typedef struct {
    int id;
    mcp_ws_connection_pool_t* pool;
} worker_args_t;

void* worker_thread_func(void* arg) {
    worker_args_t* args = (worker_args_t*)arg;
    int id = args->id;
    mcp_ws_connection_pool_t* pool = args->pool;

    printf("Worker %d: Starting\n", id);

    // Simulate work with random intervals
    srand((unsigned int)(time(NULL) + id));

    while (g_running) {
        // Random work duration between 500ms and 2000ms
        int work_duration = 500 + (rand() % 1500);

        // Get a connection from the pool with timeout
        printf("Worker %d: Requesting connection from pool\n", id);
        mcp_transport_t* transport = mcp_ws_connection_pool_get(pool, 5000);

        if (transport) {
            printf("Worker %d: Got connection, working for %d ms\n", id, work_duration);

            // Simulate work by sending a message
            const char* message = "{\"method\":\"ping\",\"params\":{},\"id\":1}";
            int result = mcp_transport_send(transport, message, strlen(message));

            if (result == 0) {
                // Wait for response
                char* buffer = NULL;
                size_t buffer_size = 0;
                int received = mcp_transport_receive(transport, &buffer, &buffer_size, 1000);

                if (received == 0 && buffer != NULL) {
                    printf("Worker %d: Received response: %s\n", id, buffer);
                    free(buffer); // Don't forget to free the allocated buffer
                } else {
                    printf("Worker %d: No response received (code: %d)\n", id, received);
                }
            } else {
                printf("Worker %d: Failed to send message\n", id);
            }

            // Simulate work
            mcp_sleep_ms(work_duration);

            // Release the connection back to the pool
            printf("Worker %d: Releasing connection back to pool\n", id);
            mcp_ws_connection_pool_release(pool, transport);

            // Random pause between 100ms and 1000ms
            int pause_duration = 100 + (rand() % 900);
            mcp_sleep_ms(pause_duration);
        } else {
            printf("Worker %d: Failed to get connection from pool\n", id);
            mcp_sleep_ms(500);
        }
    }

    printf("Worker %d: Exiting\n", id);
    free(args);
    return NULL;
}

// Print pool statistics
void print_pool_stats(mcp_ws_connection_pool_t* pool) {
    uint32_t total = 0;
    uint32_t available = 0;
    uint32_t in_use = 0;
    uint32_t connecting = 0;
    uint32_t invalid = 0;

    mcp_ws_connection_pool_get_stats(pool, &total, &available, &in_use, &connecting, &invalid);

    printf("Pool stats: Total=%u, Available=%u, In-use=%u, Connecting=%u, Invalid=%u\n",
           total, available, in_use, connecting, invalid);
}

// Main function
int main(int argc, char* argv[]) {
    // Set up signal handling for graceful shutdown
#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
#endif

    // Parse command line arguments
    const char* host = "127.0.0.1";
    uint16_t port = 8080;
    const char* path = "/ws";
    int num_workers = 5;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            path = argv[++i];
        } else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
            num_workers = atoi(argv[++i]);
            if (num_workers < 1) num_workers = 1;
            if (num_workers > 20) num_workers = 20;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --host HOST       WebSocket server host (default: 127.0.0.1)\n");
            printf("  --port PORT       WebSocket server port (default: 8080)\n");
            printf("  --path PATH       WebSocket endpoint path (default: /ws)\n");
            printf("  --workers N       Number of worker threads (default: 5, max: 20)\n");
            printf("  --help            Show this help message\n");
            return 0;
        }
    }

    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    printf("WebSocket Connection Pool Example\n");
    printf("Connecting to WebSocket server at %s:%d%s\n", host, port, path);
    printf("Using %d worker threads\n", num_workers);

    // Create WebSocket connection pool configuration
    mcp_ws_pool_config_t pool_config = {
        .min_connections = 2,
        .max_connections = 10,
        .idle_timeout_ms = 30000,    // 30 seconds
        .health_check_ms = 5000,     // 5 seconds
        .connect_timeout_ms = 1000,  // 1 second
        .ws_config = {
            .host = host,
            .port = port,
            .path = path,
            .origin = NULL,
            .protocol = NULL,
            .use_ssl = false,
            .cert_path = NULL,
            .key_path = NULL,
            .connect_timeout_ms = 1000  // 1 second
        }
    };

    // Create connection pool
    g_pool = mcp_ws_connection_pool_create(&pool_config);
    if (!g_pool) {
        printf("Failed to create WebSocket connection pool\n");
        return 1;
    }

    printf("WebSocket connection pool created successfully\n");

    // Create worker threads
    mcp_thread_t* workers = (mcp_thread_t*)malloc(num_workers * sizeof(mcp_thread_t));
    if (!workers) {
        printf("Failed to allocate memory for worker threads\n");
        mcp_ws_connection_pool_destroy(g_pool);
        return 1;
    }

    for (int i = 0; i < num_workers; i++) {
        worker_args_t* args = (worker_args_t*)malloc(sizeof(worker_args_t));
        if (!args) {
            printf("Failed to allocate memory for worker arguments\n");
            continue;
        }

        args->id = i + 1;
        args->pool = g_pool;

        if (mcp_thread_create(&workers[i], worker_thread_func, args) != 0) {
            printf("Failed to create worker thread %d\n", i + 1);
            free(args);
        }
    }

    // Main loop - print stats periodically
    while (g_running) {
        print_pool_stats(g_pool);
        mcp_sleep_ms(2000);
    }

    // Wait for worker threads to exit
    for (int i = 0; i < num_workers; i++) {
        mcp_thread_join(workers[i], NULL);
    }

    // Clean up
    free(workers);
    mcp_ws_connection_pool_destroy(g_pool);
    mcp_log_close();

    printf("WebSocket connection pool example completed\n");
    return 0;
}
