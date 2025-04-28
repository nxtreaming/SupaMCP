# CMake file for KMCP integration tests

# Add integration test sources
set(KMCP_INTEGRATION_TEST_SOURCES
    kmcp_integration_test_runner.c
    kmcp_client_profile_manager_integration_test.c
    kmcp_server_manager_stub.c
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
add_executable(integration_kmcp_client_profile_manager_test kmcp_client_profile_manager_integration_test.c kmcp_server_manager_stub.c)
target_link_libraries(integration_kmcp_client_profile_manager_test kmcp mcpcore)
target_include_directories(integration_kmcp_client_profile_manager_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/client/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
    ${CMAKE_CURRENT_SOURCE_DIR}/../unit
)
target_compile_definitions(integration_kmcp_client_profile_manager_test PRIVATE STANDALONE_TEST)

# Add individual integration tests
add_test(NAME integration_kmcp_client_profile_manager_test COMMAND integration_kmcp_client_profile_manager_test)

# Set test properties
set_tests_properties(
    integration_kmcp_client_profile_manager_test
    PROPERTIES LABELS "integration_tests"
)
