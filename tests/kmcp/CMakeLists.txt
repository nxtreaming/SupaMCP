# CMake file for KMCP tests

# Enable testing
enable_testing()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src
)

# Add test sources
set(KMCP_TEST_SOURCES
    kmcp_test_runner.c
    kmcp_error_test.c
    kmcp_event_test.c
    kmcp_profile_manager_test.c
    kmcp_client_test.c
    kmcp_version_test.c
    unit/kmcp_server_manager_stub.c
)

# Add test executable
add_executable(kmcp_tests ${KMCP_TEST_SOURCES})

# Link with KMCP library
target_link_libraries(kmcp_tests kmcp mcpcore)

# Add include directories
target_include_directories(kmcp_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/kmcp/include
    ${CMAKE_SOURCE_DIR}/kmcp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
)

# Add test
add_test(NAME kmcp_tests COMMAND kmcp_tests)

# Add individual test executables for easier debugging

# Error Test
add_executable(kmcp_error_test kmcp_error_test.c)
target_link_libraries(kmcp_error_test kmcp mcpcore)
target_include_directories(kmcp_error_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/kmcp/include
    ${CMAKE_SOURCE_DIR}/kmcp/src
)
target_compile_definitions(kmcp_error_test PRIVATE STANDALONE_TEST)

# Event Test
add_executable(kmcp_event_test kmcp_event_test.c)
target_link_libraries(kmcp_event_test kmcp mcpcore)
target_include_directories(kmcp_event_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/kmcp/include
    ${CMAKE_SOURCE_DIR}/kmcp/src
)
target_compile_definitions(kmcp_event_test PRIVATE STANDALONE_TEST)

# Profile Manager Test
add_executable(kmcp_profile_manager_test kmcp_profile_manager_test.c unit/kmcp_server_manager_stub.c)
target_link_libraries(kmcp_profile_manager_test kmcp mcpcore)
target_include_directories(kmcp_profile_manager_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/kmcp/include
    ${CMAKE_SOURCE_DIR}/kmcp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_compile_definitions(kmcp_profile_manager_test PRIVATE STANDALONE_TEST)

# Client Test
add_executable(kmcp_client_test kmcp_client_test.c unit/kmcp_server_manager_stub.c)
target_link_libraries(kmcp_client_test kmcp mcpcore)
target_include_directories(kmcp_client_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/kmcp/include
    ${CMAKE_SOURCE_DIR}/kmcp/src
    ${CMAKE_CURRENT_SOURCE_DIR}/unit
)
target_compile_definitions(kmcp_client_test PRIVATE STANDALONE_TEST)

# Version Test
add_executable(kmcp_version_test kmcp_version_test.c)
target_link_libraries(kmcp_version_test kmcp mcpcore)
target_include_directories(kmcp_version_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/kmcp/include
    ${CMAKE_SOURCE_DIR}/kmcp/src
)
target_compile_definitions(kmcp_version_test PRIVATE STANDALONE_TEST)

# Add individual tests
add_test(NAME kmcp_error_test COMMAND kmcp_error_test)
add_test(NAME kmcp_event_test COMMAND kmcp_event_test)
add_test(NAME kmcp_profile_manager_test COMMAND kmcp_profile_manager_test)
add_test(NAME kmcp_client_test COMMAND kmcp_client_test)
add_test(NAME kmcp_version_test COMMAND kmcp_version_test)

# Add subdirectories for new test structure
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit/CMakeLists.txt)
    add_subdirectory(unit)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/integration/CMakeLists.txt)
    add_subdirectory(integration)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/system/CMakeLists.txt)
    add_subdirectory(system)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/performance/CMakeLists.txt)
    add_subdirectory(performance)
endif()
