# CMake file for KMCP integration tests

# Add integration test sources
set(KMCP_INTEGRATION_TEST_SOURCES
    kmcp_integration_test_runner.c
    kmcp_server_manager_stub.c
    run_tests.c
)

# Add integration test executable
add_executable(kmcp_integration_tests ${KMCP_INTEGRATION_TEST_SOURCES})

# Link with KMCP library
target_link_libraries(kmcp_integration_tests kmcp mcpcore)

# Add include directories
target_include_directories(kmcp_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/client/include
    ${CMAKE_SOURCE_DIR}/kmcp/include
    ${CMAKE_SOURCE_DIR}/kmcp/src
)

# Add integration test
add_test(NAME kmcp_integration_tests COMMAND kmcp_integration_tests)

# Add individual integration test executables for easier debugging
# (Currently no individual integration tests defined)

# Add individual integration tests
# (Currently no individual integration tests defined)

# Set test properties
# (Currently no test properties defined)
