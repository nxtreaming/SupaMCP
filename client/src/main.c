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

int main(int argc, char** argv) {
    // Default transport
    const char* transport_type = "stdio";
    const char* host = "127.0.0.1";
    uint16_t port = 8080;

    // Basic argument parsing
    if (argc > 1) {
        if (strcmp(argv[1], "--tcp") == 0) {
            transport_type = "tcp";
            if (argc > 2) host = argv[2];
            if (argc > 3) port = (uint16_t)atoi(argv[3]);
        } else if (strcmp(argv[1], "--stdio") == 0) {
            transport_type = "stdio";
        } else if (strcmp(argv[1], "--help") == 0) {
             printf("Usage: %s [--stdio | --tcp [HOST [PORT]]]\n", argv[0]);
             return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[1]);
            return 1;
        }
    }

    // Create transport
    mcp_transport_t* transport = NULL;
    if (strcmp(transport_type, "stdio") == 0) {
        printf("Using stdio transport\n");
        transport = mcp_transport_stdio_create(); // Use specific create function
    } else if (strcmp(transport_type, "tcp") == 0) {
        printf("Using TCP client transport (%s:%d)\n", host, port);
        transport = mcp_transport_tcp_client_create(host, port);
    } else {
        fprintf(stderr, "Unknown transport type: %s\n", transport_type);
        return 1;
    }

    if (transport == NULL) {
        fprintf(stderr, "Failed to create transport\n");
        return 1;
    }

    // Create client configuration (example)
    mcp_client_config_t client_config;
    client_config.request_timeout_ms = 5000; // 5 seconds

    // Create the client
    mcp_client_t* client = mcp_client_create(&client_config, transport);
    if (client == NULL) {
        fprintf(stderr, "Failed to create client\n");
        mcp_transport_destroy(transport); // Use generic destroy
        return 1;
    }

    printf("MCP Client Started. Enter commands (e.g., 'list_resources', 'read example://hello', 'call echo {\"text\":\"Hello Tool!\"}', 'exit').\n");

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
                fprintf(stderr, "Error listing resources.\n");
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
                fprintf(stderr, "Error listing resource templates.\n");
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
                fprintf(stderr, "Error reading resource '%s'.\n", uri);
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
                fprintf(stderr, "Error listing tools.\n");
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
                     fprintf(stderr, "Error calling tool '%s'.\n", tool_name);
                }
            } else {
                 fprintf(stderr, "Invalid call command. Usage: call <tool_name> [json_arguments]\n");
            }
        }
         else {
            fprintf(stderr, "Unknown command: %s\n", buffer);
        }
    }

    printf("Exiting client...\n");
    mcp_client_destroy(client); // This will also stop and destroy the transport

    return 0;
}
