# KMCP Integration Tests

This directory contains integration tests for the KMCP module. Integration tests verify the interaction between components.

## Test Categories

1. **Client and Server Manager Integration**: Tests the interaction between the client and server manager components
2. **Client and Tool Access Control Integration**: Tests the interaction between the client and tool access control components
3. **Client and HTTP Client Integration**: Tests the interaction between the client and HTTP client components
4. **Server Manager and Process Management Integration**: Tests the interaction between the server manager and process management components
5. **Client and Profile Manager Integration**: Tests the interaction between the client and profile manager components
6. **Client and Registry Integration**: Tests the interaction between the client and registry components
7. **Tool SDK and Client Integration**: Tests the interaction between the tool SDK and client components
8. **Profile Manager and Registry Integration**: Tests the interaction between the profile manager and registry components

## Running Tests

```bash
# Build and run all integration tests
cd build
ctest -R integration_tests
```

## Adding New Tests

To add a new integration test:

1. Create a new test file in this directory
2. Add the test to the CMakeLists.txt file
3. Update this README.md file with the new test category
