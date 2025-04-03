cmake_minimum_required(VERSION 3.10)
project(SupaMCPServer VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
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
    src/json/mcp_json_hashtable.c
    src/json/mcp_json_parser.c
    src/json/mcp_json_stringifier.c
    src/json/mcp_json_message.c
    src/json/mcp_json_rpc.c
    src/transport/mcp_transport.c
    src/transport/mcp_transport_factory.c
    src/transport/mcp_stdio_transport.c
    src/transport/mcp_tcp_transport.c
    src/transport/mcp_tcp_socket_utils.c
    src/transport/mcp_tcp_client_handler.c
    src/transport/mcp_tcp_acceptor.c
    src/transport/mcp_tcp_client_transport.c
    src/transport/mcp_tcp_client_socket_utils.c
    src/transport/mcp_tcp_client_receiver.c
    src/common/mcp_types.c
    src/common/mcp_arena.c
    src/common/mcp_log.c
    src/server/mcp_buffer_pool.c
    src/server/mcp_thread_pool.c
    #src/server/mcp_cache.c
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
    src/server/mcp_auth.c
    src/connection_pool/mcp_connection_pool.c
    src/connection_pool/mcp_connection_pool_socket.c
    src/connection_pool/mcp_connection_pool_sync.c
    src/connection_pool/mcp_connection_pool_utils.c
    src/server/mcp_plugin.c
)

# Common library header files
set(COMMON_HEADERS
    include/mcp_json.h
    include/mcp_json_rpc.h
    include/mcp_transport.h
    include/mcp_transport_factory.h
    include/mcp_stdio_transport.h
    include/mcp_tcp_transport.h
    include/mcp_types.h
    include/mcp_arena.h
    include/mcp_log.h
    include/mcp_thread_pool.h
    include/mcp_cache.h
    include/mcp_rate_limiter.h
    include/mcp_profiler.h
    include/mcp_hashtable.h
    client/include/mcp_client.h
    include/mcp_server.h
    include/mcp_auth.h
    include/mcp_connection_pool.h
    include/mcp_plugin.h
)

# Server-specific source files (Now only contains the server's main entry point)
set(SERVER_SOURCES
    src/main.c
)

# Server-specific header files (Redundant if added to COMMON_HEADERS, but harmless)
set(SERVER_HEADERS
    include/mcp_server.h
)

# Client-specific source files (Now only contains the client's main entry point)
set(CLIENT_SOURCES
    client/src/main.c
)

# Client-specific header files (Redundant if added to COMMON_HEADERS, but harmless)
set(CLIENT_HEADERS
    client/include/mcp_client.h
)

# Create common library
add_library(mcp_common STATIC)
target_sources(mcp_common PRIVATE ${COMMON_SOURCES})
# Make the public headers available to consumers
target_include_directories(mcp_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/client/include
)
# Add module source directories as private include paths so internal headers can be found
target_include_directories(mcp_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/json
    ${CMAKE_CURRENT_SOURCE_DIR}/src/transport
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/connection_pool
)

# Create server executable
add_executable(mcp_server ${SERVER_SOURCES} ${SERVER_HEADERS})
target_link_libraries(mcp_server PRIVATE mcp_common)
# Server needs access to its own headers in include/
target_include_directories(mcp_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Create client executable
# Note: This might need adjustment if client also needs thread pool, but for now it links mcp_common
add_executable(mcp_client ${CLIENT_SOURCES} ${CLIENT_HEADERS})
target_link_libraries(mcp_client PRIVATE mcp_common)
# Client needs access to its own headers in client/include/
target_include_directories(mcp_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/client/include
)

# Link platform-specific libraries
if(WIN32)
    target_link_libraries(mcp_common ws2_32)
else()
    # Link pthreads and dl (for dynamic loading) on non-Windows
    target_link_libraries(mcp_common ${CMAKE_THREAD_LIBS_INIT} dl)
endif()

# Install
install(TARGETS mcp_server mcp_client DESTINATION bin)

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
# Benchmark needs access to its own headers and common headers
target_include_directories(mcp_benchmark_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/benchmark
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/client/include # Needed if benchmark uses mcp_client directly
)
# Link platform-specific libraries (needed if benchmark uses threads/sockets directly,
# otherwise inherited from mcp_common)
# if(WIN32)
#     target_link_libraries(mcp_benchmark_cli ws2_32)
# else()
#     target_link_libraries(mcp_benchmark_cli ${CMAKE_THREAD_LIBS_INIT})
# endif()

# Optionally install the benchmark tool
# install(TARGETS mcp_benchmark_cli DESTINATION bin)

# Print configuration
message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
message(STATUS "CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
