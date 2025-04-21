# KMCP Unit Test Runner Script

# Run the unit tests
Write-Host "Running unit tests..."
Set-Location -Path "build"
ctest -R unit_tests -V
Set-Location -Path ".."

# Generate test report
Write-Host "Generating test report..."
.\tests\kmcp\generate_test_report.ps1

Write-Host "Unit tests completed."
