# KMCP Test Report Generator Script

# Get current date and time
$date = Get-Date -Format "yyyy-MM-dd"
$time = Get-Date -Format "HH:mm:ss"

# Get operating system information
$os = [System.Environment]::OSVersion.ToString()

# Get compiler information
$compiler = "Unknown"
if (Test-Path -Path "build\CMakeCache.txt") {
    $compiler = (Get-Content -Path "build\CMakeCache.txt" | Select-String -Pattern "CMAKE_CXX_COMPILER:FILEPATH=").ToString()
    if ($compiler) {
        $compiler = $compiler.Split("=")[1]
    } else {
        $compiler = "Unknown"
    }
}

# Get KMCP version
$kmcp_version = "Unknown"
if (Test-Path -Path "include\kmcp\kmcp_version.h") {
    $version_file = Get-Content -Path "include\kmcp\kmcp_version.h"
    $major = ($version_file | Select-String -Pattern "#define KMCP_VERSION_MAJOR").ToString().Split(" ")[-1]
    $minor = ($version_file | Select-String -Pattern "#define KMCP_VERSION_MINOR").ToString().Split(" ")[-1]
    $patch = ($version_file | Select-String -Pattern "#define KMCP_VERSION_PATCH").ToString().Split(" ")[-1]
    $kmcp_version = "$major.$minor.$patch"
}

# Run tests
Write-Host "Running tests..."
Set-Location -Path "build"
ctest -V > test_output.txt
Set-Location -Path ".."

# Parse test results
$test_output = Get-Content -Path "build\test_output.txt"

# Initialize counters
$unit_total = 0
$unit_passed = 0
$unit_failed = 0
$unit_skipped = 0

$integration_total = 0
$integration_passed = 0
$integration_failed = 0
$integration_skipped = 0

$system_total = 0
$system_passed = 0
$system_failed = 0
$system_skipped = 0

$performance_total = 0
$performance_passed = 0
$performance_failed = 0
$performance_skipped = 0

# Parse test output
foreach ($line in $test_output) {
    if ($line -match "Test #(\d+): (.+)") {
        $test_name = $matches[2]
        
        # Determine test category
        $category = "unknown"
        if ($test_name -match "unit_tests") {
            $category = "unit"
        } elseif ($test_name -match "integration_tests") {
            $category = "integration"
        } elseif ($test_name -match "system_tests") {
            $category = "system"
        } elseif ($test_name -match "performance_tests") {
            $category = "performance"
        }
        
        # Find test result
        $result_line = $test_output | Select-String -Pattern "Test #\d+: $test_name .*" | Select-Object -First 1
        if ($result_line -match "Passed") {
            switch ($category) {
                "unit" { $unit_passed++ }
                "integration" { $integration_passed++ }
                "system" { $system_passed++ }
                "performance" { $performance_passed++ }
            }
        } elseif ($result_line -match "Failed") {
            switch ($category) {
                "unit" { $unit_failed++ }
                "integration" { $integration_failed++ }
                "system" { $system_failed++ }
                "performance" { $performance_failed++ }
            }
        } elseif ($result_line -match "Not Run") {
            switch ($category) {
                "unit" { $unit_skipped++ }
                "integration" { $integration_skipped++ }
                "system" { $system_skipped++ }
                "performance" { $performance_skipped++ }
            }
        }
        
        # Increment total
        switch ($category) {
            "unit" { $unit_total++ }
            "integration" { $integration_total++ }
            "system" { $system_total++ }
            "performance" { $performance_total++ }
        }
    }
}

# Calculate totals
$total = $unit_total + $integration_total + $system_total + $performance_total
$passed = $unit_passed + $integration_passed + $system_passed + $performance_passed
$failed = $unit_failed + $integration_failed + $system_failed + $performance_failed
$skipped = $unit_skipped + $integration_skipped + $system_skipped + $performance_skipped

# Extract performance metrics
$tool_call_throughput = "N/A"
$tool_call_min_response_time = "N/A"
$tool_call_max_response_time = "N/A"
$tool_call_avg_response_time = "N/A"

$resource_access_throughput = "N/A"
$resource_access_min_response_time = "N/A"
$resource_access_max_response_time = "N/A"
$resource_access_avg_response_time = "N/A"

$server_startup_min_time = "N/A"
$server_startup_max_time = "N/A"
$server_startup_avg_time = "N/A"

$memory_usage_min = "N/A"
$memory_usage_max = "N/A"
$memory_usage_avg = "N/A"

$cpu_usage_min = "N/A"
$cpu_usage_max = "N/A"
$cpu_usage_avg = "N/A"

$concurrency_level = "N/A"
$concurrent_tool_calls_throughput = "N/A"
$concurrent_tool_calls_min_response_time = "N/A"
$concurrent_tool_calls_max_response_time = "N/A"
$concurrent_tool_calls_avg_response_time = "N/A"

$long_running_operation_duration = "N/A"
$long_running_operation_memory_usage = "N/A"
$long_running_operation_cpu_usage = "N/A"

# Extract performance metrics from test output
foreach ($line in $test_output) {
    if ($line -match "Tool call throughput: ([\d\.]+) calls/second") {
        $tool_call_throughput = $matches[1]
    } elseif ($line -match "Average response time: ([\d\.]+) ms") {
        $tool_call_avg_response_time = $matches[1]
    }
    # Add more patterns to extract other performance metrics
}

# Generate test details
$unit_test_details = "No unit tests were run."
$integration_test_details = "No integration tests were run."
$system_test_details = "No system tests were run."
$performance_test_details = "No performance tests were run."

# Generate test details from test output
if ($unit_total -gt 0) {
    $unit_test_details = "| Test | Result |\n|------|--------|\n"
    foreach ($line in $test_output) {
        if ($line -match "Test #(\d+): (.+)") {
            $test_name = $matches[2]
            if ($test_name -match "unit_tests") {
                $result_line = $test_output | Select-String -Pattern "Test #\d+: $test_name .*" | Select-Object -First 1
                if ($result_line -match "Passed") {
                    $unit_test_details += "| $test_name | Passed |\n"
                } elseif ($result_line -match "Failed") {
                    $unit_test_details += "| $test_name | Failed |\n"
                } elseif ($result_line -match "Not Run") {
                    $unit_test_details += "| $test_name | Skipped |\n"
                }
            }
        }
    }
}

if ($integration_total -gt 0) {
    $integration_test_details = "| Test | Result |\n|------|--------|\n"
    foreach ($line in $test_output) {
        if ($line -match "Test #(\d+): (.+)") {
            $test_name = $matches[2]
            if ($test_name -match "integration_tests") {
                $result_line = $test_output | Select-String -Pattern "Test #\d+: $test_name .*" | Select-Object -First 1
                if ($result_line -match "Passed") {
                    $integration_test_details += "| $test_name | Passed |\n"
                } elseif ($result_line -match "Failed") {
                    $integration_test_details += "| $test_name | Failed |\n"
                } elseif ($result_line -match "Not Run") {
                    $integration_test_details += "| $test_name | Skipped |\n"
                }
            }
        }
    }
}

if ($system_total -gt 0) {
    $system_test_details = "| Test | Result |\n|------|--------|\n"
    foreach ($line in $test_output) {
        if ($line -match "Test #(\d+): (.+)") {
            $test_name = $matches[2]
            if ($test_name -match "system_tests") {
                $result_line = $test_output | Select-String -Pattern "Test #\d+: $test_name .*" | Select-Object -First 1
                if ($result_line -match "Passed") {
                    $system_test_details += "| $test_name | Passed |\n"
                } elseif ($result_line -match "Failed") {
                    $system_test_details += "| $test_name | Failed |\n"
                } elseif ($result_line -match "Not Run") {
                    $system_test_details += "| $test_name | Skipped |\n"
                }
            }
        }
    }
}

if ($performance_total -gt 0) {
    $performance_test_details = "| Test | Result |\n|------|--------|\n"
    foreach ($line in $test_output) {
        if ($line -match "Test #(\d+): (.+)") {
            $test_name = $matches[2]
            if ($test_name -match "performance_tests") {
                $result_line = $test_output | Select-String -Pattern "Test #\d+: $test_name .*" | Select-Object -First 1
                if ($result_line -match "Passed") {
                    $performance_test_details += "| $test_name | Passed |\n"
                } elseif ($result_line -match "Failed") {
                    $performance_test_details += "| $test_name | Failed |\n"
                } elseif ($result_line -match "Not Run") {
                    $performance_test_details += "| $test_name | Skipped |\n"
                }
            }
        }
    }
}

# Generate code coverage data
$client_line_coverage = "N/A"
$client_function_coverage = "N/A"
$client_branch_coverage = "N/A"

$server_manager_line_coverage = "N/A"
$server_manager_function_coverage = "N/A"
$server_manager_branch_coverage = "N/A"

$config_parser_line_coverage = "N/A"
$config_parser_function_coverage = "N/A"
$config_parser_branch_coverage = "N/A"

$tool_access_line_coverage = "N/A"
$tool_access_function_coverage = "N/A"
$tool_access_branch_coverage = "N/A"

$http_client_line_coverage = "N/A"
$http_client_function_coverage = "N/A"
$http_client_branch_coverage = "N/A"

$process_line_coverage = "N/A"
$process_function_coverage = "N/A"
$process_branch_coverage = "N/A"

$profile_manager_line_coverage = "N/A"
$profile_manager_function_coverage = "N/A"
$profile_manager_branch_coverage = "N/A"

$registry_line_coverage = "N/A"
$registry_function_coverage = "N/A"
$registry_branch_coverage = "N/A"

$tool_sdk_line_coverage = "N/A"
$tool_sdk_function_coverage = "N/A"
$tool_sdk_branch_coverage = "N/A"

$error_line_coverage = "N/A"
$error_function_coverage = "N/A"
$error_branch_coverage = "N/A"

$total_line_coverage = "N/A"
$total_function_coverage = "N/A"
$total_branch_coverage = "N/A"

# Generate issues found
$issues_found = "No issues were found."

# Generate recommendations
$recommendations = "No recommendations at this time."

# Read the template
$template = Get-Content -Path "tests\kmcp\test_report_template.md" -Raw

# Replace placeholders
$template = $template.Replace('${DATE}', $date)
$template = $template.Replace('${TIME}', $time)
$template = $template.Replace('${OS}', $os)
$template = $template.Replace('${COMPILER}', $compiler)
$template = $template.Replace('${KMCP_VERSION}', $kmcp_version)

$template = $template.Replace('${UNIT_TOTAL}', $unit_total)
$template = $template.Replace('${UNIT_PASSED}', $unit_passed)
$template = $template.Replace('${UNIT_FAILED}', $unit_failed)
$template = $template.Replace('${UNIT_SKIPPED}', $unit_skipped)

$template = $template.Replace('${INTEGRATION_TOTAL}', $integration_total)
$template = $template.Replace('${INTEGRATION_PASSED}', $integration_passed)
$template = $template.Replace('${INTEGRATION_FAILED}', $integration_failed)
$template = $template.Replace('${INTEGRATION_SKIPPED}', $integration_skipped)

$template = $template.Replace('${SYSTEM_TOTAL}', $system_total)
$template = $template.Replace('${SYSTEM_PASSED}', $system_passed)
$template = $template.Replace('${SYSTEM_FAILED}', $system_failed)
$template = $template.Replace('${SYSTEM_SKIPPED}', $system_skipped)

$template = $template.Replace('${PERFORMANCE_TOTAL}', $performance_total)
$template = $template.Replace('${PERFORMANCE_PASSED}', $performance_passed)
$template = $template.Replace('${PERFORMANCE_FAILED}', $performance_failed)
$template = $template.Replace('${PERFORMANCE_SKIPPED}', $performance_skipped)

$template = $template.Replace('${TOTAL}', $total)
$template = $template.Replace('${PASSED}', $passed)
$template = $template.Replace('${FAILED}', $failed)
$template = $template.Replace('${SKIPPED}', $skipped)

$template = $template.Replace('${UNIT_TEST_DETAILS}', $unit_test_details)
$template = $template.Replace('${INTEGRATION_TEST_DETAILS}', $integration_test_details)
$template = $template.Replace('${SYSTEM_TEST_DETAILS}', $system_test_details)
$template = $template.Replace('${PERFORMANCE_TEST_DETAILS}', $performance_test_details)

$template = $template.Replace('${TOOL_CALL_THROUGHPUT_TEST}', "kmcp_tool_call_throughput_test")
$template = $template.Replace('${TOOL_CALL_THROUGHPUT}', $tool_call_throughput)
$template = $template.Replace('${TOOL_CALL_MIN_RESPONSE_TIME}', $tool_call_min_response_time)
$template = $template.Replace('${TOOL_CALL_MAX_RESPONSE_TIME}', $tool_call_max_response_time)
$template = $template.Replace('${TOOL_CALL_AVG_RESPONSE_TIME}', $tool_call_avg_response_time)

$template = $template.Replace('${RESOURCE_ACCESS_THROUGHPUT_TEST}', "kmcp_resource_access_throughput_test")
$template = $template.Replace('${RESOURCE_ACCESS_THROUGHPUT}', $resource_access_throughput)
$template = $template.Replace('${RESOURCE_ACCESS_MIN_RESPONSE_TIME}', $resource_access_min_response_time)
$template = $template.Replace('${RESOURCE_ACCESS_MAX_RESPONSE_TIME}', $resource_access_max_response_time)
$template = $template.Replace('${RESOURCE_ACCESS_AVG_RESPONSE_TIME}', $resource_access_avg_response_time)

$template = $template.Replace('${SERVER_STARTUP_TIME_TEST}', "kmcp_server_startup_time_test")
$template = $template.Replace('${SERVER_STARTUP_MIN_TIME}', $server_startup_min_time)
$template = $template.Replace('${SERVER_STARTUP_MAX_TIME}', $server_startup_max_time)
$template = $template.Replace('${SERVER_STARTUP_AVG_TIME}', $server_startup_avg_time)

$template = $template.Replace('${MEMORY_USAGE_TEST}', "kmcp_memory_usage_test")
$template = $template.Replace('${MEMORY_USAGE_MIN}', $memory_usage_min)
$template = $template.Replace('${MEMORY_USAGE_MAX}', $memory_usage_max)
$template = $template.Replace('${MEMORY_USAGE_AVG}', $memory_usage_avg)

$template = $template.Replace('${CPU_USAGE_TEST}', "kmcp_cpu_usage_test")
$template = $template.Replace('${CPU_USAGE_MIN}', $cpu_usage_min)
$template = $template.Replace('${CPU_USAGE_MAX}', $cpu_usage_max)
$template = $template.Replace('${CPU_USAGE_AVG}', $cpu_usage_avg)

$template = $template.Replace('${CONCURRENT_TOOL_CALLS_TEST}', "kmcp_concurrent_tool_calls_test")
$template = $template.Replace('${CONCURRENCY_LEVEL}', $concurrency_level)
$template = $template.Replace('${CONCURRENT_TOOL_CALLS_THROUGHPUT}', $concurrent_tool_calls_throughput)
$template = $template.Replace('${CONCURRENT_TOOL_CALLS_MIN_RESPONSE_TIME}', $concurrent_tool_calls_min_response_time)
$template = $template.Replace('${CONCURRENT_TOOL_CALLS_MAX_RESPONSE_TIME}', $concurrent_tool_calls_max_response_time)
$template = $template.Replace('${CONCURRENT_TOOL_CALLS_AVG_RESPONSE_TIME}', $concurrent_tool_calls_avg_response_time)

$template = $template.Replace('${LONG_RUNNING_OPERATION_TEST}', "kmcp_long_running_operation_test")
$template = $template.Replace('${LONG_RUNNING_OPERATION_DURATION}', $long_running_operation_duration)
$template = $template.Replace('${LONG_RUNNING_OPERATION_MEMORY_USAGE}', $long_running_operation_memory_usage)
$template = $template.Replace('${LONG_RUNNING_OPERATION_CPU_USAGE}', $long_running_operation_cpu_usage)

$template = $template.Replace('${CLIENT_LINE_COVERAGE}', $client_line_coverage)
$template = $template.Replace('${CLIENT_FUNCTION_COVERAGE}', $client_function_coverage)
$template = $template.Replace('${CLIENT_BRANCH_COVERAGE}', $client_branch_coverage)

$template = $template.Replace('${SERVER_MANAGER_LINE_COVERAGE}', $server_manager_line_coverage)
$template = $template.Replace('${SERVER_MANAGER_FUNCTION_COVERAGE}', $server_manager_function_coverage)
$template = $template.Replace('${SERVER_MANAGER_BRANCH_COVERAGE}', $server_manager_branch_coverage)

$template = $template.Replace('${CONFIG_PARSER_LINE_COVERAGE}', $config_parser_line_coverage)
$template = $template.Replace('${CONFIG_PARSER_FUNCTION_COVERAGE}', $config_parser_function_coverage)
$template = $template.Replace('${CONFIG_PARSER_BRANCH_COVERAGE}', $config_parser_branch_coverage)

$template = $template.Replace('${TOOL_ACCESS_LINE_COVERAGE}', $tool_access_line_coverage)
$template = $template.Replace('${TOOL_ACCESS_FUNCTION_COVERAGE}', $tool_access_function_coverage)
$template = $template.Replace('${TOOL_ACCESS_BRANCH_COVERAGE}', $tool_access_branch_coverage)

$template = $template.Replace('${HTTP_CLIENT_LINE_COVERAGE}', $http_client_line_coverage)
$template = $template.Replace('${HTTP_CLIENT_FUNCTION_COVERAGE}', $http_client_function_coverage)
$template = $template.Replace('${HTTP_CLIENT_BRANCH_COVERAGE}', $http_client_branch_coverage)

$template = $template.Replace('${PROCESS_LINE_COVERAGE}', $process_line_coverage)
$template = $template.Replace('${PROCESS_FUNCTION_COVERAGE}', $process_function_coverage)
$template = $template.Replace('${PROCESS_BRANCH_COVERAGE}', $process_branch_coverage)

$template = $template.Replace('${PROFILE_MANAGER_LINE_COVERAGE}', $profile_manager_line_coverage)
$template = $template.Replace('${PROFILE_MANAGER_FUNCTION_COVERAGE}', $profile_manager_function_coverage)
$template = $template.Replace('${PROFILE_MANAGER_BRANCH_COVERAGE}', $profile_manager_branch_coverage)

$template = $template.Replace('${REGISTRY_LINE_COVERAGE}', $registry_line_coverage)
$template = $template.Replace('${REGISTRY_FUNCTION_COVERAGE}', $registry_function_coverage)
$template = $template.Replace('${REGISTRY_BRANCH_COVERAGE}', $registry_branch_coverage)

$template = $template.Replace('${TOOL_SDK_LINE_COVERAGE}', $tool_sdk_line_coverage)
$template = $template.Replace('${TOOL_SDK_FUNCTION_COVERAGE}', $tool_sdk_function_coverage)
$template = $template.Replace('${TOOL_SDK_BRANCH_COVERAGE}', $tool_sdk_branch_coverage)

$template = $template.Replace('${ERROR_LINE_COVERAGE}', $error_line_coverage)
$template = $template.Replace('${ERROR_FUNCTION_COVERAGE}', $error_function_coverage)
$template = $template.Replace('${ERROR_BRANCH_COVERAGE}', $error_branch_coverage)

$template = $template.Replace('${TOTAL_LINE_COVERAGE}', $total_line_coverage)
$template = $template.Replace('${TOTAL_FUNCTION_COVERAGE}', $total_function_coverage)
$template = $template.Replace('${TOTAL_BRANCH_COVERAGE}', $total_branch_coverage)

$template = $template.Replace('${ISSUES_FOUND}', $issues_found)
$template = $template.Replace('${RECOMMENDATIONS}', $recommendations)

# Write the report
$report_file = "tests\kmcp\test_report_$(Get-Date -Format 'yyyyMMdd_HHmmss').md"
$template | Out-File -FilePath $report_file

Write-Host "Test report generated: $report_file"
