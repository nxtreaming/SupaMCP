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
 * Test creating a client with a profile manager
 */
static int test_client_profile_manager_create(void) {
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

    // Activate the profile
    result = kmcp_profile_activate(manager, "test-profile");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Create a client configuration
    kmcp_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.name = mcp_strdup("test-client");
    config.version = mcp_strdup("1.0.0");
    config.use_manager = true;
    config.timeout_ms = 30000;

    // Create a client
    kmcp_client_t* client = kmcp_client_create(&config);
    TEST_ASSERT(client != NULL);

    // Free client configuration
    free(config.name);
    free(config.version);

    // Get the server manager from the client
    kmcp_server_manager_t* server_manager = kmcp_client_get_manager(client);
    TEST_ASSERT(server_manager != NULL);

    // Get the server manager from the profile
    kmcp_server_manager_t* profile_server_manager = kmcp_profile_get_server_manager(manager, "test-profile");
    TEST_ASSERT(profile_server_manager != NULL);

    // Destroy the client
    kmcp_client_destroy(client);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test using a profile manager with a client
 */
static int test_client_profile_manager_use(void) {
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

    // Activate the profile
    result = kmcp_profile_activate(manager, "test-profile");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Create a client configuration
    kmcp_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.name = mcp_strdup("test-client");
    config.version = mcp_strdup("1.0.0");
    config.use_manager = true;
    config.timeout_ms = 30000;

    // Create a client
    kmcp_client_t* client = kmcp_client_create(&config);
    TEST_ASSERT(client != NULL);

    // Free client configuration
    free(config.name);
    free(config.version);

    // Get the server manager from the client
    kmcp_server_manager_t* server_manager = kmcp_client_get_manager(client);
    TEST_ASSERT(server_manager != NULL);

    // Get the server manager from the profile
    kmcp_server_manager_t* profile_server_manager = kmcp_profile_get_server_manager(manager, "test-profile");
    TEST_ASSERT(profile_server_manager != NULL);

    // Use the profile server manager with the client
    kmcp_client_set_manager(client, profile_server_manager);

    // Call a tool (this will fail since we're using a dummy server, but we're just testing the integration)
    const char* tool_name = "echo";
    const char* params_json = "{\"text\":\"Hello, World!\"}";
    char* result_json = NULL;

    result = kmcp_client_call_tool(client, tool_name, params_json, &result_json);
    // We expect this to fail since we're using a dummy server
    TEST_ASSERT(result != KMCP_SUCCESS);

    // Destroy the client
    kmcp_client_destroy(client);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Test switching profiles with a client
 */
static int test_client_profile_manager_switch(void) {
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
    prod_server.command = mcp_strdup("echo");
    prod_server.args_count = 1;
    prod_server.args = (char**)malloc(prod_server.args_count * sizeof(char*));
    prod_server.args[0] = mcp_strdup("prod");

    result = kmcp_profile_add_server(manager, "production", &prod_server);

    // Free server configuration
    free(prod_server.name);
    free(prod_server.command);
    for (size_t i = 0; i < prod_server.args_count; i++) {
        free(prod_server.args[i]);
    }
    free(prod_server.args);

    TEST_ASSERT(result == KMCP_SUCCESS);

    // Create a client configuration
    kmcp_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.name = mcp_strdup("test-client");
    config.version = mcp_strdup("1.0.0");
    config.use_manager = true;
    config.timeout_ms = 30000;

    // Create a client
    kmcp_client_t* client = kmcp_client_create(&config);
    TEST_ASSERT(client != NULL);

    // Free client configuration
    free(config.name);
    free(config.version);

    // Get the server manager from the client
    kmcp_server_manager_t* server_manager = kmcp_client_get_manager(client);
    TEST_ASSERT(server_manager != NULL);

    // Activate the development profile
    result = kmcp_profile_activate(manager, "development");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Get the server manager from the development profile
    kmcp_server_manager_t* dev_server_manager = kmcp_profile_get_server_manager(manager, "development");
    TEST_ASSERT(dev_server_manager != NULL);

    // Use the development server manager with the client
    kmcp_client_set_manager(client, dev_server_manager);

    // Activate the production profile
    result = kmcp_profile_activate(manager, "production");
    TEST_ASSERT(result == KMCP_SUCCESS);

    // Get the server manager from the production profile
    kmcp_server_manager_t* prod_server_manager = kmcp_profile_get_server_manager(manager, "production");
    TEST_ASSERT(prod_server_manager != NULL);

    // Use the production server manager with the client
    kmcp_client_set_manager(client, prod_server_manager);

    // Destroy the client
    kmcp_client_destroy(client);

    // Close the profile manager
    kmcp_profile_manager_close(manager);

    // Close logging
    mcp_log_close();

    return 1;
}

/**
 * Run all tests
 */
int run_tests(void) {
    int success = 1;

    success &= test_client_profile_manager_create();
    success &= test_client_profile_manager_use();
    success &= test_client_profile_manager_switch();

    return success ? 0 : 1;
}
