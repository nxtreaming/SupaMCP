# KMCP Performance Tests

This directory contains performance tests for the KMCP module. Performance tests verify the performance and resource usage of the system.

## Test Categories

1. **Tool Call Throughput**: Measures the number of tool calls per second
2. **Resource Access Throughput**: Measures the number of resource accesses per second
3. **Server Startup Time**: Measures the time to start a server
4. **Memory Usage**: Measures memory usage during operation
5. **CPU Usage**: Measures CPU usage during operation
6. **Concurrent Tool Calls**: Measures performance with concurrent tool calls
7. **Long-Running Operation**: Measures performance during long-running operations

## Running Tests

```bash
# Build and run all performance tests
cd build
ctest -R performance_tests
```

## Adding New Tests

To add a new performance test:

1. Create a new test file in this directory
2. Add the test to the CMakeLists.txt file
3. Update this README.md file with the new test category
