# KMCP Performance Test Runner Script

# Start the MCP server
Write-Host "Starting MCP server..."
$mcp_server_process = Start-Process -FilePath "build\debug\mcp_server.exe" -ArgumentList "--tcp", "--port", "8080" -PassThru

# Wait for the server to start
Start-Sleep -Seconds 2

# Run the performance tests
Write-Host "Running performance tests..."
Set-Location -Path "build"
ctest -R performance_tests -V
Set-Location -Path ".."

# Stop the MCP server
Write-Host "Stopping MCP server..."
Stop-Process -Id $mcp_server_process.Id -Force

# Generate test report
Write-Host "Generating test report..."
.\tests\kmcp\generate_test_report.ps1

Write-Host "Performance tests completed."
