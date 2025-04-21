# KMCP System Tests

This directory contains system tests for the KMCP module. System tests verify the functionality of the entire system.

## Test Categories

1. **Basic Usage**: Tests the basic usage flow of the KMCP module
2. **Server Management**: Tests server management functionality
3. **Tool Access Control**: Tests tool access control functionality
4. **Profile Management**: Tests profile management functionality
5. **Registry Integration**: Tests registry integration functionality
6. **Tool SDK Integration**: Tests tool SDK integration functionality
7. **Error Handling**: Tests error handling functionality
8. **Resource Management**: Tests resource management functionality

## Running Tests

```bash
# Build and run all system tests
cd build
ctest -R system_tests
```

## Adding New Tests

To add a new system test:

1. Create a new test file in this directory
2. Add the test to the CMakeLists.txt file
3. Update this README.md file with the new test category
