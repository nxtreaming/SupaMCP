# KMCP All Tests Runner Script

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

# Return to original directory
Set-Location -Path ".."

# Run unit tests
Write-Host "Running unit tests..."
.\tests\kmcp\run_unit_tests.ps1

# Run integration tests
Write-Host "Running integration tests..."
.\tests\kmcp\run_integration_tests.ps1

# Run system tests
Write-Host "Running system tests..."
.\tests\kmcp\run_system_tests.ps1

# Run performance tests
Write-Host "Running performance tests..."
.\tests\kmcp\run_performance_tests.ps1

Write-Host "All tests completed."
