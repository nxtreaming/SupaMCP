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
    ../client/src/mcp_client.c
    ../src/mcp_hashtable.c
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
    # Unity headers are included via unity_lib target
)

# Add compile definition to expose static functions for testing
target_compile_definitions(mcp_tests PRIVATE TESTING)

# Define a CTest test case that runs the executable
add_test(NAME RunUnitTests COMMAND mcp_tests)
