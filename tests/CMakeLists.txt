# Test executable source files
set(TEST_RUNNER_SOURCES
    test_runner.c
    test_mcp_arena.c
    test_mcp_tcp_transport.c
    test_mcp_json.c
    test_mcp_buffer_pool.c
    mock_transport.c
    test_mcp_client_async.c
    test_mcp_hashtable.c
    test_mcp_cache.c
    test_mcp_transport_factory.c
    test_mcp_server_handlers.c
    ../client/src/mcp_client.c
    ../src/common/mcp_arena.c
    ../src/common/mcp_hashtable.c
    ../src/common/mcp_types.c
    ../src/common/mcp_log.c
    ../src/common/mcp_thread_local.c
    ../src/server/mcp_buffer_pool.c
    ../src/server/mcp_cache_optimized.c
    ../src/server/mcp_plugin.c
    ../src/server/mcp_profiler.c
    ../src/server/mcp_rate_limiter.c
    ../src/server/mcp_server.c
    ../src/server/mcp_server_dispatch.c
    ../src/server/mcp_server_handlers.c
    ../src/server/mcp_server_ping.c
    ../src/server/mcp_server_response.c
    ../src/server/mcp_server_task.c
    ../src/server/mcp_server_resource.c
    ../src/server/mcp_server_tool.c
    ../src/server/mcp_thread_pool.c
    ../src/server/mcp_auth.c
    ../src/json/mcp_json.c
    ../src/json/mcp_json_parser.c
    ../src/json/mcp_json_stringifier.c
    ../src/json/mcp_json_hashtable.c
    ../src/json/mcp_json_rpc.c
    ../src/json/mcp_json_message.c
    # Add other test_*.c files here later
)

# Create the test runner executable
add_executable(mcp_tests ${TEST_RUNNER_SOURCES})

# Link the test runner against Unity and our common library
# NOTE: mcp_common does NOT contain mcp_client.c when linked here,
# but mcp_tests compiles mcp_client.c directly.
target_link_libraries(mcp_tests unity_lib mcp_common)

# Add include directories needed by tests AND the included mcp_client.c
target_include_directories(mcp_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/client/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/server
    ${CMAKE_SOURCE_DIR}/src/json/internal
    ${CMAKE_SOURCE_DIR}/src/common/internal
    ${CMAKE_SOURCE_DIR}/src/common
    # Unity headers are included via unity_lib target
)

# Add compile definition to expose static functions for testing
target_compile_definitions(mcp_tests PRIVATE TESTING)

# Define a CTest test case that runs the executable
add_test(NAME RunUnitTests COMMAND mcp_tests)
