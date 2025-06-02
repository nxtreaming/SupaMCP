cmake_minimum_required(VERSION 3.10)
project(SupaMCPServer VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add compiler flags
if(MSVC)
    # MSVC flags
    add_compile_options(/W4)
    # Enable multi-threaded compilation
    add_compile_options(/MP)
    # Enable function-level linking
    add_compile_options(/Gy)
    # Enable intrinsic functions
    add_compile_options(/Oi)
    # Specify runtime library
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    # GCC/Clang flags
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Check for pthreads on non-Windows platforms
if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

# Use manually installed libwebsockets
message(STATUS "Using libwebsockets")

# Set paths to libwebsockets
if(APPLE)
    # On macOS, try to use Homebrew installed libwebsockets
    execute_process(
        COMMAND brew --prefix libwebsockets
        OUTPUT_VARIABLE BREW_LIBWEBSOCKETS_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(BREW_LIBWEBSOCKETS_PREFIX)
        message(STATUS "Using Homebrew libwebsockets at ${BREW_LIBWEBSOCKETS_PREFIX}")
        set(LIBWEBSOCKETS_INCLUDE_DIR "${BREW_LIBWEBSOCKETS_PREFIX}/include")
        set(LIBWEBSOCKETS_LIBRARY "${BREW_LIBWEBSOCKETS_PREFIX}/lib/libwebsockets.dylib")
    else()
        # Fallback to manually installed
        set(LIBWEBSOCKETS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
        set(LIBWEBSOCKETS_LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/libwebsockets)
        set(LIBWEBSOCKETS_LIBRARY ${LIBWEBSOCKETS_LIBRARY_DIR}/libwebsockets.a)
    endif()
else()
    # Original path for other platforms
    set(LIBWEBSOCKETS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set(LIBWEBSOCKETS_LIBRARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/libwebsockets)

    # Check if the library files exist
    if(WIN32)
        set(LIBWEBSOCKETS_LIBRARY ${LIBWEBSOCKETS_LIBRARY_DIR}/websockets.lib)
    else()
        set(LIBWEBSOCKETS_LIBRARY ${LIBWEBSOCKETS_LIBRARY_DIR}/libwebsockets.a)
    endif()
endif()

# Check if the library files exist
if(NOT EXISTS ${LIBWEBSOCKETS_LIBRARY})
    message(FATAL_ERROR "Could not find libwebsockets library at ${LIBWEBSOCKETS_LIBRARY}")
endif()

# Create an imported target for libwebsockets
if(APPLE AND BREW_LIBWEBSOCKETS_PREFIX)
    add_library(websockets SHARED IMPORTED)
else()
    add_library(websockets STATIC IMPORTED)
endif()
set_target_properties(websockets PROPERTIES
    IMPORTED_LOCATION ${LIBWEBSOCKETS_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES "${LIBWEBSOCKETS_INCLUDE_DIR};${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Add include directory to system include paths for OpenSSL headers
include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Set up OpenSSL
if(WIN32)
    set(OPENSSL_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/OpenSSL)
    set(OPENSSL_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/openssl)
    set(OPENSSL_LIBRARIES
        ${OPENSSL_ROOT_DIR}/libssl.lib
        ${OPENSSL_ROOT_DIR}/libcrypto.lib
    )

    # Create imported targets for OpenSSL
    add_library(ssl STATIC IMPORTED)
    set_target_properties(ssl PROPERTIES
        IMPORTED_LOCATION ${OPENSSL_ROOT_DIR}/libssl.lib
        INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_INCLUDE_DIR}
    )

    add_library(crypto STATIC IMPORTED)
    set_target_properties(crypto PROPERTIES
        IMPORTED_LOCATION ${OPENSSL_ROOT_DIR}/libcrypto.lib
        INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_INCLUDE_DIR}
    )
else()
    find_package(OpenSSL REQUIRED)
endif()

# Check if we need to link against additional dependencies
if(WIN32)
    # On Windows, libwebsockets might need ws2_32 and other libraries
    set_property(TARGET websockets PROPERTY
        INTERFACE_LINK_LIBRARIES ws2_32 crypt32 secur32)
endif()

# Common library source files
set(COMMON_SOURCES
    src/json/mcp_json.c
    src/json/mcp_json_parser.c
    src/json/mcp_json_stringifier.c
    src/json/mcp_json_utils.c
    src/json/mcp_json_message.c
    src/json/mcp_json_rpc.c
    src/json/mcp_json_parse_direct.c
    src/json/mcp_json_schema_cache.c
    src/json/mcp_json_schema_validate.c
    src/transport/mcp_transport.c
    src/transport/mcp_transport_factory.c
    src/transport/mcp_stdio_transport.c
    src/transport/mcp_tcp_transport.c
    src/transport/mcp_tcp_client_handler.c
    src/transport/mcp_tcp_acceptor.c
    src/transport/mcp_tcp_client_transport.c
    src/transport/mcp_tcp_client_receiver.c
    src/transport/mcp_tcp_client_reconnect.c
    src/transport/mcp_tcp_pool_transport.c
    src/transport/mcp_tcp_server_utils.c
    src/transport/mcp_websocket_common.c
    src/transport/mcp_websocket_transport.c
    src/transport/mcp_websocket_server_bitmap.c
    src/transport/mcp_websocket_server_client.c
    src/transport/mcp_websocket_server_callback.c
    src/transport/mcp_websocket_server_event.c
    src/transport/mcp_websocket_client_transport.c
    src/transport/mcp_websocket_client_connection.c
    src/transport/mcp_websocket_client_message.c
    src/transport/mcp_websocket_connection_pool.c
    src/transport/mcp_http_transport.c
    src/transport/mcp_http_sse_common.c
    src/transport/mcp_http_server_sse.c
    src/transport/mcp_http_server_handlers.c
    src/transport/mcp_http_server_callbacks.c
    src/transport/mcp_http_client_transport.c
    src/transport/mcp_http_client_request.c
    src/transport/mcp_http_client_sse.c
    src/transport/mcp_http_client_utils.c
    src/transport/http_client_ssl_internal.c
    src/transport/mcp_http_session_manager.c
    src/transport/mcp_sthttp_transport.c
    src/transport/mcp_sthttp_utils.c
    src/transport/mcp_sthttp_threads.c
    src/transport/mcp_sthttp_callbacks.c
    src/transport/mcp_sthttp_client_transport.c
    src/transport/mcp_sthttp_client_core.c
    src/transport/mcp_sthttp_client_sse.c
    src/common/mcp_types.c
    src/common/mcp_arena.c
    src/common/mcp_log.c
    src/common/mcp_thread_local.c
    src/common/mcp_string_utils.c
    src/common/mcp_sync.c
    src/common/mcp_socket_utils.c
    src/common/mcp_sys_utils.c
    src/common/mcp_framing.c
    src/common/mcp_thread_pool.c
    src/common/mcp_thread_pool_atomic.c
    src/common/mcp_thread_pool_deque.c
    src/common/mcp_thread_pool_system.c
    src/common/mcp_thread_pool_tasks.c
    src/common/mcp_thread_pool_worker.c
    src/common/mcp_thread_pool_stats.c
    src/common/mcp_object_pool.c
    src/common/mcp_memory_pool.c
    src/common/mcp_thread_cache.c
    src/common/mcp_memory_tracker.c
    src/common/mcp_template.c
    src/common/mcp_template_optimized.c
    src/common/mcp_rwlock.c
    src/common/mcp_list.c
    src/common/mcp_buffer_pool.c
    src/server/mcp_cache.c
    src/server/mcp_rate_limiter.c
    src/server/mcp_profiler.c
    src/common/mcp_hashtable.c
    src/common/mcp_performance_metrics.c
    src/server/mcp_performance_collector.c
    src/common/mcp_object_cache.c
    src/server/mcp_advanced_rate_limiter.c
    src/client/mcp_client_template.c
    src/client/mcp_client_core.c
    src/client/mcp_client_hash_table.c
    src/client/mcp_client_request.c
    src/client/mcp_client_api.c
    src/client/mcp_client_batch.c
    src/server/mcp_server.c
    src/server/mcp_server_dispatch.c
    src/server/mcp_server_handlers.c
    src/server/mcp_server_task.c
    src/server/mcp_server_response.c
    src/server/mcp_server_template_router.c
    src/server/mcp_template_security.c
    src/server/mcp_server_template_security.c
    src/server/mcp_auth.c
    src/server/mcp_performance_metrics_handlers.c
    src/connection_pool/mcp_connection_pool.c
    src/connection_pool/mcp_connection_pool_socket.c
    src/connection_pool/mcp_connection_pool_sync.c
    src/connection_pool/mcp_connection_pool_maintenance.c
    src/connection_pool/mcp_connection_pool_health.c
    src/connection_pool/mcp_connection_pool_dns_cache.c
    src/server/mcp_plugin.c
    src/gateway/mcp_gateway_config.c
    src/gateway/mcp_gateway_routing.c
    src/gateway/mcp_gateway_pool.c
    src/gateway/mcp_gateway_config_manager.c
)

# Create common library
add_library(mcpcore SHARED ${COMMON_SOURCES})
target_include_directories(mcpcore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include       # Public API headers
)
target_link_libraries(mcpcore PUBLIC websockets ssl crypto)

# Windows-specific settings for mcpcore
if(MSVC)
    # Generate import library
    set_target_properties(mcpcore PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()

# Make module source directories as private include paths so internal headers can be found
target_include_directories(mcpcore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/json
    ${CMAKE_CURRENT_SOURCE_DIR}/src/json/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client
    ${CMAKE_CURRENT_SOURCE_DIR}/src/client/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/connection_pool
    ${CMAKE_CURRENT_SOURCE_DIR}/src/connection_pool/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gateway
)

# KMCP library has been removed

# Server-specific source files
set(SERVER_SOURCES
    src/main.c
    src/tools/http_client_tool.c
)

# Client-specific source files
set(CLIENT_SOURCES
    client/main.c
)

# Create server executable
add_executable(mcp_server ${SERVER_SOURCES})
target_link_libraries(mcp_server PRIVATE mcpcore)

# Create client executable
add_executable(mcp_client ${CLIENT_SOURCES})
target_link_libraries(mcp_client PRIVATE mcpcore)

# Link platform-specific libraries
if(WIN32)
    target_link_libraries(mcpcore INTERFACE ws2_32) # Use INTERFACE so consumers link it too
else()
    # Link pthreads and dl (for dynamic loading) on non-Windows
    target_link_libraries(mcpcore INTERFACE Threads::Threads dl) # Use modern Threads target and INTERFACE
endif()

# Install
install(TARGETS mcp_server mcp_client DESTINATION bin)
install(DIRECTORY include/ DESTINATION include) # Install public headers

# --- Benchmark Setup ---
# Define benchmark sources
set(BENCHMARK_SOURCES
    benchmark/mcp_benchmark.c
    benchmark/benchmark_cli.c
)
set(BENCHMARK_HEADERS
    benchmark/mcp_benchmark.h
)

# Create benchmark executable
add_executable(mcp_benchmark_cli ${BENCHMARK_SOURCES} ${BENCHMARK_HEADERS})
target_link_libraries(mcp_benchmark_cli PRIVATE mcpcore)
# Benchmark needs access to its own headers
target_include_directories(mcp_benchmark_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmark
)
# Common/Client headers are inherited via mcp_common PUBLIC include directories

# Optionally install the benchmark tool
# install(TARGETS mcp_benchmark_cli DESTINATION bin)

# Print configuration
message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
message(STATUS "CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# --- Testing Setup ---
enable_testing()

# Add Unity library
set(UNITY_SOURCES tests/unity/unity.c)
set(UNITY_HEADERS tests/unity/unity.h tests/unity/unity_internals.h)
add_library(unity_lib STATIC ${UNITY_SOURCES} ${UNITY_HEADERS})
target_include_directories(unity_lib PUBLIC tests/unity) # Make headers available

# Add tests subdirectory (will contain test runner definition)
add_subdirectory(tests)

# Add examples subdirectory (optional, if examples exist)
add_subdirectory(examples)

# Python bindings and KMCP module have been removed
