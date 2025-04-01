# Test executable source files
set(TEST_RUNNER_SOURCES
    test_runner.c
    test_mcp_arena.c
    test_mcp_tcp_transport.c
    test_mcp_json.c
    test_mcp_buffer_pool.c
    # Add other test_*.c files here later
)

# Create the test runner executable
add_executable(mcp_tests ${TEST_RUNNER_SOURCES})

# Link the test runner against Unity and our common library
target_link_libraries(mcp_tests unity_lib mcp_common)

# Add include directories needed by tests (e.g., client headers for free functions)
target_include_directories(mcp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../client/include
)

# Define a CTest test case that runs the executable
add_test(NAME RunUnitTests COMMAND mcp_tests)
