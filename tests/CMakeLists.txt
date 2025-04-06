# Test executable source files (only test-specific files)
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
    test_mcp_log.c
    test_mcp_rate_limiter.c
    test_mcp_types.c
    test_mcp_json_message.c
    test_mcp_thread_pool.c
    test_mcp_plugin.c
    test_mcp_stdio_transport.c
    test_mcp_tcp_client_transport.c
    # Add other test_*.c files here
)

# Create the test runner executable
add_executable(mcp_tests ${TEST_RUNNER_SOURCES})

# Link the test runner against Unity and our common library
# mcp_common now contains all necessary library code definitions.
target_link_libraries(mcp_tests PRIVATE unity_lib mcp_common)

# Add include directories needed by tests
target_include_directories(mcp_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/client/include
    # Add internal directories needed by some tests, relative to CMAKE_SOURCE_DIR
    ${CMAKE_SOURCE_DIR}/src/common/internal
    ${CMAKE_SOURCE_DIR}/src/transport/internal
    ${CMAKE_SOURCE_DIR}/src/server/internal
    # Also add parent src directories for includes like "internal/..."
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/server
    # Unity headers are included via unity_lib target
)

# Add compile definition to expose static functions for testing
target_compile_definitions(mcp_tests PRIVATE TESTING)

# Define a CTest test case that runs the executable
add_test(NAME RunUnitTests COMMAND mcp_tests)
