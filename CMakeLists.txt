cmake_minimum_required(VERSION 3.10)
project(SupaMCPServer VERSION 1.0.0 LANGUAGES C)

# Set C standard (C11 required for stdatomic.h)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add compiler flags
if(MSVC)
    # MSVC flags
    add_compile_options(/W4)
else()
    # GCC/Clang flags
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Check for pthreads on non-Windows platforms
if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

# Common library source files
set(COMMON_SOURCES
    src/json/mcp_json.c
    src/json/mcp_json_parser.c
    src/json/mcp_json_stringifier.c
    src/json/mcp_json_utils.c
    src/json/mcp_json_message.c
    src/json/mcp_json_rpc.c
    src/transport/mcp_transport.c
    src/transport/mcp_transport_factory.c
    src/transport/mcp_stdio_transport.c
    src/transport/mcp_tcp_transport.c
    src/transport/mcp_tcp_client_handler.c
    src/transport/mcp_tcp_acceptor.c
    src/transport/mcp_tcp_client_transport.c
    src/transport/mcp_tcp_client_receiver.c
    src/common/mcp_types.c
    src/common/mcp_arena.c
    src/common/mcp_log.c
    src/common/mcp_thread_local.c
    src/common/mcp_sync.c
    src/common/mcp_socket_utils.c
    src/common/mcp_framing.c
    src/common/mcp_string_utils.c
    src/common/mcp_thread_pool.c
    src/common/mcp_object_pool.c
    src/server/mcp_buffer_pool.c
    src/server/mcp_cache_optimized.c
    src/server/mcp_rate_limiter.c
    src/server/mcp_profiler.c
    src/common/mcp_hashtable.c
    client/src/mcp_client.c
    src/server/mcp_server.c
    src/server/mcp_server_dispatch.c
    src/server/mcp_server_handlers.c
    src/server/mcp_server_task.c
    src/server/mcp_server_response.c
    src/server/mcp_server_ping.c
    src/server/mcp_auth.c
    src/connection_pool/mcp_connection_pool.c
    src/connection_pool/mcp_connection_pool_socket.c
    src/connection_pool/mcp_connection_pool_sync.c
    src/connection_pool/mcp_connection_pool_utils.c
    src/server/mcp_plugin.c
    src/gateway/gateway_config.c
    src/gateway/gateway_routing.c
    src/gateway/gateway_pool.c
    src/gateway/gateway_socket_utils.c
)

# Create common library
add_library(mcp_common STATIC)
target_sources(mcp_common PRIVATE ${COMMON_SOURCES})
# Make the public headers available to consumers
target_include_directories(mcp_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include       # Public API headers
    ${CMAKE_CURRENT_SOURCE_DIR}/client/include # Make client headers public too
)
# Add module source directories as private include paths so internal headers can be found
target_include_directories(mcp_common PRIVATE
    # ${CMAKE_CURRENT_SOURCE_DIR}/client/include # Now public
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/json
    ${CMAKE_CURRENT_SOURCE_DIR}/src/json/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/connection_pool
    ${CMAKE_CURRENT_SOURCE_DIR}/src/connection_pool/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gateway
)

# Server-specific source files
set(SERVER_SOURCES
    src/main.c
)

# Client-specific source files
set(CLIENT_SOURCES
    client/src/main.c
)

# Create server executable
add_executable(mcp_server ${SERVER_SOURCES})
target_link_libraries(mcp_server PRIVATE mcp_common)
# Server include directories are inherited from mcp_common PUBLIC

# Create client executable
add_executable(mcp_client ${CLIENT_SOURCES})
target_link_libraries(mcp_client PRIVATE mcp_common)
# Client include directories are inherited from mcp_common PUBLIC

# Link platform-specific libraries
if(WIN32)
    target_link_libraries(mcp_common INTERFACE ws2_32) # Use INTERFACE so consumers link it too
else()
    # Link pthreads and dl (for dynamic loading) on non-Windows
    target_link_libraries(mcp_common INTERFACE Threads::Threads dl) # Use modern Threads target and INTERFACE
endif()

# Install
install(TARGETS mcp_server mcp_client DESTINATION bin)
install(DIRECTORY include/ DESTINATION include) # Install public headers
install(DIRECTORY client/include/ DESTINATION include) # Install public client headers

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
target_link_libraries(mcp_benchmark_cli PRIVATE mcp_common)
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
