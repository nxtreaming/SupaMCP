# CMake file for KMCP system tests

# Add system test sources
set(KMCP_SYSTEM_TEST_SOURCES
    kmcp_system_test_runner.c
    kmcp_profile_management_test.c
    kmcp_server_manager_stub.c
)

# Add system test executable
add_executable(kmcp_system_tests ${KMCP_SYSTEM_TEST_SOURCES})

# Link with KMCP library
target_link_libraries(kmcp_system_tests kmcp mcp_common)

# Add include directories
target_include_directories(kmcp_system_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
)

# Add system test
add_test(NAME kmcp_system_tests COMMAND kmcp_system_tests)

# Add individual system test executables for easier debugging
add_executable(system_kmcp_profile_management_test kmcp_profile_management_test.c kmcp_server_manager_stub.c)
target_link_libraries(system_kmcp_profile_management_test kmcp mcp_common)
target_include_directories(system_kmcp_profile_management_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
    ${CMAKE_CURRENT_SOURCE_DIR}/../unit
)
target_compile_definitions(system_kmcp_profile_management_test PRIVATE STANDALONE_TEST)

# Add individual system tests
add_test(NAME system_kmcp_profile_management_test COMMAND system_kmcp_profile_management_test)

# Set test properties
set_tests_properties(
    system_kmcp_profile_management_test
    PROPERTIES LABELS "system_tests"
)
