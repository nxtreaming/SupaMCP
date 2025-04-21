# KMCP Unit Tests

This directory contains unit tests for the KMCP module. Unit tests verify the functionality of individual components in isolation.

## Test Categories

1. **Client**: Tests for the client component
2. **Server Manager**: Tests for the server manager component
3. **Configuration Parser**: Tests for the configuration parser component
4. **Tool Access Control**: Tests for the tool access control component
5. **HTTP Client**: Tests for the HTTP client component
6. **Process Management**: Tests for the process management component
7. **Profile Manager**: Tests for the profile manager component
8. **Registry**: Tests for the registry component
9. **Tool SDK**: Tests for the tool SDK component
10. **Error Handling**: Tests for the error handling component

## Running Tests

```bash
# Build and run all unit tests
cd build
ctest -R unit_tests
```

## Adding New Tests

To add a new unit test:

1. Create a new test file in this directory
2. Add the test to the CMakeLists.txt file
3. Update this README.md file with the new test category
