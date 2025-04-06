# SupaMCPServer Integration Guide

## Introduction

This guide provides instructions on how to build the SupaMCPServer library and integrate its server and client components into your C applications. SupaMCPServer provides a foundation for building Model Context Protocol (MCP) compliant servers and clients.

## Building the Library

The library uses CMake for building.

**Prerequisites:**
*   CMake (version 3.10 or higher)
*   A C compiler supporting C99 (e.g., GCC, Clang, MSVC)
*   (Optional) Pthreads library on non-Windows systems (usually included by default)

**Build Steps:**

1.  **Clone the repository (if you haven't already):**
    ```bash
    git clone <repository_url> SupaMCPServer
    cd SupaMCPServer
    ```

2.  **Create a build directory:**
    ```bash
    mkdir build
    cd build
    ```

3.  **Configure using CMake:**
    *   **Basic:**
        ```bash
        cmake ..
        ```
    *   **Specify Generator (e.g., for Visual Studio):**
        ```bash
        cmake .. -G "Visual Studio 17 2022"
        ```
    *   **Enable Profiling (Optional):**
        ```bash
        cmake .. -DMCP_ENABLE_PROFILING=ON
        ```

4.  **Build the library and examples:**
    ```bash
    cmake --build .
    ```
    Or use your IDE's build command (e.g., build the solution in Visual Studio).

This will build:
*   `libmcp_common.a` (or `.lib`) in the build directory (or a subdirectory).
*   `mcp_server` executable (the main server application).
*   `mcp_client` executable (the main client application).
*   `echo_server` executable (example).
*   `echo_client` executable (example).
*   Test executables (if tests are enabled).

## Integrating the Server

To create your own MCP server:

1.  **Include Headers:** Include the necessary headers, primarily `mcp_server.h` and potentially transport headers like `mcp_tcp_transport.h`.
    ```c
    #include <mcp_server.h>
    #include <mcp_tcp_transport.h>
    #include <mcp_log.h> // Optional for logging
    // ... other necessary includes
    ```

2.  **Define Handlers:** Implement callback functions for handling resource reads (`mcp_server_resource_handler_t`) and tool calls (`mcp_server_tool_handler_t`) based on your server's logic. Remember these handlers might be called concurrently and should be thread-safe if accessing shared state. See `examples/echo_server.c` for a basic tool handler example.

3.  **Configure the Server:** Create and populate `mcp_server_config_t` and `mcp_server_capabilities_t` structs.
    ```c
    mcp_server_config_t server_config = {
        .name = "my-custom-server",
        .version = "1.0",
        .thread_pool_size = 4,
        .max_message_size = 1024 * 1024, // 1MB
        // ... other options like cache, rate limit, api_key
    };
    mcp_server_capabilities_t capabilities = {
        .resources_supported = true, // Or false
        .tools_supported = true      // Or false
    };
    ```

4.  **Create the Server Instance:**
    ```c
    mcp_server_t* server = mcp_server_create(&server_config, &capabilities);
    if (!server) { /* Handle error */ }
    ```

5.  **Register Handlers, Resources, Tools:**
    ```c
    mcp_server_set_tool_handler(server, my_tool_handler, my_user_data);
    mcp_server_set_resource_handler(server, my_resource_handler, my_user_data);

    // Add static resources/templates/tools if needed
    mcp_tool_t* my_tool = mcp_tool_create(...);
    mcp_server_add_tool(server, my_tool);
    mcp_tool_free(my_tool); // Server makes a copy
    ```

6.  **Create a Transport:** Choose and create a transport (e.g., TCP).
    ```c
    mcp_transport_t* transport = mcp_transport_tcp_create("0.0.0.0", 9000, 60000); // Host, Port, Idle Timeout (ms)
    if (!transport) { /* Handle error */ }
    ```

7.  **Start the Server:** Associate the server with the transport and start processing.
    ```c
    if (mcp_server_start(server, transport) != 0) {
        /* Handle error */
        mcp_transport_destroy(transport); // Clean up transport if start fails
        mcp_server_destroy(server);
    }
    ```

8.  **Run and Shutdown:** Keep your application running (e.g., in a loop or waiting for signals). On shutdown, destroy the server and transport.
    ```c
    // ... wait loop ...

    mcp_server_destroy(server); // Stops and cleans up server resources
    // Transport is destroyed by mcp_server_destroy if started
    ```

## Gateway Mode

The server can optionally run in Gateway mode by passing the `--gateway` command-line flag.

When in Gateway mode:
1.  The server attempts to load backend configurations from `gateway_config.json` located in the current working directory.
2.  It creates connection pools for configured TCP backends.
3.  Incoming `read_resource` and `call_tool` requests are checked against the routing rules defined in the configuration.
4.  If a matching rule is found for a TCP backend with a valid connection pool, the request is forwarded to that backend, and the backend's response is relayed to the original client.
5.  If no routing rule matches, or if the server is not run with `--gateway`, the request is handled by the server's local resource/tool handlers (if registered).

**Configuration (`gateway_config.json`):**

The file should contain a JSON array of backend objects:

```json
[
  {
    "name": "unique_backend_name",
    "address": "tcp://host:port", // Only tcp:// currently supported
    "routing": {
      "resource_prefixes": ["prefix1://", ...], // Optional
      "tool_names": ["tool1", ...]              // Optional
    },
    "timeout_ms": 5000 // Optional (milliseconds)
  }
]
```

**Note:** Currently, only TCP backends are fully supported for forwarding. Stdio backends defined in the config will not have connection pools created.

## Integrating the Client

To create an MCP client:

1.  **Include Headers:** Include `mcp_client.h` and the relevant transport header (e.g., `mcp_tcp_client_transport.h`).
    ```c
    #include <mcp_client.h>
    #include <mcp_tcp_client_transport.h>
    #include <mcp_log.h> // Optional
    // ...
    ```

2.  **Configure the Client:** Create and populate `mcp_client_config_t`.
    ```c
    mcp_client_config_t client_config = {
        .request_timeout_ms = 10000 // 10 seconds
        // .api_key = "secret-key" // If server requires it
    };
    ```

3.  **Create a Client Transport:** Create a transport suitable for connecting to the server.
    ```c
    mcp_transport_t* transport = mcp_transport_tcp_client_create("127.0.0.1", 18889); // Server host, Server port
    if (!transport) { /* Handle error */ }
    ```

4.  **Create the Client:** The client takes ownership of the transport.
    ```c
    mcp_client_t* client = mcp_client_create(&client_config, transport);
    if (!client) {
        /* Handle error */
        // Transport is destroyed by mcp_client_create on failure
    }
    ```

5.  **Call Tools / Read Resources:** Use `mcp_client_call_tool` or `mcp_client_read_resource`. Remember to format tool arguments as a JSON string.
    ```c
    mcp_content_item_t** result_content = NULL;
    size_t result_count = 0;
    bool is_error = false;
    const char* tool_args = "{\"text\": \"Hello from client!\"}"; // JSON string

    int status = mcp_client_call_tool(client, "echo", tool_args, &result_content, &result_count, &is_error);

    if (status == 0 && !is_error) {
        // Process result_content
        printf("Result: %s\n", (const char*)result_content[0]->data);
    } else {
        // Handle error
    }
    mcp_free_content(result_content, result_count); // IMPORTANT: Free the result
    ```

6.  **Destroy the Client:** This also destroys the associated transport.
    ```c
    mcp_client_destroy(client);
    ```

## Key Concepts

*   **Transport:** Handles the underlying communication (TCP, Stdio). The server uses a listening transport, the client uses a connecting transport.
*   **Server:** Manages incoming connections, parses messages, dispatches requests to handlers via a thread pool, and sends responses.
*   **Client:** Sends requests, waits for responses, handles timeouts.
*   **Handlers:** User-provided callback functions (`mcp_server_resource_handler_t`, `mcp_server_tool_handler_t`) that implement the server's specific logic for resources and tools.
*   **Arena Allocator (`mcp_arena.h`):** Used internally for efficient temporary allocations during message parsing. Not typically used directly by integrators unless needed for advanced parameter parsing within handlers.
*   **Content Items (`mcp_types.h`):** Standard way to represent resource/tool results (`mcp_content_item_t`). Handlers allocate these, and the client receives them. Remember to use `mcp_free_content` on the client side.

---

*This is a basic integration guide. Refer to the header file comments (`*.h`) for detailed API documentation.*
