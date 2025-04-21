# CMake file for KMCP performance tests

# Add performance test sources
set(KMCP_PERFORMANCE_TEST_SOURCES
    kmcp_performance_test_runner.c
    kmcp_tool_call_throughput_test.c
    kmcp_server_manager_stub.c
)

# Add performance test executable
add_executable(kmcp_performance_tests ${KMCP_PERFORMANCE_TEST_SOURCES})

# Link with KMCP library
target_link_libraries(kmcp_performance_tests kmcp mcp_common)

# Add include directories
target_include_directories(kmcp_performance_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
)

# Add performance test
add_test(NAME kmcp_performance_tests COMMAND kmcp_performance_tests)

# Add individual performance test executables for easier debugging
add_executable(perf_kmcp_tool_call_throughput_test kmcp_tool_call_throughput_test.c kmcp_server_manager_stub.c)
target_link_libraries(perf_kmcp_tool_call_throughput_test kmcp mcp_common)
target_include_directories(perf_kmcp_tool_call_throughput_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/kmcp
    ${CMAKE_SOURCE_DIR}/src/kmcp
    ${CMAKE_CURRENT_SOURCE_DIR}/../unit
)
target_compile_definitions(perf_kmcp_tool_call_throughput_test PRIVATE STANDALONE_TEST)

# Add individual performance tests
add_test(NAME perf_kmcp_tool_call_throughput_test COMMAND perf_kmcp_tool_call_throughput_test)

# Set test properties
set_tests_properties(
    perf_kmcp_tool_call_throughput_test
    PROPERTIES LABELS "performance_tests"
)
