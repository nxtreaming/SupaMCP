# KMCP Testing

This directory contains tests for the KMCP module.

## Test Structure

The tests are organized into the following categories:

- **Unit Tests**: Tests for individual components in isolation
- **Integration Tests**: Tests for component interactions
- **System Tests**: Tests for the entire system
- **Performance Tests**: Tests for performance and resource usage

## Running Tests

### Prerequisites

- CMake 3.15+
- C Compiler (MSVC, GCC, or Clang)
- OpenSSL 1.1.1+
- libwebsockets 4.0+

### Building and Running Tests

```bash
# Create build directory
mkdir -p build
cd build

# Configure with testing enabled
cmake -DBUILD_TESTING=ON ..

# Build
cmake --build .

# Run all tests
ctest

# Run specific test category
ctest -R unit_tests
ctest -R integration_tests
ctest -R system_tests
ctest -R performance_tests
```

## Test Categories

### Unit Tests

Unit tests verify the functionality of individual components in isolation. They are located in the `unit` directory.

### Integration Tests

Integration tests verify the interaction between components. They are located in the `integration` directory.

### System Tests

System tests verify the functionality of the entire system. They are located in the `system` directory.

### Performance Tests

Performance tests verify the performance and resource usage of the system. They are located in the `performance` directory.

## Adding New Tests

To add a new test:

1. Create a new test file in the appropriate directory
2. Add the test to the CMake configuration
3. Update the test documentation

## Test Coverage

Test coverage reports are generated during the build process. They can be found in the `build/coverage` directory.

## Continuous Integration

Tests are automatically run on GitHub Actions for each pull request and push to the main branch.
