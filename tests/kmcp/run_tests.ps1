# KMCP Test Runner Script

# Create build directory if it doesn't exist
if (-not (Test-Path -Path "build")) {
    New-Item -ItemType Directory -Path "build"
}

# Change to build directory
Set-Location -Path "build"

# Configure with testing enabled
cmake -DBUILD_TESTING=ON ..

# Build
cmake --build .

# Run all tests
ctest

# Run specific test categories
Write-Host "Running unit tests..."
ctest -R unit_tests

Write-Host "Running integration tests..."
ctest -R integration_tests

Write-Host "Running system tests..."
ctest -R system_tests

Write-Host "Running performance tests..."
ctest -R performance_tests

# Return to original directory
Set-Location -Path ".."
