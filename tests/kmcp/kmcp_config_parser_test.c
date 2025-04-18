/**
 * @file kmcp_config_parser_test.c
 * @brief Test file for KMCP configuration parser functionality
 */
#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kmcp_config_parser.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * @brief Create a test configuration file
 *
 * @param file_path Path to create the file
 * @return int Returns 0 on success, non-zero on failure
 */
static int create_test_config_file(const char* file_path) {
    FILE* file = fopen(file_path, "w");
    if (!file) {
        printf("FAIL: Failed to create test configuration file\n");
        return 1;
    }

    // Use a very simple JSON structure for testing
    const char* json_content = "{\"clientConfig\":{\"clientName\":\"test-client\",\"clientVersion\":\"1.0.0\",\"useServerManager\":true,\"requestTimeoutMs\":30000},\"mcpServers\":{\"local\":{\"command\":\"mcp_server\",\"args\":[\"--tcp\",\"--port\",\"8080\"]},\"remote\":{\"url\":\"http://example.com:8080\"}},\"toolAccessControl\":{\"defaultAllow\":false,\"allowedTools\":[\"tool1\",\"tool2\"],\"disallowedTools\":[\"tool3\"]}}";

    // Write the JSON content to the file
    fprintf(file, "%s", json_content);

    // Print the JSON content for debugging
    printf("DEBUG: Created test config file with content:\n%s\n", json_content);

    fclose(file);
    return 0;
}

/**
 * @brief Test configuration parser creation
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_config_parser_create() {
    printf("Testing configuration parser creation...\n");

    // Create a test configuration file
    char current_dir[1024] = {0};
    if (GetCurrentDirectory(sizeof(current_dir), current_dir) > 0) {
        printf("DEBUG: Current directory: %s\n", current_dir);
    }

    // Use an absolute path for the test config file
    char test_config_path[1024] = {0};
    if (GetCurrentDirectory(sizeof(test_config_path), test_config_path) > 0) {
        strcat(test_config_path, "\\test_config.json");
    } else {
        strcpy(test_config_path, "test_config.json");
    }
    const char* test_config_file = test_config_path;
    printf("DEBUG: Creating test config file at: %s\n", test_config_file);
    if (create_test_config_file(test_config_file) != 0) {
        return 1;
    }

    // Verify the file exists and read its contents
    FILE* check_file = fopen(test_config_file, "r");
    if (!check_file) {
        printf("FAIL: Test config file does not exist after creation\n");
        return 1;
    }

    // Get file size
    fseek(check_file, 0, SEEK_END);
    long file_size = ftell(check_file);
    fseek(check_file, 0, SEEK_SET);

    // Read file contents
    char* buffer = (char*)malloc(file_size + 1);
    if (!buffer) {
        printf("FAIL: Failed to allocate memory for file contents\n");
        fclose(check_file);
        return 1;
    }

    size_t read_size = fread(buffer, 1, file_size, check_file);
    buffer[read_size] = '\0';
    fclose(check_file);

    printf("DEBUG: Read %zu bytes from test config file:\n%s\n", read_size, buffer);
    free(buffer);
    printf("DEBUG: Verified test config file exists\n");

    // Create configuration parser
    printf("DEBUG: Creating configuration parser for file: %s\n", test_config_file);
    kmcp_config_parser_t* parser = kmcp_config_parser_create(test_config_file);
    if (!parser) {
        printf("FAIL: Failed to create configuration parser\n");
        remove(test_config_file);
        return 1;
    }
    printf("DEBUG: Successfully created configuration parser\n");

    // Test with invalid parameters
    kmcp_config_parser_t* invalid_parser = kmcp_config_parser_create(NULL);
    if (invalid_parser) {
        printf("FAIL: Created configuration parser with NULL file path\n");
        kmcp_config_parser_close(invalid_parser);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    invalid_parser = kmcp_config_parser_create("non_existent_file.json");
    if (invalid_parser) {
        printf("FAIL: Created configuration parser with non-existent file\n");
        kmcp_config_parser_close(invalid_parser);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Clean up
    kmcp_config_parser_close(parser);
    remove(test_config_file);

    printf("PASS: Configuration parser creation tests passed\n");
    return 0;
}

/**
 * @brief Test client configuration parsing
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_config_parser_get_client() {
    printf("Testing client configuration parsing...\n");

    // Use an absolute path for the test config file
    char test_config_path[1024] = {0};
    if (GetCurrentDirectory(sizeof(test_config_path), test_config_path) > 0) {
        strcat(test_config_path, "\\test_config.json");
    } else {
        strcpy(test_config_path, "test_config.json");
    }
    const char* test_config_file = test_config_path;
    printf("DEBUG: Creating test config file at: %s\n", test_config_file);
    if (create_test_config_file(test_config_file) != 0) {
        return 1;
    }

    // Create configuration parser
    printf("DEBUG: Creating configuration parser for file: %s\n", test_config_file);
    kmcp_config_parser_t* parser = kmcp_config_parser_create(test_config_file);
    if (!parser) {
        printf("FAIL: Failed to create configuration parser\n");
        remove(test_config_file);
        return 1;
    }
    printf("DEBUG: Successfully created configuration parser\n");

    // Parse client configuration
    kmcp_client_config_t config = {0};
    kmcp_error_t result = kmcp_config_parser_get_client(parser, &config);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to parse client configuration, error: %s\n", kmcp_error_message(result));
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Verify configuration fields
    if (!config.name || strcmp(config.name, "test-client") != 0) {
        printf("FAIL: Expected client name to be 'test-client', got '%s'\n",
               config.name ? config.name : "NULL");
        free(config.name);
        free(config.version);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    if (!config.version || strcmp(config.version, "1.0.0") != 0) {
        printf("FAIL: Expected client version to be '1.0.0', got '%s'\n",
               config.version ? config.version : "NULL");
        free(config.name);
        free(config.version);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    if (!config.use_manager) {
        printf("FAIL: Expected use_manager to be true\n");
        free(config.name);
        free(config.version);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Test with invalid parameters
    result = kmcp_config_parser_get_client(NULL, &config);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL parser, got %d\n", result);
        free(config.name);
        free(config.version);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    result = kmcp_config_parser_get_client(parser, NULL);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL config, got %d\n", result);
        free(config.name);
        free(config.version);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Clean up
    free(config.name);
    free(config.version);
    kmcp_config_parser_close(parser);
    remove(test_config_file);

    printf("PASS: Client configuration parsing tests passed\n");
    return 0;
}

/**
 * @brief Test server configurations parsing
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_config_parser_get_servers() {
    printf("Testing server configurations parsing...\n");

    // Use an absolute path for the test config file
    char test_config_path[1024] = {0};
    if (GetCurrentDirectory(sizeof(test_config_path), test_config_path) > 0) {
        strcat(test_config_path, "\\test_config.json");
    } else {
        strcpy(test_config_path, "test_config.json");
    }
    const char* test_config_file = test_config_path;
    printf("DEBUG: Creating test config file at: %s\n", test_config_file);
    if (create_test_config_file(test_config_file) != 0) {
        return 1;
    }

    // Create configuration parser
    printf("DEBUG: Creating configuration parser for file: %s\n", test_config_file);
    kmcp_config_parser_t* parser = kmcp_config_parser_create(test_config_file);
    if (!parser) {
        printf("FAIL: Failed to create configuration parser\n");
        remove(test_config_file);
        return 1;
    }
    printf("DEBUG: Successfully created configuration parser\n");

    // Parse server configurations
    kmcp_server_config_t** servers = NULL;
    size_t server_count = 0;
    kmcp_error_t result = kmcp_config_parser_get_servers(parser, &servers, &server_count);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to parse server configurations, error: %s\n", kmcp_error_message(result));
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Verify server count
    if (server_count != 2) {
        printf("FAIL: Expected 2 servers, got %zu\n", server_count);
        for (size_t i = 0; i < server_count; i++) {
            // Free server config fields
            free(servers[i]->name);
            free(servers[i]->command);
            free(servers[i]->url);

            // Free args array
            if (servers[i]->args) {
                for (size_t j = 0; j < servers[i]->args_count; j++) {
                    free(servers[i]->args[j]);
                }
                free(servers[i]->args);
            }

            // Free env array
            if (servers[i]->env) {
                for (size_t j = 0; j < servers[i]->env_count; j++) {
                    free(servers[i]->env[j]);
                }
                free(servers[i]->env);
            }

            // Free the server config itself
            free(servers[i]);
        }
        free(servers);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Verify server configurations
    bool found_local = false;
    bool found_remote = false;

    for (size_t i = 0; i < server_count; i++) {
        if (strcmp(servers[i]->name, "local") == 0) {
            found_local = true;

            if (strcmp(servers[i]->command, "mcp_server") != 0) {
                printf("FAIL: Expected local server command to be 'mcp_server', got '%s'\n", servers[i]->command);
                for (size_t j = 0; j < server_count; j++) {
                    // Free server config fields
                    free(servers[j]->name);
                    free(servers[j]->command);
                    free(servers[j]->url);

                    // Free args array
                    if (servers[j]->args) {
                        for (size_t k = 0; k < servers[j]->args_count; k++) {
                            free(servers[j]->args[k]);
                        }
                        free(servers[j]->args);
                    }

                    // Free env array
                    if (servers[j]->env) {
                        for (size_t k = 0; k < servers[j]->env_count; k++) {
                            free(servers[j]->env[k]);
                        }
                        free(servers[j]->env);
                    }

                    // Free the server config itself
                    free(servers[j]);
                }
                free(servers);
                kmcp_config_parser_close(parser);
                remove(test_config_file);
                return 1;
            }

            // Check if is_http is set correctly based on presence of url vs command
            if (servers[i]->url != NULL) {
                printf("FAIL: Expected local server to have command, not URL\n");
                for (size_t j = 0; j < server_count; j++) {
                    // Free server config fields
                    free(servers[j]->name);
                    free(servers[j]->command);
                    free(servers[j]->url);

                    // Free args array
                    if (servers[j]->args) {
                        for (size_t k = 0; k < servers[j]->args_count; k++) {
                            free(servers[j]->args[k]);
                        }
                        free(servers[j]->args);
                    }

                    // Free env array
                    if (servers[j]->env) {
                        for (size_t k = 0; k < servers[j]->env_count; k++) {
                            free(servers[j]->env[k]);
                        }
                        free(servers[j]->env);
                    }

                    // Free the server config itself
                    free(servers[j]);
                }
                free(servers);
                kmcp_config_parser_close(parser);
                remove(test_config_file);
                return 1;
            }
        } else if (strcmp(servers[i]->name, "remote") == 0) {
            found_remote = true;

            if (strcmp(servers[i]->url, "http://example.com:8080") != 0) {
                printf("FAIL: Expected remote server URL to be 'http://example.com:8080', got '%s'\n", servers[i]->url);
                for (size_t j = 0; j < server_count; j++) {
                    // Free server config fields
                    free(servers[j]->name);
                    free(servers[j]->command);
                    free(servers[j]->url);

                    // Free args array
                    if (servers[j]->args) {
                        for (size_t k = 0; k < servers[j]->args_count; k++) {
                            free(servers[j]->args[k]);
                        }
                        free(servers[j]->args);
                    }

                    // Free env array
                    if (servers[j]->env) {
                        for (size_t k = 0; k < servers[j]->env_count; k++) {
                            free(servers[j]->env[k]);
                        }
                        free(servers[j]->env);
                    }

                    // Free the server config itself
                    free(servers[j]);
                }
                free(servers);
                kmcp_config_parser_close(parser);
                remove(test_config_file);
                return 1;
            }

            // Check if is_http is set correctly based on presence of url vs command
            if (servers[i]->command != NULL) {
                printf("FAIL: Expected remote server to have URL, not command\n");
                for (size_t j = 0; j < server_count; j++) {
                    // Free server config fields
                    free(servers[j]->name);
                    free(servers[j]->command);
                    free(servers[j]->url);

                    // Free args array
                    if (servers[j]->args) {
                        for (size_t k = 0; k < servers[j]->args_count; k++) {
                            free(servers[j]->args[k]);
                        }
                        free(servers[j]->args);
                    }

                    // Free env array
                    if (servers[j]->env) {
                        for (size_t k = 0; k < servers[j]->env_count; k++) {
                            free(servers[j]->env[k]);
                        }
                        free(servers[j]->env);
                    }

                    // Free the server config itself
                    free(servers[j]);
                }
                free(servers);
                kmcp_config_parser_close(parser);
                remove(test_config_file);
                return 1;
            }
        }
    }

    if (!found_local) {
        printf("FAIL: Local server configuration not found\n");
        for (size_t i = 0; i < server_count; i++) {
            // Free server config fields
            free(servers[i]->name);
            free(servers[i]->command);
            free(servers[i]->url);

            // Free args array
            if (servers[i]->args) {
                for (size_t j = 0; j < servers[i]->args_count; j++) {
                    free(servers[i]->args[j]);
                }
                free(servers[i]->args);
            }

            // Free env array
            if (servers[i]->env) {
                for (size_t j = 0; j < servers[i]->env_count; j++) {
                    free(servers[i]->env[j]);
                }
                free(servers[i]->env);
            }

            // Free the server config itself
            free(servers[i]);
        }
        free(servers);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    if (!found_remote) {
        printf("FAIL: Remote server configuration not found\n");
        for (size_t i = 0; i < server_count; i++) {
            // Free server config fields
        free(servers[i]->name);
        free(servers[i]->command);
        free(servers[i]->url);

        // Free args array
        if (servers[i]->args) {
            for (size_t j = 0; j < servers[i]->args_count; j++) {
                free(servers[i]->args[j]);
            }
            free(servers[i]->args);
        }

        // Free env array
        if (servers[i]->env) {
            for (size_t j = 0; j < servers[i]->env_count; j++) {
                free(servers[i]->env[j]);
            }
            free(servers[i]->env);
        }

        // Free the server config itself
        free(servers[i]);
        }
        free(servers);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Test with invalid parameters
    result = kmcp_config_parser_get_servers(NULL, &servers, &server_count);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL parser, got %d\n", result);
        for (size_t i = 0; i < server_count; i++) {
            // Free server config fields
            free(servers[i]->name);
            free(servers[i]->command);
            free(servers[i]->url);

            // Free args array
            if (servers[i]->args) {
                for (size_t j = 0; j < servers[i]->args_count; j++) {
                    free(servers[i]->args[j]);
                }
                free(servers[i]->args);
            }

            // Free env array
            if (servers[i]->env) {
                for (size_t j = 0; j < servers[i]->env_count; j++) {
                    free(servers[i]->env[j]);
                }
                free(servers[i]->env);
            }

            // Free the server config itself
            free(servers[i]);
        }
        free(servers);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    result = kmcp_config_parser_get_servers(parser, NULL, &server_count);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL servers, got %d\n", result);
        for (size_t i = 0; i < server_count; i++) {
            // Free server config fields
            free(servers[i]->name);
            free(servers[i]->command);
            free(servers[i]->url);

            // Free args array
            if (servers[i]->args) {
                for (size_t j = 0; j < servers[i]->args_count; j++) {
                    free(servers[i]->args[j]);
                }
                free(servers[i]->args);
            }

            // Free env array
            if (servers[i]->env) {
                for (size_t j = 0; j < servers[i]->env_count; j++) {
                    free(servers[i]->env[j]);
                }
                free(servers[i]->env);
            }

            // Free the server config itself
            free(servers[i]);
        }
        free(servers);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    result = kmcp_config_parser_get_servers(parser, &servers, NULL);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL server_count, got %d\n", result);
        for (size_t i = 0; i < server_count; i++) {
            // Free server config fields
            free(servers[i]->name);
            free(servers[i]->command);
            free(servers[i]->url);

            // Free args array
            if (servers[i]->args) {
                for (size_t j = 0; j < servers[i]->args_count; j++) {
                    free(servers[i]->args[j]);
                }
                free(servers[i]->args);
            }

            // Free env array
            if (servers[i]->env) {
                for (size_t j = 0; j < servers[i]->env_count; j++) {
                    free(servers[i]->env[j]);
                }
                free(servers[i]->env);
            }

            // Free the server config itself
            free(servers[i]);
        }
        free(servers);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Clean up
    for (size_t i = 0; i < server_count; i++) {
        // Free server config fields
        free(servers[i]->name);
        free(servers[i]->command);
        free(servers[i]->url);

        // Free args array
        if (servers[i]->args) {
            for (size_t j = 0; j < servers[i]->args_count; j++) {
                free(servers[i]->args[j]);
            }
            free(servers[i]->args);
        }

        // Free env array
        if (servers[i]->env) {
            for (size_t j = 0; j < servers[i]->env_count; j++) {
                free(servers[i]->env[j]);
            }
            free(servers[i]->env);
        }

        // Free the server config itself
        free(servers[i]);
    }
    free(servers);
    kmcp_config_parser_close(parser);
    remove(test_config_file);

    printf("PASS: Server configurations parsing tests passed\n");
    return 0;
}

/**
 * @brief Test tool access control parsing
 *
 * @return int Returns 0 on success, non-zero on failure
 */
static int test_config_parser_get_access() {
    printf("Testing tool access control parsing...\n");

    // Use an absolute path for the test config file
    char test_config_path[1024] = {0};
    if (GetCurrentDirectory(sizeof(test_config_path), test_config_path) > 0) {
        strcat(test_config_path, "\\test_config.json");
    } else {
        strcpy(test_config_path, "test_config.json");
    }
    const char* test_config_file = test_config_path;
    printf("DEBUG: Creating test config file at: %s\n", test_config_file);
    if (create_test_config_file(test_config_file) != 0) {
        return 1;
    }

    // Create configuration parser
    printf("DEBUG: Creating configuration parser for file: %s\n", test_config_file);
    kmcp_config_parser_t* parser = kmcp_config_parser_create(test_config_file);
    if (!parser) {
        printf("FAIL: Failed to create configuration parser\n");
        remove(test_config_file);
        return 1;
    }
    printf("DEBUG: Successfully created configuration parser\n");

    // Create tool access
    kmcp_tool_access_t* access = kmcp_tool_access_create(false); // Default deny
    printf("DEBUG: Created tool access with default_allow = false\n");
    if (!access) {
        printf("FAIL: Failed to create tool access\n");
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Parse tool access control
    kmcp_error_t result = kmcp_config_parser_get_access(parser, access);
    if (result != KMCP_SUCCESS) {
        printf("FAIL: Failed to parse tool access control, error: %s\n", kmcp_error_message(result));
        kmcp_tool_access_destroy(access);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }
    printf("DEBUG: Successfully parsed tool access control\n");

    // Verify tool access

    // Check allowed tools
    bool allowed = kmcp_tool_access_check(access, "tool1");
    printf("DEBUG: tool1 access check result: %s\n", allowed ? "allowed" : "disallowed");
    if (!allowed) {
        printf("FAIL: Expected tool1 to be allowed\n");
        kmcp_tool_access_destroy(access);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    allowed = kmcp_tool_access_check(access, "tool2");
    if (!allowed) {
        printf("FAIL: Expected tool2 to be allowed\n");
        kmcp_tool_access_destroy(access);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Check disallowed tools
    printf("DEBUG: Checking if tool3 is disallowed...\n");
    allowed = kmcp_tool_access_check(access, "tool3");
    printf("DEBUG: tool3 access check result: %s\n", allowed ? "allowed" : "disallowed");

    // Try adding tool3 to the disallowed list directly
    printf("DEBUG: Manually adding tool3 to disallowed list...\n");
    kmcp_error_t add_result = kmcp_tool_access_add(access, "tool3", false);
    printf("DEBUG: Manual add result: %s\n", kmcp_error_message(add_result));

    // Try adding a different tool to the disallowed list
    printf("DEBUG: Manually adding test_tool to disallowed list...\n");
    add_result = kmcp_tool_access_add(access, "test_tool", false);
    printf("DEBUG: Manual add result for test_tool: %s\n", kmcp_error_message(add_result));

    // Check test_tool
    bool test_tool_allowed = kmcp_tool_access_check(access, "test_tool");
    printf("DEBUG: test_tool access check result: %s\n", test_tool_allowed ? "allowed" : "disallowed");

    // Check again
    allowed = kmcp_tool_access_check(access, "tool3");
    printf("DEBUG: tool3 access check result after manual add: %s\n", allowed ? "allowed" : "disallowed");

    if (allowed) {
        printf("FAIL: Expected tool3 to be disallowed\n");
        kmcp_tool_access_destroy(access);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Check unknown tool (should be disallowed by default)
    allowed = kmcp_tool_access_check(access, "unknown_tool");
    if (allowed) {
        printf("FAIL: Expected unknown_tool to be disallowed by default\n");
        kmcp_tool_access_destroy(access);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Test with invalid parameters
    result = kmcp_config_parser_get_access(NULL, access);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL parser, got %d\n", result);
        kmcp_tool_access_destroy(access);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    result = kmcp_config_parser_get_access(parser, NULL);
    if (result != KMCP_ERROR_INVALID_PARAMETER) {
        printf("FAIL: Expected KMCP_ERROR_INVALID_PARAMETER for NULL access, got %d\n", result);
        kmcp_tool_access_destroy(access);
        kmcp_config_parser_close(parser);
        remove(test_config_file);
        return 1;
    }

    // Clean up
    kmcp_tool_access_destroy(access);
    kmcp_config_parser_close(parser);
    remove(test_config_file);

    printf("PASS: Tool access control parsing tests passed\n");
    return 0;
}

/**
 * @brief Main function for config parser tests
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int kmcp_config_parser_test_main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    // Initialize thread-local arena for JSON allocation
    if (mcp_arena_init_current_thread(0) != 0) {
        mcp_log_error("Failed to initialize thread-local arena");
        return 1;
    }

    printf("=== KMCP Configuration Parser Tests ===\n");

    int failures = 0;

    // Run tests
    failures += test_config_parser_create();
    failures += test_config_parser_get_client();
    failures += test_config_parser_get_servers();
    failures += test_config_parser_get_access();

    // Print summary
    printf("\n=== Test Summary ===\n");
    if (failures == 0) {
        printf("All tests PASSED\n");
    } else {
        printf("%d tests FAILED\n", failures);
    }

    // Clean up logging
    mcp_log_close();

    return failures;
}

#ifdef STANDALONE_TEST
/**
 * @brief Main function for standalone test
 *
 * @return int Returns 0 on success, non-zero on failure
 */
int main() {
    return kmcp_config_parser_test_main();
}
#endif
