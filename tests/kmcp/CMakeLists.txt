# CMake file for KMCP tests

# Add test sources
set(KMCP_TEST_SOURCES
    kmcp_test_runner.c
    kmcp_error_test.c
    kmcp_process_test.c
    kmcp_http_client_test.c
    kmcp_server_manager_test.c
    kmcp_tool_access_test.c
    kmcp_config_parser_test.c
    kmcp_version_test.c
)

# Add test executable
add_executable(kmcp_tests ${KMCP_TEST_SOURCES})

# Link with KMCP library
target_link_libraries(kmcp_tests kmcp mcp_common)

# Add include directories
target_include_directories(kmcp_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
)

# Add test
add_test(NAME kmcp_tests COMMAND kmcp_tests)

# Add individual test executables for easier debugging
add_executable(kmcp_error_test kmcp_error_test.c)
target_link_libraries(kmcp_error_test kmcp mcp_common)
target_include_directories(kmcp_error_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
)
target_compile_definitions(kmcp_error_test PRIVATE STANDALONE_TEST)

add_executable(kmcp_process_test kmcp_process_test.c)
target_link_libraries(kmcp_process_test kmcp mcp_common)
target_include_directories(kmcp_process_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
)
target_compile_definitions(kmcp_process_test PRIVATE STANDALONE_TEST)

add_executable(kmcp_http_client_test kmcp_http_client_test.c)
target_link_libraries(kmcp_http_client_test kmcp mcp_common)
target_include_directories(kmcp_http_client_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
)
target_compile_definitions(kmcp_http_client_test PRIVATE STANDALONE_TEST)

add_executable(kmcp_server_manager_test kmcp_server_manager_test.c)
target_link_libraries(kmcp_server_manager_test kmcp mcp_common)
target_include_directories(kmcp_server_manager_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
)
target_compile_definitions(kmcp_server_manager_test PRIVATE STANDALONE_TEST)

add_executable(kmcp_tool_access_test kmcp_tool_access_test.c)
target_link_libraries(kmcp_tool_access_test kmcp mcp_common)
target_include_directories(kmcp_tool_access_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
)
target_compile_definitions(kmcp_tool_access_test PRIVATE STANDALONE_TEST)

add_executable(kmcp_config_parser_test kmcp_config_parser_test.c)
target_link_libraries(kmcp_config_parser_test kmcp mcp_common)
target_include_directories(kmcp_config_parser_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
)
target_compile_definitions(kmcp_config_parser_test PRIVATE STANDALONE_TEST)

add_executable(kmcp_version_test kmcp_version_test.c)
target_link_libraries(kmcp_version_test kmcp mcp_common)
target_include_directories(kmcp_version_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
)
target_compile_definitions(kmcp_version_test PRIVATE STANDALONE_TEST)

# Add individual tests
add_test(NAME kmcp_error_test COMMAND kmcp_error_test)
add_test(NAME kmcp_process_test COMMAND kmcp_process_test)
add_test(NAME kmcp_http_client_test COMMAND kmcp_http_client_test)
add_test(NAME kmcp_server_manager_test COMMAND kmcp_server_manager_test)
add_test(NAME kmcp_tool_access_test COMMAND kmcp_tool_access_test)
add_test(NAME kmcp_config_parser_test COMMAND kmcp_config_parser_test)
add_test(NAME kmcp_version_test COMMAND kmcp_version_test)
