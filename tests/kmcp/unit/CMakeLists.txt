# CMake file for KMCP unit tests

# Add unit test sources
set(KMCP_UNIT_TEST_SOURCES
    kmcp_unit_test_runner.c
    kmcp_profile_manager_test.c
    kmcp_server_manager_stub.c
)

# Add unit test executable
add_executable(kmcp_unit_tests ${KMCP_UNIT_TEST_SOURCES})

# Link with KMCP library
target_link_libraries(kmcp_unit_tests kmcp mcp_common)

# Add include directories
target_include_directories(kmcp_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/kmcp/include
    ${CMAKE_SOURCE_DIR}/kmcp/src
)

# Add unit test
add_test(NAME kmcp_unit_tests COMMAND kmcp_unit_tests)

# Add individual unit test executables for easier debugging
add_executable(unit_kmcp_profile_manager_test kmcp_profile_manager_test.c kmcp_server_manager_stub.c)
target_link_libraries(unit_kmcp_profile_manager_test kmcp mcp_common)
target_include_directories(unit_kmcp_profile_manager_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
)
target_compile_definitions(unit_kmcp_profile_manager_test PRIVATE STANDALONE_TEST)

# Add individual unit tests
add_test(NAME unit_kmcp_profile_manager_test COMMAND unit_kmcp_profile_manager_test)

# Set test properties
set_tests_properties(
    unit_kmcp_profile_manager_test
    PROPERTIES LABELS "unit_tests"
)
