# CMake file for KMCP system tests

# Add system test sources
set(KMCP_SYSTEM_TEST_SOURCES
    kmcp_system_test_runner.c
    kmcp_server_manager_stub.c
    run_tests.c
)

# Add system test executable
add_executable(kmcp_system_tests ${KMCP_SYSTEM_TEST_SOURCES})

# Link with KMCP library
target_link_libraries(kmcp_system_tests kmcp mcpcore)

# Add include directories
target_include_directories(kmcp_system_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/client/include
    ${CMAKE_SOURCE_DIR}/kmcp/include
    ${CMAKE_SOURCE_DIR}/kmcp/src
)

# Add system test
add_test(NAME kmcp_system_tests COMMAND kmcp_system_tests)

# Add individual system test executables for easier debugging
# (Currently no individual system tests defined)

# Add individual system tests
# (Currently no individual system tests defined)

# Set test properties
# (Currently no test properties defined)
