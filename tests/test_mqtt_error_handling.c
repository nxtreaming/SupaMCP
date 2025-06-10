/**
 * @file test_mqtt_error_handling.c
 * @brief Test cases for MQTT error handling improvements
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define access _access
#define F_OK 0
#define unlink _unlink
#define mkdir _mkdir
#define rmdir _rmdir
#else
#include <unistd.h>
#endif

// Include the MQTT session persistence header
#include "transport/internal/mqtt_session_persistence.h"
#include "mcp_log.h"

/**
 * @brief Test safe file write operations
 */
static void test_safe_file_operations(void) {
    printf("Testing safe file operations...\n");
    
    // Test 1: Normal file write
    FILE* fp = fopen("test_session.dat", "wb");
    assert(fp != NULL);

    uint32_t test_data = 0x12345678;
    // Write some test data to verify file operations work
    size_t written = fwrite(&test_data, sizeof(test_data), 1, fp);
    assert(written == 1);

    fclose(fp);
    unlink("test_session.dat");
    
    printf("Basic file operations test passed\n");
}

/**
 * @brief Test session persistence error handling
 */
static void test_session_persistence_errors(void) {
    printf("Testing session persistence error handling...\n");
    
    // Initialize persistence system
    const char* test_dir = "./test_sessions";

#ifdef _WIN32
    mkdir(test_dir);
#else
    mkdir(test_dir, 0755);
#endif
    
    int result = mqtt_session_persistence_init(test_dir);
    assert(result == 0);
    
    // Test 1: Save session with valid data
    mqtt_session_data_t session = {0};
    session.session_created_time = 1234567890;
    session.session_last_access_time = 1234567890;
    session.session_expiry_interval = 3600;
    session.last_packet_id = 42;
    
    result = mqtt_session_save("test_client", &session);
    assert(result == 0);
    
    // Test 2: Load the saved session
    mqtt_session_data_t loaded_session = {0};
    result = mqtt_session_load("test_client", &loaded_session);
    assert(result == 0);
    assert(loaded_session.last_packet_id == 42);
    
    // Test 3: Try to save with NULL client_id (should fail)
    result = mqtt_session_save(NULL, &session);
    assert(result == -1);
    
    // Test 4: Try to load with NULL session (should fail)
    result = mqtt_session_load("test_client", NULL);
    assert(result == -1);
    
    // Test 5: Check if session exists
    bool exists = mqtt_session_exists("test_client");
    assert(exists == true);
    
    exists = mqtt_session_exists("nonexistent_client");
    assert(exists == false);
    
    // Test 6: Delete session
    result = mqtt_session_delete("test_client");
    assert(result == 0);
    
    exists = mqtt_session_exists("test_client");
    assert(exists == false);
    
    // Cleanup
    mqtt_session_persistence_cleanup();

#ifdef _WIN32
    rmdir(test_dir);
#else
    rmdir(test_dir);
#endif
    
    printf("Session persistence error handling tests passed\n");
}

/**
 * @brief Test error handling with invalid paths
 */
static void test_invalid_path_handling(void) {
    printf("Testing invalid path handling...\n");
    
    // Test 1: Initialize with NULL path (should fail)
    int result = mqtt_session_persistence_init(NULL);
    assert(result == -1);
    
    // Test 2: Initialize with empty path (should fail)
    result = mqtt_session_persistence_init("");
    assert(result == -1);
    
    // Test 3: Try operations without initialization
    mqtt_session_data_t session = {0};
    result = mqtt_session_save("test", &session);
    assert(result == -1);
    
    result = mqtt_session_load("test", &session);
    assert(result == -1);
    
    bool exists = mqtt_session_exists("test");
    assert(exists == false);
    
    printf("Invalid path handling tests passed\n");
}

/**
 * @brief Test logging error handling
 */
static void test_logging_error_handling(void) {
    printf("Testing logging error handling...\n");
    
    // Test 1: Initialize logging with valid file
    int result = mcp_log_init("test.log", MCP_LOG_LEVEL_DEBUG);
    assert(result == 0);
    
    // Test 2: Log some messages to test safe_fprintf
    mcp_log_info("Test info message");
    mcp_log_error("Test error message");
    mcp_log_debug("Test debug message");
    
    // Test 3: Close logging
    mcp_log_close();
    
    // Cleanup
    unlink("test.log");
    
    printf("Logging error handling tests passed\n");
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("Starting MQTT error handling tests...\n\n");
    
    // Initialize logging for tests
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    
    test_safe_file_operations();
    test_session_persistence_errors();
    test_invalid_path_handling();
    test_logging_error_handling();
    
    printf("\nAll MQTT error handling tests passed!\n");
    return 0;
}
