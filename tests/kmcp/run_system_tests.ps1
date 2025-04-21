# KMCP System Test Runner Script

# Start the MCP server
Write-Host "Starting MCP server..."
$mcp_server_process = Start-Process -FilePath "build\debug\mcp_server.exe" -ArgumentList "--tcp", "--port", "8080" -PassThru

# Wait for the server to start
Start-Sleep -Seconds 2

# Start the registry server
Write-Host "Starting registry server..."
$registry_server_process = Start-Process -FilePath "build\debug\test_registry_server.exe" -PassThru

# Wait for the registry server to start
Start-Sleep -Seconds 2

# Run the system tests
Write-Host "Running system tests..."
Set-Location -Path "build"
ctest -R system_tests -V
Set-Location -Path ".."

# Stop the registry server
Write-Host "Stopping registry server..."
Stop-Process -Id $registry_server_process.Id -Force

# Stop the MCP server
Write-Host "Stopping MCP server..."
Stop-Process -Id $mcp_server_process.Id -Force

# Generate test report
Write-Host "Generating test report..."
.\tests\kmcp\generate_test_report.ps1

Write-Host "System tests completed."
