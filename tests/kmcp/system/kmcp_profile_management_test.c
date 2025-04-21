#include "kmcp.h"
#include "kmcp_profile_manager.h"
#include "mcp_log.h"
#include "kmcp_server_manager_stub.h"
#include "mcp_string_utils.h"
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
 * Test creating and managing profiles
 */
static int test_profile_management_create(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Create development profile
    kmcp_error_t result = kmcp_profile_create(manager, "development");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Create production profile
    result = kmcp_profile_create(manager, "production");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Create testing profile
    result = kmcp_profile_create(manager, "testing");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if profiles exist
    bool exists = kmcp_profile_exists(manager, "development");
    TEST_ASSERT(exists);

    exists = kmcp_profile_exists(manager, "production");
    TEST_ASSERT(exists);

    exists = kmcp_profile_exists(manager, "testing");
    TEST_ASSERT(exists);

    exists = kmcp_profile_exists(manager, "nonexistent");
    TEST_ASSERT(!exists);

    // Get profile count
    size_t count = kmcp_profile_get_count(manager);
    TEST_ASSERT(count == 3);

    // Get profile names
    char** names = NULL;
    size_t names_count = 0;
    result = kmcp_profile_get_names(manager, &names, &names_count);
    TEST_ASSERT(result == KMCP_SUCCESS);
    TEST_ASSERT(names_count == 3);

    // Free profile names
    for (size_t i = 0; i < names_count; i++) {
        free(names[i]);
    }
    free(names);

    // Rename a profile
    result = kmcp_profile_rename(manager, "testing", "qa");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if renamed profile exists
    exists = kmcp_profile_exists(manager, "testing");
    TEST_ASSERT(!exists);

    exists = kmcp_profile_exists(manager, "qa");
    TEST_ASSERT(exists);

    // Delete a profile
    result = kmcp_profile_delete(manager, "qa");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if deleted profile exists
    exists = kmcp_profile_exists(manager, "qa");
    TEST_ASSERT(!exists);

    // Get profile count after deletion
    count = kmcp_profile_get_count(manager);
    TEST_ASSERT(count == 2);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test adding and managing servers in profiles
 */
static int test_profile_management_servers(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Create development profile
    kmcp_error_t result = kmcp_profile_create(manager, "development");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Create production profile
    result = kmcp_profile_create(manager, "production");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Add a server to the development profile
    kmcp_server_config_t dev_server;
    memset(&dev_server, 0, sizeof(dev_server));
    dev_server.name = mcp_strdup("dev-server");
    dev_server.command = mcp_strdup("echo");
    dev_server.args_count = 1;
    dev_server.args = (char**)malloc(dev_server.args_count * sizeof(char*));
    dev_server.args[0] = mcp_strdup("dev");

    result = kmcp_profile_add_server(manager, "development", &dev_server);

    // Free server configuration
    free(dev_server.name);
    free(dev_server.command);
    for (size_t i = 0; i < dev_server.args_count; i++) {
        free(dev_server.args[i]);
    }
    free(dev_server.args);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Add a server to the production profile
    kmcp_server_config_t prod_server;
    memset(&prod_server, 0, sizeof(prod_server));
    prod_server.name = mcp_strdup("prod-server");
    prod_server.url = mcp_strdup("https://example.com:8080");

    result = kmcp_profile_add_server(manager, "production", &prod_server);

    // Free server configuration
    free(prod_server.name);
    free(prod_server.url);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Get the server manager for the development profile
    kmcp_server_manager_t* dev_manager = kmcp_profile_get_server_manager(manager, "development");
    TEST_ASSERT(dev_manager != NULL);

    // Get the server manager for the production profile
    kmcp_server_manager_t* prod_manager = kmcp_profile_get_server_manager(manager, "production");
    TEST_ASSERT(prod_manager != NULL);

    // Check if servers exist in the profiles
    bool exists = kmcp_server_manager_has_server(dev_manager, "dev-server");
    TEST_ASSERT(exists);

    exists = kmcp_server_manager_has_server(prod_manager, "prod-server");
    TEST_ASSERT(exists);

    // Copy a server from development to production
    result = kmcp_profile_copy_server(
        manager,
        "development",  // Source profile
        "dev-server",   // Source server
        "production",   // Target profile
        "dev-copy"      // Target server
    );
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if copied server exists in the production profile
    exists = kmcp_server_manager_has_server(prod_manager, "dev-copy");
    TEST_ASSERT(exists);

    // Move a server from production to development
    result = kmcp_profile_move_server(
        manager,
        "production",   // Source profile
        "prod-server",  // Source server
        "development",  // Target profile
        "prod-moved"    // Target server
    );
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if moved server exists in the development profile
    exists = kmcp_server_manager_has_server(dev_manager, "prod-moved");
    TEST_ASSERT(exists);

    // Check if moved server no longer exists in the production profile
    exists = kmcp_server_manager_has_server(prod_manager, "prod-server");
    TEST_ASSERT(!exists);

    // Remove a server from the development profile
    result = kmcp_profile_remove_server(manager, "development", "dev-server");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check if removed server no longer exists in the development profile
    exists = kmcp_server_manager_has_server(dev_manager, "dev-server");
    TEST_ASSERT(!exists);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test activating and using profiles
 */
static int test_profile_management_activate(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Create development profile
    kmcp_error_t result = kmcp_profile_create(manager, "development");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Create production profile
    result = kmcp_profile_create(manager, "production");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Add a server to the development profile
    kmcp_server_config_t dev_server;
    memset(&dev_server, 0, sizeof(dev_server));
    dev_server.name = mcp_strdup("dev-server");
    dev_server.command = mcp_strdup("echo");
    dev_server.args_count = 1;
    dev_server.args = (char**)malloc(dev_server.args_count * sizeof(char*));
    dev_server.args[0] = mcp_strdup("dev");

    result = kmcp_profile_add_server(manager, "development", &dev_server);

    // Free server configuration
    free(dev_server.name);
    free(dev_server.command);
    for (size_t i = 0; i < dev_server.args_count; i++) {
        free(dev_server.args[i]);
    }
    free(dev_server.args);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Add a server to the production profile
    kmcp_server_config_t prod_server;
    memset(&prod_server, 0, sizeof(prod_server));
    prod_server.name = mcp_strdup("prod-server");
    prod_server.url = mcp_strdup("https://example.com:8080");

    result = kmcp_profile_add_server(manager, "production", &prod_server);

    // Free server configuration
    free(prod_server.name);
    free(prod_server.url);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that no profile is active initially
    const char* active_profile = kmcp_profile_get_active(manager);
    TEST_ASSERT(active_profile == NULL);

    // Activate the development profile
    result = kmcp_profile_activate(manager, "development");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that the development profile is active
    active_profile = kmcp_profile_get_active(manager);
    TEST_ASSERT(active_profile != NULL);
    TEST_ASSERT(strcmp(active_profile, "development") == 0);

    // Activate the production profile
    result = kmcp_profile_activate(manager, "production");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that the production profile is active
    active_profile = kmcp_profile_get_active(manager);
    TEST_ASSERT(active_profile != NULL);
    TEST_ASSERT(strcmp(active_profile, "production") == 0);

    // Deactivate the production profile
    result = kmcp_profile_deactivate(manager, "production");
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
 * Test saving and loading profiles
 */
static int test_profile_management_save_load(void) {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Create a profile manager
    kmcp_profile_manager_t* manager = kmcp_profile_manager_create();
    TEST_ASSERT(manager != NULL);

    // Create development profile
    kmcp_error_t result = kmcp_profile_create(manager, "development");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Create production profile
    result = kmcp_profile_create(manager, "production");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Add a server to the development profile
    kmcp_server_config_t dev_server;
    memset(&dev_server, 0, sizeof(dev_server));
    dev_server.name = mcp_strdup("dev-server");
    dev_server.command = mcp_strdup("echo");
    dev_server.args_count = 1;
    dev_server.args = (char**)malloc(dev_server.args_count * sizeof(char*));
    dev_server.args[0] = mcp_strdup("dev");

    result = kmcp_profile_add_server(manager, "development", &dev_server);

    // Free server configuration
    free(dev_server.name);
    free(dev_server.command);
    for (size_t i = 0; i < dev_server.args_count; i++) {
        free(dev_server.args[i]);
    }
    free(dev_server.args);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Add a server to the production profile
    kmcp_server_config_t prod_server;
    memset(&prod_server, 0, sizeof(prod_server));
    prod_server.name = mcp_strdup("prod-server");
    prod_server.url = mcp_strdup("https://example.com:8080");

    result = kmcp_profile_add_server(manager, "production", &prod_server);

    // Free server configuration
    free(prod_server.name);
    free(prod_server.url);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Activate the development profile
    result = kmcp_profile_activate(manager, "development");
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
    bool exists = kmcp_profile_exists(new_manager, "development");
    TEST_ASSERT(exists);

    exists = kmcp_profile_exists(new_manager, "production");
    TEST_ASSERT(exists);

    // Check that the active profile was loaded correctly
    const char* active_profile = kmcp_profile_get_active(new_manager);
    TEST_ASSERT(active_profile != NULL);
    TEST_ASSERT(strcmp(active_profile, "development") == 0);

    // Get the server manager for the development profile
    kmcp_server_manager_t* dev_manager = kmcp_profile_get_server_manager(new_manager, "development");
    TEST_ASSERT(dev_manager != NULL);

    // Get the server manager for the production profile
    kmcp_server_manager_t* prod_manager = kmcp_profile_get_server_manager(new_manager, "production");
    TEST_ASSERT(prod_manager != NULL);

    // Check if servers exist in the profiles
    exists = kmcp_server_manager_has_server(dev_manager, "dev-server");
    TEST_ASSERT(exists);

    exists = kmcp_server_manager_has_server(prod_manager, "prod-server");
    TEST_ASSERT(exists);

    // Export the development profile to a file
    result = kmcp_profile_export(new_manager, "development", "development.json");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Import the development profile as a new profile
    result = kmcp_profile_import(new_manager, "development.json", "development-copy");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Check that the imported profile exists
    exists = kmcp_profile_exists(new_manager, "development-copy");
    TEST_ASSERT(exists);

    // Get the server manager for the imported profile
    kmcp_server_manager_t* imported_manager = kmcp_profile_get_server_manager(new_manager, "development-copy");
    TEST_ASSERT(imported_manager != NULL);

    // Check if servers exist in the imported profile
    exists = kmcp_server_manager_has_server(imported_manager, "dev-server");
    TEST_ASSERT(exists);

    // Close the profile manager
    kmcp_profile_manager_close(new_manager);

    // Remove the test files
    remove("profiles.json");
    remove("development.json");

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Run all tests
 */
int run_tests(void) {
    int success = 1;

    success &= test_profile_management_create();
    success &= test_profile_management_servers();
    success &= test_profile_management_activate();
    success &= test_profile_management_save_load();

    return success ? 0 : 1;
}
