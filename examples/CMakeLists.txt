# Examples CMakeLists.txt

# Ensure the common library headers are available
# The target_include_directories in the parent CMakeLists.txt
# should make the 'include' directory available publicly.

# Example Echo Server
add_executable(echo_server echo_server.c)
target_link_libraries(echo_server PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(echo_server PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(echo_server PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Example Echo Client
add_executable(echo_client echo_client.c)
target_include_directories(echo_client PRIVATE ../client/include) # Add client include path
target_link_libraries(echo_client PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(echo_client PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(echo_client PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Template Server Example
add_executable(template_server template_server.c)
target_link_libraries(template_server PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(template_server PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(template_server PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Secure Template Server Example
add_executable(secure_template_server secure_template_server.c)
target_link_libraries(secure_template_server PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(secure_template_server PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(secure_template_server PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Template Benchmark
add_executable(template_benchmark template_benchmark.c)
target_link_libraries(template_benchmark PRIVATE mcpcore)

# Template Benchmark Advanced
add_executable(template_benchmark_advanced template_benchmark_advanced.c)
target_link_libraries(template_benchmark_advanced PRIVATE mcpcore)

# Template Debug
add_executable(template_debug template_debug.c)
target_link_libraries(template_debug PRIVATE mcpcore)

# Template Examples
add_executable(template_examples template_examples.c)
target_link_libraries(template_examples PRIVATE mcpcore)

# Template Client Test
add_executable(template_client_test template_client_test.c)
target_link_libraries(template_client_test PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(template_client_test PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(template_client_test PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# WebSocket Server Example
add_executable(websocket_server websocket_server.c)
target_link_libraries(websocket_server PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(websocket_server PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(websocket_server PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# WebSocket Client Example
add_executable(websocket_client websocket_client.c)
target_include_directories(websocket_client PRIVATE ../client/include) # Add client include path
target_link_libraries(websocket_client PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(websocket_client PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(websocket_client PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Reconnect Client Example
add_executable(reconnect_client_example reconnect_client_example.c)
target_include_directories(reconnect_client_example PRIVATE ../client/include) # Add client include path
target_link_libraries(reconnect_client_example PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(reconnect_client_example PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(reconnect_client_example PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Connection Pool Client Example
add_executable(pool_client_example pool_client_example.c)
target_include_directories(pool_client_example PRIVATE ../client/include) # Add client include path
target_link_libraries(pool_client_example PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(pool_client_example PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(pool_client_example PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Graceful Shutdown Example
add_executable(graceful_shutdown_example graceful_shutdown_example.c)
target_include_directories(graceful_shutdown_example PRIVATE ../server/include) # Add server include path
target_link_libraries(graceful_shutdown_example PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(graceful_shutdown_example PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(graceful_shutdown_example PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Batch Request Example
add_executable(batch_request_example batch_request_example.c)
target_include_directories(batch_request_example PRIVATE ../client/include) # Add client include path
target_link_libraries(batch_request_example PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(batch_request_example PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(batch_request_example PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Thread Cache Demo
add_executable(thread_cache_demo thread_cache_demo.c)
target_link_libraries(thread_cache_demo PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(thread_cache_demo PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(thread_cache_demo PRIVATE Threads::Threads m) # Use PRIVATE keyword, add math library
endif()

# Memory Pool Statistics Demo
add_executable(memory_pool_stats_demo memory_pool_stats_demo.c)
target_link_libraries(memory_pool_stats_demo PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(memory_pool_stats_demo PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(memory_pool_stats_demo PRIVATE Threads::Threads m) # Use PRIVATE keyword, add math library
endif()

# Read-Write Lock Demo
add_executable(rwlock_demo rwlock_demo.c)
target_link_libraries(rwlock_demo PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(rwlock_demo PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(rwlock_demo PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Performance Metrics Test
add_executable(performance_metrics_test performance_metrics_test.c)
target_link_libraries(performance_metrics_test PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(performance_metrics_test PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(performance_metrics_test PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Object Cache Test
add_executable(object_cache_test object_cache_test.c)
target_link_libraries(object_cache_test PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(object_cache_test PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(object_cache_test PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Thread Cache Test
add_executable(thread_cache_test thread_cache_test.c)
target_link_libraries(thread_cache_test PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(thread_cache_test PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(thread_cache_test PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Advanced Rate Limiter Demo
add_executable(advanced_rate_limiter_demo advanced_rate_limiter_demo.c)
target_link_libraries(advanced_rate_limiter_demo PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(advanced_rate_limiter_demo PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(advanced_rate_limiter_demo PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# KMCP examples have been removed

# HTTP Server Example with SSE
add_executable(http_server http_server.c http_static_res.c)
target_link_libraries(http_server PRIVATE mcpcore)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(http_server PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(http_server PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Basic HTTP Server Example (no libwebsockets dependency)
add_executable(basic_http_server basic_http_server.c)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(basic_http_server PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(basic_http_server PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Optional: Install examples
# install(TARGETS echo_server echo_client template_server secure_template_server template_benchmark websocket_server websocket_client http_server DESTINATION bin/examples)
