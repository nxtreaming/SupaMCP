# Examples CMakeLists.txt

# Ensure the common library headers are available
# The target_include_directories in the parent CMakeLists.txt
# should make the 'include' directory available publicly.

# Example Echo Server
add_executable(echo_server echo_server.c)
target_link_libraries(echo_server PRIVATE mcp_common)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(echo_server PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(echo_server PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Example Echo Client
add_executable(echo_client echo_client.c)
target_include_directories(echo_client PRIVATE ../client/include) # Add client include path
target_link_libraries(echo_client PRIVATE mcp_common)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(echo_client PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(echo_client PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Template Server Example
add_executable(template_server template_server.c)
target_link_libraries(template_server PRIVATE mcp_common)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(template_server PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(template_server PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Secure Template Server Example
add_executable(secure_template_server secure_template_server.c)
target_link_libraries(secure_template_server PRIVATE mcp_common)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(secure_template_server PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(secure_template_server PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Template Benchmark
add_executable(template_benchmark template_benchmark.c)
target_link_libraries(template_benchmark PRIVATE mcp_common)

# Template Benchmark Advanced
add_executable(template_benchmark_advanced template_benchmark_advanced.c)
target_link_libraries(template_benchmark_advanced PRIVATE mcp_common)

# Template Debug
add_executable(template_debug template_debug.c)
target_link_libraries(template_debug PRIVATE mcp_common)

# Template Examples
add_executable(template_examples template_examples.c)
target_link_libraries(template_examples PRIVATE mcp_common)

# Template Client Test
add_executable(template_client_test template_client_test.c)
target_link_libraries(template_client_test PRIVATE mcp_common)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(template_client_test PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(template_client_test PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# WebSocket Server Example
add_executable(websocket_server websocket_server.c)
target_link_libraries(websocket_server PRIVATE mcp_common)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(websocket_server PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(websocket_server PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# WebSocket Client Example
add_executable(websocket_client websocket_client.c)
target_include_directories(websocket_client PRIVATE ../client/include) # Add client include path
target_link_libraries(websocket_client PRIVATE mcp_common)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(websocket_client PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(websocket_client PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Reconnect Client Example
add_executable(reconnect_client_example reconnect_client_example.c)
target_include_directories(reconnect_client_example PRIVATE ../client/include) # Add client include path
target_link_libraries(reconnect_client_example PRIVATE mcp_common)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(reconnect_client_example PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(reconnect_client_example PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Optional: Install examples
# install(TARGETS echo_server echo_client template_server secure_template_server template_benchmark websocket_server websocket_client DESTINATION bin/examples)
