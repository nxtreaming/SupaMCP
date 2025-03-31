# Test executable source files
set(TEST_RUNNER_SOURCES
    test_runner.c
    test_mcp_arena.c
    test_mcp_tcp_transport.c
    test_mcp_json.c
    # Add other test_*.c files here later
)

# Create the test runner executable
add_executable(mcp_tests ${TEST_RUNNER_SOURCES})

# Link the test runner against Unity and our common library
target_link_libraries(mcp_tests unity_lib mcp_common)

# Add include directories needed for tests
target_include_directories(mcp_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include 
    # tests/unity is already public via unity_lib target
)

# Define a CTest test case that runs the executable
add_test(NAME RunUnitTests COMMAND mcp_tests)
