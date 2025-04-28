# CMake file for KMCP unit tests

# Add unit test sources
set(KMCP_UNIT_TEST_SOURCES
    kmcp_unit_test_runner.c
    kmcp_server_manager_stub.c
    run_tests.c
)

# Add unit test executable
add_executable(kmcp_unit_tests ${KMCP_UNIT_TEST_SOURCES})

# Link with KMCP library
target_link_libraries(kmcp_unit_tests kmcp mcpcore)

# Add include directories
target_include_directories(kmcp_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/client/include
    ${CMAKE_SOURCE_DIR}/kmcp/include
    ${CMAKE_SOURCE_DIR}/kmcp/src
)

# Add unit test
add_test(NAME kmcp_unit_tests COMMAND kmcp_unit_tests)

# Add individual unit test executables for easier debugging
# (Currently no individual unit tests defined)

# Add individual unit tests
# (Currently no individual unit tests defined)

# Set test properties
# (Currently no test properties defined)
