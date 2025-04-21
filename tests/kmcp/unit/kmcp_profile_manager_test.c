#include "kmcp_profile_manager.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "kmcp_server_manager_stub.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declaration
int run_tests(void);

#ifdef STANDALONE_TEST
#define TEST_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", #condition, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while (0)

int main(void) {
    return run_tests();
}
#else
#define TEST_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", #condition, __FILE__, __LINE__); \
            return 0; \
        } \
    } while (0)
#endif

/**
 * Test creating and closing a profile manager
 */
static int test_profile_manager_create_close(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test creating and deleting profiles
 */
static int test_profile_manager_create_delete_profile(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Create a profile
    kmcp_error_t result = kmcp_profile_create(manager, "test-profile");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if the profile exists
    bool exists = kmcp_profile_exists(manager, "test-profile");
    TEST_ASSERT(exists);

    // Delete the profile
    result = kmcp_profile_delete(manager, "test-profile");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if the profile no longer exists
    exists = kmcp_profile_exists(manager, "test-profile");
    TEST_ASSERT(!exists);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test renaming profiles
 */
static int test_profile_manager_rename_profile(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Create a profile
    kmcp_error_t result = kmcp_profile_create(manager, "old-name");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if the profile exists
    bool exists = kmcp_profile_exists(manager, "old-name");
    TEST_ASSERT(exists);

    // Rename the profile
    result = kmcp_profile_rename(manager, "old-name", "new-name");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if the old name no longer exists
    exists = kmcp_profile_exists(manager, "old-name");
    TEST_ASSERT(!exists);

    // Check if the new name exists
    exists = kmcp_profile_exists(manager, "new-name");
    TEST_ASSERT(exists);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test activating and deactivating profiles
 */
static int test_profile_manager_activate_deactivate_profile(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Create profiles
    kmcp_error_t result = kmcp_profile_create(manager, "profile1");
    TEST_ASSERT(result == KMCP_SUCCESS);

    result = kmcp_profile_create(manager, "profile2");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that no profile is active initially
    const char* active_profile = kmcp_profile_get_active(manager);
    TEST_ASSERT(active_profile == NULL);

    // Activate profile1
    result = kmcp_profile_activate(manager, "profile1");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that profile1 is active
    active_profile = kmcp_profile_get_active(manager);
    TEST_ASSERT(active_profile != NULL);
    TEST_ASSERT(strcmp(active_profile, "profile1") == 0);

    // Activate profile2
    result = kmcp_profile_activate(manager, "profile2");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that profile2 is active
    active_profile = kmcp_profile_get_active(manager);
    TEST_ASSERT(active_profile != NULL);
    TEST_ASSERT(strcmp(active_profile, "profile2") == 0);

    // Deactivate profile2
    result = kmcp_profile_deactivate(manager, "profile2");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that no profile is active
    active_profile = kmcp_profile_get_active(manager);
    TEST_ASSERT(active_profile == NULL);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test getting profile count and names
 */
static int test_profile_manager_get_count_names(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Check that there are no profiles initially
    size_t count = kmcp_profile_get_count(manager);
    TEST_ASSERT(count == 0);

    // Create profiles
    kmcp_error_t result = kmcp_profile_create(manager, "profile1");
    TEST_ASSERT(result == KMCP_SUCCESS);

    result = kmcp_profile_create(manager, "profile2");
    TEST_ASSERT(result == KMCP_SUCCESS);

    result = kmcp_profile_create(manager, "profile3");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that there are 3 profiles
    count = kmcp_profile_get_count(manager);
    TEST_ASSERT(count == 3);

    // Get profile names
    char** names = NULL;
    size_t names_count = 0;
    result = kmcp_profile_get_names(manager, &names, &names_count);
    TEST_ASSERT(result == KMCP_SUCCESS);
    TEST_ASSERT(names_count == 3);

    // Check that the names are correct
    bool found_profile1 = false;
    bool found_profile2 = false;
    bool found_profile3 = false;

    for (size_t i = 0; i < names_count; i++) {
        if (strcmp(names[i], "profile1") == 0) {
            found_profile1 = true;
        } else if (strcmp(names[i], "profile2") == 0) {
            found_profile2 = true;
        } else if (strcmp(names[i], "profile3") == 0) {
            found_profile3 = true;
        }
        free(names[i]);
    }
    free(names);

    TEST_ASSERT(found_profile1);
    TEST_ASSERT(found_profile2);
    TEST_ASSERT(found_profile3);

    // Delete a profile
    result = kmcp_profile_delete(manager, "profile2");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that there are 2 profiles
    count = kmcp_profile_get_count(manager);
    TEST_ASSERT(count == 2);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test adding and removing servers from profiles
 */
static int test_profile_manager_add_remove_server(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Create a profile
    kmcp_error_t result = kmcp_profile_create(manager, "test-profile");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Add a server to the profile
    kmcp_server_config_t server_config;
    memset(&server_config, 0, sizeof(server_config));
    server_config.name = mcp_strdup("test-server");
    server_config.command = mcp_strdup("echo");
    server_config.args_count = 1;
    server_config.args = (char**)malloc(server_config.args_count * sizeof(char*));
    server_config.args[0] = mcp_strdup("hello");

    result = kmcp_profile_add_server(manager, "test-profile", &server_config);

    // Free server configuration
    free(server_config.name);
    free(server_config.command);
    for (size_t i = 0; i < server_config.args_count; i++) {
        free(server_config.args[i]);
    }
    free(server_config.args);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Get the server manager for the profile
    kmcp_server_manager_t* server_manager = kmcp_profile_get_server_manager(manager, "test-profile");
    TEST_ASSERT(server_manager != NULL);

    // Check if the server exists in the profile
    bool exists = kmcp_server_manager_has_server(server_manager, "test-server");
    TEST_ASSERT(exists);

    // Remove the server from the profile
    result = kmcp_profile_remove_server(manager, "test-profile", "test-server");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if the server no longer exists in the profile
    exists = kmcp_server_manager_has_server(server_manager, "test-server");
    TEST_ASSERT(!exists);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test copying and moving servers between profiles
 */
static int test_profile_manager_copy_move_server(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Create profiles
    kmcp_error_t result = kmcp_profile_create(manager, "source-profile");
    TEST_ASSERT(result == KMCP_SUCCESS);

    result = kmcp_profile_create(manager, "target-profile");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Add a server to the source profile
    kmcp_server_config_t server_config;
    memset(&server_config, 0, sizeof(server_config));
    server_config.name = mcp_strdup("source-server");
    server_config.command = mcp_strdup("echo");
    server_config.args_count = 1;
    server_config.args = (char**)malloc(server_config.args_count * sizeof(char*));
    server_config.args[0] = mcp_strdup("hello");

    result = kmcp_profile_add_server(manager, "source-profile", &server_config);

    // Free server configuration
    free(server_config.name);
    free(server_config.command);
    for (size_t i = 0; i < server_config.args_count; i++) {
        free(server_config.args[i]);
    }
    free(server_config.args);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Get the server managers for the profiles
    kmcp_server_manager_t* source_manager = kmcp_profile_get_server_manager(manager, "source-profile");
    TEST_ASSERT(source_manager != NULL);

    kmcp_server_manager_t* target_manager = kmcp_profile_get_server_manager(manager, "target-profile");
    TEST_ASSERT(target_manager != NULL);

    // Check if the server exists in the source profile
    bool exists = kmcp_server_manager_has_server(source_manager, "source-server");
    TEST_ASSERT(exists);

    // Copy the server from the source profile to the target profile
    result = kmcp_profile_copy_server(
        manager,
        "source-profile",  // Source profile
        "source-server",   // Source server
        "target-profile",  // Target profile
        "copied-server"    // Target server
    );
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if the server exists in both profiles
    exists = kmcp_server_manager_has_server(source_manager, "source-server");
    TEST_ASSERT(exists);

    exists = kmcp_server_manager_has_server(target_manager, "copied-server");
    TEST_ASSERT(exists);

    // Move the server from the source profile to the target profile
    result = kmcp_profile_move_server(
        manager,
        "source-profile",  // Source profile
        "source-server",   // Source server
        "target-profile",  // Target profile
        "moved-server"     // Target server
    );
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if the server no longer exists in the source profile
    exists = kmcp_server_manager_has_server(source_manager, "source-server");
    TEST_ASSERT(!exists);

    // Check if the server exists in the target profile
    exists = kmcp_server_manager_has_server(target_manager, "moved-server");
    TEST_ASSERT(exists);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test saving and loading profiles
 */
static int test_profile_manager_save_load(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Create profiles
    kmcp_error_t result = kmcp_profile_create(manager, "profile1");
    TEST_ASSERT(result == KMCP_SUCCESS);

    result = kmcp_profile_create(manager, "profile2");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Add a server to profile1
    kmcp_server_config_t server_config1;
    memset(&server_config1, 0, sizeof(server_config1));
    server_config1.name = mcp_strdup("server1");
    server_config1.command = mcp_strdup("echo");
    server_config1.args_count = 1;
    server_config1.args = (char**)malloc(server_config1.args_count * sizeof(char*));
    server_config1.args[0] = mcp_strdup("hello");

    result = kmcp_profile_add_server(manager, "profile1", &server_config1);

    // Free server configuration
    free(server_config1.name);
    free(server_config1.command);
    for (size_t i = 0; i < server_config1.args_count; i++) {
        free(server_config1.args[i]);
    }
    free(server_config1.args);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Add a server to profile2
    kmcp_server_config_t server_config2;
    memset(&server_config2, 0, sizeof(server_config2));
    server_config2.name = mcp_strdup("server2");
    server_config2.url = mcp_strdup("https://example.com:8080");

    result = kmcp_profile_add_server(manager, "profile2", &server_config2);

    // Free server configuration
    free(server_config2.name);
    free(server_config2.url);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Activate profile1
    result = kmcp_profile_activate(manager, "profile1");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Save profiles to a file
    result = kmcp_profile_save(manager, "profiles.json");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Create a new profile manager
    kmcp_profile_manager_t* new_manager = kmcp_profile_manager_create();
    TEST_ASSERT(new_manager != NULL);

    // Load profiles from the file
    result = kmcp_profile_load(new_manager, "profiles.json");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that the profiles were loaded correctly
    bool exists = kmcp_profile_exists(new_manager, "profile1");
    TEST_ASSERT(exists);

    exists = kmcp_profile_exists(new_manager, "profile2");
    TEST_ASSERT(exists);

    // Check that the active profile was loaded correctly
    const char* active_profile = kmcp_profile_get_active(new_manager);
    TEST_ASSERT(active_profile != NULL);
    TEST_ASSERT(strcmp(active_profile, "profile1") == 0);

    // Get the server managers for the profiles
    kmcp_server_manager_t* server_manager1 = kmcp_profile_get_server_manager(new_manager, "profile1");
    TEST_ASSERT(server_manager1 != NULL);

    kmcp_server_manager_t* server_manager2 = kmcp_profile_get_server_manager(new_manager, "profile2");
    TEST_ASSERT(server_manager2 != NULL);

    // Check if the servers exist in the profiles
    exists = kmcp_server_manager_has_server(server_manager1, "server1");
    TEST_ASSERT(exists);

    exists = kmcp_server_manager_has_server(server_manager2, "server2");
    TEST_ASSERT(exists);

    // Close the profile manager
    kmcp_profile_manager_close(new_manager);

    // Remove the test file
    remove("profiles.json");

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test exporting and importing profiles
 */
static int test_profile_manager_export_import(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Create a profile
    kmcp_error_t result = kmcp_profile_create(manager, "source-profile");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Add a server to the profile
    kmcp_server_config_t server_config;
    memset(&server_config, 0, sizeof(server_config));
    server_config.name = mcp_strdup("source-server");
    server_config.command = mcp_strdup("echo");
    server_config.args_count = 1;
    server_config.args = (char**)malloc(server_config.args_count * sizeof(char*));
    server_config.args[0] = mcp_strdup("hello");

    result = kmcp_profile_add_server(manager, "source-profile", &server_config);

    // Free server configuration
    free(server_config.name);
    free(server_config.command);
    for (size_t i = 0; i < server_config.args_count; i++) {
        free(server_config.args[i]);
    }
    free(server_config.args);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Export the profile to a file
    result = kmcp_profile_export(manager, "source-profile", "profile.json");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Import the profile with a new name
    result = kmcp_profile_import(manager, "profile.json", "imported-profile");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that the imported profile exists
    bool exists = kmcp_profile_exists(manager, "imported-profile");
    TEST_ASSERT(exists);

    // Get the server managers for the profiles
    kmcp_server_manager_t* source_manager = kmcp_profile_get_server_manager(manager, "source-profile");
    TEST_ASSERT(source_manager != NULL);

    kmcp_server_manager_t* imported_manager = kmcp_profile_get_server_manager(manager, "imported-profile");
    TEST_ASSERT(imported_manager != NULL);

    // Check if the server exists in both profiles
    exists = kmcp_server_manager_has_server(source_manager, "source-server");
    TEST_ASSERT(exists);

    exists = kmcp_server_manager_has_server(imported_manager, "source-server");
    TEST_ASSERT(exists);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Remove the test file
    remove("profile.json");

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Run all tests
 */
int run_tests(void) {
    int success = 1;

    success &= test_profile_manager_create_close();
    success &= test_profile_manager_create_delete_profile();
    success &= test_profile_manager_rename_profile();
    success &= test_profile_manager_activate_deactivate_profile();
    success &= test_profile_manager_get_count_names();
    success &= test_profile_manager_add_remove_server();
    success &= test_profile_manager_copy_move_server();
    success &= test_profile_manager_save_load();
    success &= test_profile_manager_export_import();

    return success ? 0 : 1;
}
