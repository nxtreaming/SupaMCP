#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcp_client.h"
#include "mcp_stdio_transport.h"
#include "mcp_tcp_client_transport.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"

int main(int argc, char** argv) {
    // Default transport
    const char* transport_type = "stdio";
    const char* host = "127.0.0.1";
    uint16_t port = 8080;

    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG); // Use new init and enum

    // Initialize thread-local storage (arena) for the main thread
    // Using 1MB as the initial size. Adjust if needed.
    if (mcp_arena_init_current_thread(1024 * 1024) != 0) {
        mcp_log_error("Failed to initialize thread-local arena for main thread.");
        mcp_log_close(); // Close log before exiting
        return 1;
    }

    // Basic argument parsing
    if (argc > 1) {
        if (strcmp(argv[1], "--tcp") == 0) {
            transport_type = "tcp";
            if (argc > 2) host = argv[2];
            if (argc > 3) port = (uint16_t)atoi(argv[3]);
        } else if (strcmp(argv[1], "--stdio") == 0) {
            transport_type = "stdio";
        } else if (strcmp(argv[1], "--help") == 0) {
             printf("Usage: %s [--stdio | --tcp [HOST [PORT]]]\n\n", argv[0]);
             printf("Options:\n");
             printf("  --stdio             Use stdio transport (default)\n");
             printf("  --tcp [HOST [PORT]] Use TCP transport (default: 127.0.0.1:8080)\n");
             printf("  --help              Show this help message\n\n");
             printf("Interactive Commands:\n");
             printf("  list_resources              - List available resources\n");
             printf("  list_templates              - List available resource templates\n");
             printf("  list_tools                  - List available tools\n");
             printf("  read <uri>                  - Read a resource by URI\n");
             printf("  expand <template> <params>  - Expand a template with parameters\n");
             printf("  read_template <template> <params> - Read a resource using a template\n");
             printf("  call <tool> <params>        - Call a tool with parameters\n");
             printf("  help                        - Show available commands\n");
             printf("  exit                        - Exit the client\n");
             printf("\nExample: expand example://{name} {\"name\":\"john\"}\n");
             printf("Example: read_template example://{name} {\"name\":\"john\"}\n");
             return 0;
        } else {
            mcp_log_error("Unknown option: %s\n", argv[1]);
            return 1;
        }
    }

    // Create transport
    mcp_transport_t* transport = NULL;
    if (strcmp(transport_type, "stdio") == 0) {
        mcp_log_info("Using stdio transport");
        transport = mcp_transport_stdio_create(); // Use specific create function
    } else if (strcmp(transport_type, "tcp") == 0) {
        mcp_log_info("Using TCP client transport (%s:%d)", host, port);
        transport = mcp_transport_tcp_client_create(host, port);
    } else {
        mcp_log_error("Unknown transport type: %s", transport_type);
        return 1;
    }

    if (transport == NULL) {
        mcp_log_error("Failed to create transport");
        return 1;
    }

    // Create client configuration (example)
    mcp_client_config_t client_config;
    client_config.request_timeout_ms = 50000; // 5 seconds

    // Create the client
    mcp_client_t* client = mcp_client_create(&client_config, transport);
    if (client == NULL) {
        mcp_log_error("Failed to create client");
        //NOTE: transport has been destroyed in mcp_client_create()
        //mcp_transport_destroy(transport); // Use generic destroy
        return 1;
    }

    printf("MCP Client Started. Type 'help' to see available commands.\n");

    char buffer[1024];
    while (1) {
        printf("> ");
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            break; // EOF or error
        }

        // Remove trailing newline
        buffer[strcspn(buffer, "\r\n")] = 0;

        if (strcmp(buffer, "exit") == 0) {
            break;
        } else if (strcmp(buffer, "help") == 0) {
            printf("Available commands:\n");
            printf("  list_resources              - List available resources\n");
            printf("  list_templates              - List available resource templates\n");
            printf("  list_tools                  - List available tools\n");
            printf("  read <uri>                  - Read a resource by URI\n");
            printf("  expand <template> <params>  - Expand a template with parameters\n");
            printf("  read_template <template> <params> - Read a resource using a template\n");
            printf("  call <tool> <params>        - Call a tool with parameters\n");
            printf("  help                        - Show this help message\n");
            printf("  exit                        - Exit the client\n");
            printf("\nExample: expand example://{name} {\"name\":\"john\"}\n");
            printf("Example: read_template example://{name} {\"name\":\"john\"}\n");
        } else if (strncmp(buffer, "list_resources", 14) == 0) {
            mcp_resource_t** resources = NULL;
            size_t count = 0;
            if (mcp_client_list_resources(client, &resources, &count) == 0) {
                printf("Resources (%zu):\n", count);
                for (size_t i = 0; i < count; ++i) {
                    printf("  - URI: %s\n", resources[i]->uri);
                    if (resources[i]->name) printf("    Name: %s\n", resources[i]->name);
                    if (resources[i]->description) printf("    Desc: %s\n", resources[i]->description);
                    mcp_resource_free(resources[i]); // Free individual resource
                }
                free(resources); // Free the array
            } else {
                mcp_log_error("Error listing resources.");
            }
        } else if (strncmp(buffer, "list_templates", 14) == 0) {
             mcp_resource_template_t** templates = NULL;
            size_t count = 0;
            if (mcp_client_list_resource_templates(client, &templates, &count) == 0) {
                printf("Resource Templates (%zu):\n", count);
                for (size_t i = 0; i < count; ++i) {
                    printf("  - URI Template: %s\n", templates[i]->uri_template);
                     if (templates[i]->name) printf("    Name: %s\n", templates[i]->name);
                    if (templates[i]->description) printf("    Desc: %s\n", templates[i]->description);
                    mcp_resource_template_free(templates[i]);
                }
                free(templates);
            } else {
                mcp_log_error("Error listing resource templates.");
            }
        } else if (strncmp(buffer, "expand ", 7) == 0) {
            // Format: expand <template_uri> <params_json>
            char* cmd_copy = strdup(buffer + 7);
            if (cmd_copy) {
                char* template_uri = strtok(cmd_copy, " ");
                char* params_json = strtok(NULL, "\n");

                if (template_uri && params_json) {
                    char* expanded_uri = NULL;
                    if (mcp_client_expand_template(client, template_uri, params_json, &expanded_uri) == 0) {
                        printf("Expanded URI: %s\n", expanded_uri);
                        free(expanded_uri);
                    } else {
                        mcp_log_error("Error expanding template '%s' with params '%s'.", template_uri, params_json);
                    }
                } else {
                    printf("Usage: expand <template_uri> <params_json>\n");
                    printf("Example: expand example://{name}/resource {\"name\":\"test\"}\n");
                }
                free(cmd_copy);
            }
        } else if (strncmp(buffer, "read_template ", 14) == 0) {
            // Format: read_template <template_uri> <params_json>
            char* cmd_copy = strdup(buffer + 14);
            if (cmd_copy) {
                char* template_uri = strtok(cmd_copy, " ");
                char* params_json = strtok(NULL, "\n");

                if (template_uri && params_json) {
                    mcp_content_item_t** content = NULL;
                    size_t count = 0;
                    if (mcp_client_read_resource_with_template(client, template_uri, params_json, &content, &count) == 0) {
                        printf("Resource Content (%zu items):\n", count);
                        for (size_t i = 0; i < count; ++i) {
                            printf("  - Item %zu:\n", i + 1);
                            if (content[i]->mime_type) printf("    MIME: %s\n", content[i]->mime_type);
                            if (content[i]->type == MCP_CONTENT_TYPE_TEXT && content[i]->data) {
                                printf("    Text: %s\n", (char*)content[i]->data);
                            } else {
                                printf("    Data Size: %zu bytes\n", content[i]->data_size);
                            }
                            mcp_content_item_free(content[i]);
                        }
                        free(content);
                    } else {
                        mcp_log_error("Error reading resource with template '%s' and params '%s'.", template_uri, params_json);
                    }
                } else {
                    printf("Usage: read_template <template_uri> <params_json>\n");
                    printf("Example: read_template example://{name}/resource {\"name\":\"test\"}\n");
                }
                free(cmd_copy);
            }
        } else if (strncmp(buffer, "read ", 5) == 0) {
            const char* uri = buffer + 5;
            mcp_content_item_t** content = NULL;
            size_t count = 0;
            if (mcp_client_read_resource(client, uri, &content, &count) == 0) {
                printf("Resource Content (%zu items):\n", count);
                 for (size_t i = 0; i < count; ++i) {
                     printf("  - Item %zu:\n", i + 1);
                     printf("    URI: %s\n", uri); // Assuming URI is same for all items in response
                     if (content[i]->mime_type) printf("    MIME: %s\n", content[i]->mime_type);
                     if (content[i]->type == MCP_CONTENT_TYPE_TEXT && content[i]->data) {
                         printf("    Text: %s\n", (char*)content[i]->data);
                     } else {
                         printf("    Data Size: %zu bytes\n", content[i]->data_size);
                     }
                     mcp_content_item_free(content[i]);
                 }
                 free(content);
            } else {
                mcp_log_error("Error reading resource '%s'.", uri);
            }
        } else if (strncmp(buffer, "list_tools", 10) == 0) {
            mcp_tool_t** tools = NULL;
            size_t count = 0;
            if (mcp_client_list_tools(client, &tools, &count) == 0) {
                printf("Tools (%zu):\n", count);
                for (size_t i = 0; i < count; ++i) {
                    printf("  - Name: %s\n", tools[i]->name);
                    if (tools[i]->description) printf("    Desc: %s\n", tools[i]->description);
                    if (tools[i]->input_schema_count > 0) {
                        printf("    Params:\n");
                        for(size_t j=0; j < tools[i]->input_schema_count; ++j) {
                            printf("      - %s (%s)%s%s\n",
                                   tools[i]->input_schema[j].name,
                                   tools[i]->input_schema[j].type,
                                   tools[i]->input_schema[j].required ? " [required]" : "",
                                   tools[i]->input_schema[j].description ? ": " : "");
                            if(tools[i]->input_schema[j].description) printf("        %s\n", tools[i]->input_schema[j].description);
                        }
                    }
                    mcp_tool_free(tools[i]);
                }
                free(tools);
            } else {
                mcp_log_error("Error listing tools.");
            }
        } else if (strncmp(buffer, "call ", 5) == 0) {
            char* tool_name = strtok(buffer + 5, " ");
            char* args_json = strtok(NULL, ""); // Rest of the string
            if (tool_name) {
                mcp_content_item_t** content = NULL;
                size_t count = 0;
                bool is_error = false;
                if (mcp_client_call_tool(client, tool_name, args_json ? args_json : "{}", &content, &count, &is_error) == 0) {
                    printf("Tool Result (%s, %zu items):\n", is_error ? "ERROR" : "OK", count);
                    for (size_t i = 0; i < count; ++i) {
                         printf("  - Item %zu:\n", i + 1);
                         if (content[i]->mime_type) printf("    MIME: %s\n", content[i]->mime_type);
                         if (content[i]->type == MCP_CONTENT_TYPE_TEXT && content[i]->data) {
                             printf("    Text: %s\n", (char*)content[i]->data);
                         } else {
                             printf("    Data Size: %zu bytes\n", content[i]->data_size);
                         }
                         mcp_content_item_free(content[i]);
                    }
                    free(content);
                } else {
                     mcp_log_error("Error calling tool '%s'.", tool_name);
                }
            } else {
                 mcp_log_error("Invalid call command. Usage: call <tool_name> [json_arguments]");
            }
        }
         else {
            mcp_log_error("Unknown command: %s", buffer);
        }
    }

    printf("Exiting client...\n");
    mcp_client_destroy(client); // This will also stop and destroy the transport

    mcp_arena_destroy_current_thread(); // Clean up thread-local arena
    mcp_log_close(); // Close log file if open

    return 0;
}
