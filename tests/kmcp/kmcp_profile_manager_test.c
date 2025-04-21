/**
 * @file kmcp_profile_manager_test.c
 * @brief Test for KMCP profile manager
 */

#include "kmcp_profile_manager.h"
#include "kmcp_server_manager.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef STANDALONE_TEST
#define TEST_RESULT(name, result) \
    printf("Test %s: %s\n", name, (result) ? "PASSED" : "FAILED")
#else
#define TEST_RESULT(name, result) \
    if (!(result)) { \
        printf("Test %s: FAILED\n", name); \
        return 0; \
    }
#endif

/**
 * @brief Test profile creation and deletion
 */
static int test_profile_create_delete(void) {
    // Create profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    if (!manager) {
        printf("Failed to create profile manager\n");
        return 0;
    }

    // Create profile
    kmcp_error_t result = kmcp_profile_create(manager, "test_profile");
    if (result != KMCP_SUCCESS) {
        printf("Failed to create profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check if profile exists
    if (!kmcp_profile_exists(manager, "test_profile")) {
        printf("Profile does not exist after creation\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Delete profile
    result = kmcp_profile_delete(manager, "test_profile");
    if (result != KMCP_SUCCESS) {
        printf("Failed to delete profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check if profile exists
    if (kmcp_profile_exists(manager, "test_profile")) {
        printf("Profile still exists after deletion\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Close profile manager
    kmcp_profile_manager_close(manager);

    return 1;
}

/**
 * @brief Test profile activation and deactivation
 */
static int test_profile_activate_deactivate(void) {
    // Create profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    if (!manager) {
        printf("Failed to create profile manager\n");
        return 0;
    }

    // Create profiles
    kmcp_error_t result = kmcp_profile_create(manager, "profile1");
    if (result != KMCP_SUCCESS) {
        printf("Failed to create profile1: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    result = kmcp_profile_create(manager, "profile2");
    if (result != KMCP_SUCCESS) {
        printf("Failed to create profile2: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Activate profile1
    result = kmcp_profile_activate(manager, "profile1");
    if (result != KMCP_SUCCESS) {
        printf("Failed to activate profile1: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check active profile
    const char* active = kmcp_profile_get_active(manager);
    if (!active || strcmp(active, "profile1") != 0) {
        printf("Active profile is not profile1\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Activate profile2
    result = kmcp_profile_activate(manager, "profile2");
    if (result != KMCP_SUCCESS) {
        printf("Failed to activate profile2: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check active profile
    active = kmcp_profile_get_active(manager);
    if (!active || strcmp(active, "profile2") != 0) {
        printf("Active profile is not profile2\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Deactivate profile2
    result = kmcp_profile_deactivate(manager, "profile2");
    if (result != KMCP_SUCCESS) {
        printf("Failed to deactivate profile2: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check active profile
    active = kmcp_profile_get_active(manager);
    if (active) {
        printf("Active profile is not NULL after deactivation\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Close profile manager
    kmcp_profile_manager_close(manager);

    return 1;
}

/**
 * @brief Test profile server operations
 */
static int test_profile_server_operations(void) {
    // Create profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    if (!manager) {
        printf("Failed to create profile manager\n");
        return 0;
    }

    // Create profiles
    kmcp_error_t result = kmcp_profile_create(manager, "profile1");
    if (result != KMCP_SUCCESS) {
        printf("Failed to create profile1: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    result = kmcp_profile_create(manager, "profile2");
    if (result != KMCP_SUCCESS) {
        printf("Failed to create profile2: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Create server configuration
    kmcp_server_config_t config;
    memset(&config, 0, sizeof(config));
    config.name = "test_server";
    config.is_http = true;
    config.url = "http://localhost:8080";

    // Add server to profile1
    result = kmcp_profile_add_server(manager, "profile1", &config);
    if (result != KMCP_SUCCESS) {
        printf("Failed to add server to profile1: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Get server manager for profile1
    kmcp_server_manager_t* server_manager = kmcp_profile_get_server_manager(manager, "profile1");
    if (!server_manager) {
        printf("Failed to get server manager for profile1\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check if server exists in profile1
    kmcp_server_config_t* server_config = NULL;
    result = kmcp_server_get_config(server_manager, "test_server", &server_config);
    if (result != KMCP_SUCCESS || !server_config) {
        printf("Server not found in profile1\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Free server configuration
    kmcp_server_config_free(server_config);

    // Copy server to profile2
    result = kmcp_profile_copy_server(manager, "profile1", "test_server", "profile2", "copied_server");
    if (result != KMCP_SUCCESS) {
        printf("Failed to copy server to profile2: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Get server manager for profile2
    server_manager = kmcp_profile_get_server_manager(manager, "profile2");
    if (!server_manager) {
        printf("Failed to get server manager for profile2\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check if copied server exists in profile2
    server_config = NULL;
    result = kmcp_server_get_config(server_manager, "copied_server", &server_config);
    if (result != KMCP_SUCCESS || !server_config) {
        printf("Copied server not found in profile2\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Free server configuration
    kmcp_server_config_free(server_config);

    // Move server from profile1 to profile2
    result = kmcp_profile_move_server(manager, "profile1", "test_server", "profile2", "moved_server");
    if (result != KMCP_SUCCESS) {
        printf("Failed to move server to profile2: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Get server manager for profile1
    server_manager = kmcp_profile_get_server_manager(manager, "profile1");
    if (!server_manager) {
        printf("Failed to get server manager for profile1\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check if server was removed from profile1
    server_config = NULL;
    result = kmcp_server_get_config(server_manager, "test_server", &server_config);
    if (result == KMCP_SUCCESS && server_config) {
        printf("Server still exists in profile1 after move\n");
        kmcp_server_config_free(server_config);
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Get server manager for profile2
    server_manager = kmcp_profile_get_server_manager(manager, "profile2");
    if (!server_manager) {
        printf("Failed to get server manager for profile2\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check if moved server exists in profile2
    server_config = NULL;
    result = kmcp_server_get_config(server_manager, "moved_server", &server_config);
    if (result != KMCP_SUCCESS || !server_config) {
        printf("Moved server not found in profile2\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Free server configuration
    kmcp_server_config_free(server_config);

    // Close profile manager
    kmcp_profile_manager_close(manager);

    return 1;
}

/**
 * @brief Test profile save and load
 */
static int test_profile_save_load(void) {
    // Create profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    if (!manager) {
        printf("Failed to create profile manager\n");
        return 0;
    }

    // Create profile
    kmcp_error_t result = kmcp_profile_create(manager, "save_test_profile");
    if (result != KMCP_SUCCESS) {
        printf("Failed to create profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Create server configuration
    kmcp_server_config_t config;
    memset(&config, 0, sizeof(config));
    config.name = "test_server";
    config.is_http = true;
    config.url = "http://localhost:8080";

    // Add server to profile
    result = kmcp_profile_add_server(manager, "save_test_profile", &config);
    if (result != KMCP_SUCCESS) {
        printf("Failed to add server to profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Activate profile
    result = kmcp_profile_activate(manager, "save_test_profile");
    if (result != KMCP_SUCCESS) {
        printf("Failed to activate profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Save profiles to file
    result = kmcp_profile_save(manager, "test_profiles.json");
    if (result != KMCP_SUCCESS) {
        printf("Failed to save profiles: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Close profile manager
    kmcp_profile_manager_close(manager);

    // Create new profile manager
    manager = kmcp_profile_manager_create();
    if (!manager) {
        printf("Failed to create profile manager\n");
        return 0;
    }

    // Load profiles from file
    result = kmcp_profile_load(manager, "test_profiles.json");
    if (result != KMCP_SUCCESS) {
        printf("Failed to load profiles: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check if profile exists
    if (!kmcp_profile_exists(manager, "save_test_profile")) {
        printf("Profile does not exist after loading\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check active profile
    const char* active = kmcp_profile_get_active(manager);
    if (!active || strcmp(active, "save_test_profile") != 0) {
        printf("Active profile is not save_test_profile after loading\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Get server manager for profile
    kmcp_server_manager_t* server_manager = kmcp_profile_get_server_manager(manager, "save_test_profile");
    if (!server_manager) {
        printf("Failed to get server manager for profile\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check if server exists in profile
    kmcp_server_config_t* server_config = NULL;
    result = kmcp_server_get_config(server_manager, "test_server", &server_config);
    if (result != KMCP_SUCCESS || !server_config) {
        printf("Server not found in profile after loading\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check server configuration
    if (!server_config->is_http || !server_config->url || strcmp(server_config->url, "http://localhost:8080") != 0) {
        printf("Server configuration is incorrect after loading\n");
        kmcp_server_config_free(server_config);
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Free server configuration
    kmcp_server_config_free(server_config);

    // Close profile manager
    kmcp_profile_manager_close(manager);

    // Remove test file
    remove("test_profiles.json");

    return 1;
}

/**
 * @brief Test profile export and import
 */
static int test_profile_export_import(void) {
    // Create profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    if (!manager) {
        printf("Failed to create profile manager\n");
        return 0;
    }

    // Create profile
    kmcp_error_t result = kmcp_profile_create(manager, "export_test_profile");
    if (result != KMCP_SUCCESS) {
        printf("Failed to create profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Create server configuration
    kmcp_server_config_t config;
    memset(&config, 0, sizeof(config));
    config.name = "test_server";
    config.is_http = true;
    config.url = "http://localhost:8080";

    // Add server to profile
    result = kmcp_profile_add_server(manager, "export_test_profile", &config);
    if (result != KMCP_SUCCESS) {
        printf("Failed to add server to profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Export profile to file
    result = kmcp_profile_export(manager, "export_test_profile", "test_profile_export.json");
    if (result != KMCP_SUCCESS) {
        printf("Failed to export profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Delete profile
    result = kmcp_profile_delete(manager, "export_test_profile");
    if (result != KMCP_SUCCESS) {
        printf("Failed to delete profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Import profile from file
    result = kmcp_profile_import(manager, "test_profile_export.json", "imported_profile");
    if (result != KMCP_SUCCESS) {
        printf("Failed to import profile: %s\n", kmcp_error_message(result));
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check if profile exists
    if (!kmcp_profile_exists(manager, "imported_profile")) {
        printf("Profile does not exist after importing\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Get server manager for profile
    kmcp_server_manager_t* server_manager = kmcp_profile_get_server_manager(manager, "imported_profile");
    if (!server_manager) {
        printf("Failed to get server manager for profile\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check if server exists in profile
    kmcp_server_config_t* server_config = NULL;
    result = kmcp_server_get_config(server_manager, "test_server", &server_config);
    if (result != KMCP_SUCCESS || !server_config) {
        printf("Server not found in profile after importing\n");
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Check server configuration
    if (!server_config->is_http || !server_config->url || strcmp(server_config->url, "http://localhost:8080") != 0) {
        printf("Server configuration is incorrect after importing\n");
        kmcp_server_config_free(server_config);
        kmcp_profile_manager_close(manager);
        return 0;
    }

    // Free server configuration
    kmcp_server_config_free(server_config);

    // Close profile manager
    kmcp_profile_manager_close(manager);

    // Remove test file
    remove("test_profile_export.json");

    return 1;
}

/**
 * @brief Run all profile manager tests
 */
int kmcp_profile_manager_test(void) {
    int result = 1;

    // Run tests
    result &= test_profile_create_delete();
    TEST_RESULT("profile_create_delete", result);

    result &= test_profile_activate_deactivate();
    TEST_RESULT("profile_activate_deactivate", result);

    result &= test_profile_server_operations();
    TEST_RESULT("profile_server_operations", result);

    result &= test_profile_save_load();
    TEST_RESULT("profile_save_load", result);

    result &= test_profile_export_import();
    TEST_RESULT("profile_export_import", result);

    return result;
}

#ifdef STANDALONE_TEST
int main(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Run tests
    int result = kmcp_profile_manager_test();

    // Close logging
    mcp_log_close();

    return result ? 0 : 1;
}
#endif

/**
 * @brief Main entry point for profile manager tests when run from the test runner
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_profile_manager_test_main(void) {
    printf("Running profile manager tests...\n");

    // Run tests
    int result = kmcp_profile_manager_test();

    // Return 0 for success, non-zero for failure
    return result ? 0 : 1;
}
