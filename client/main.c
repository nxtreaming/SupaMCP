#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcp_client.h"
#include "mcp_stdio_transport.h"
#include "mcp_tcp_client_transport.h"
#include "mcp_http_client_transport.h"
#include "mcp_sthttp_client_transport.h"
#include "mcp_transport_factory.h"
#include "mcp_websocket_transport.h"
#include "mcp_mqtt_client_transport.h"
#include "mcp_log.h"
#include "mcp_thread_local.h"

int main(int argc, char** argv) {
    const char* transport_type = "stdio";
    const char* host = "127.0.0.1";
    uint16_t port = 8080;
    const char* ws_path = "/ws"; // Default WebSocket path
    bool use_ssl = false;
    const char* api_key = NULL;
    uint32_t timeout_ms = 30000; // 30 seconds default timeout
    bool enable_sessions = true;
    bool enable_sse_streams = true;
    bool auto_reconnect_sse = true;

    // MQTT-specific options
    const char* mqtt_client_id = NULL;
    const char* mqtt_username = NULL;
    const char* mqtt_password = NULL;
    const char* mqtt_topic_prefix = "mcp/";
    int mqtt_qos = 1;
    bool mqtt_clean_session = true;

    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);

    // Initialize thread-local storage (arena) for the main thread
    // Using 1MB as the initial size. Adjust if needed.
    if (mcp_arena_init_current_thread(1024 * 1024) != 0) {
        mcp_log_error("Failed to initialize thread-local arena for main thread.");
        mcp_log_close();
        return 1;
    }

    // Basic argument parsing
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tcp") == 0) {
            transport_type = "tcp";
        } else if (strncmp(argv[i], "--host=", 7) == 0) {
            host = argv[i] + 7;
        } else if (strncmp(argv[i], "--port=", 7) == 0) {
            port = (uint16_t)atoi(argv[i] + 7);
        } else if (strcmp(argv[i], "--http") == 0) {
            transport_type = "http";
        } else if (strcmp(argv[i], "--https") == 0) {
            transport_type = "http";
            use_ssl = true;
        } else if (strcmp(argv[i], "--sthttp") == 0) {
            transport_type = "sthttp";
        } else if (strcmp(argv[i], "--sthttps") == 0) {
            transport_type = "sthttp";
            use_ssl = true;
        } else if (strcmp(argv[i], "--websocket") == 0 || strcmp(argv[i], "--ws") == 0) {
            transport_type = "websocket";
        } else if (strncmp(argv[i], "--ws-path=", 10) == 0) {
            ws_path = argv[i] + 10;
        } else if (strcmp(argv[i], "--mqtt") == 0) {
            transport_type = "mqtt";
        } else if (strncmp(argv[i], "--mqtt-client-id=", 17) == 0) {
            mqtt_client_id = argv[i] + 17;
        } else if (strncmp(argv[i], "--mqtt-username=", 16) == 0) {
            mqtt_username = argv[i] + 16;
        } else if (strncmp(argv[i], "--mqtt-password=", 16) == 0) {
            mqtt_password = argv[i] + 16;
        } else if (strncmp(argv[i], "--mqtt-topic-prefix=", 20) == 0) {
            mqtt_topic_prefix = argv[i] + 20;
        } else if (strncmp(argv[i], "--mqtt-qos=", 11) == 0) {
            mqtt_qos = atoi(argv[i] + 11);
        } else if (strcmp(argv[i], "--mqtt-clean-session") == 0) {
            mqtt_clean_session = true;
        } else if (strcmp(argv[i], "--mqtt-persistent-session") == 0) {
            mqtt_clean_session = false;
        } else if (strcmp(argv[i], "--stdio") == 0) {
            transport_type = "stdio";
        } else if (strncmp(argv[i], "--api-key=", 10) == 0) {
            api_key = argv[i] + 10;
        } else if (strncmp(argv[i], "--timeout=", 10) == 0) {
            timeout_ms = (uint32_t)atoi(argv[i] + 10);
        } else if (strcmp(argv[i], "--api-key") == 0) {
            if (i + 1 < argc) api_key = argv[++i];
        } else if (strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 < argc) timeout_ms = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--host") == 0) {
            if (i + 1 < argc) host = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0) {
            if (i + 1 < argc) port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--enable-sessions") == 0) {
            enable_sessions = true;
        } else if (strcmp(argv[i], "--disable-sessions") == 0) {
            enable_sessions = false;
        } else if (strcmp(argv[i], "--enable-sse") == 0) {
            enable_sse_streams = true;
        } else if (strcmp(argv[i], "--disable-sse") == 0) {
            enable_sse_streams = false;
        } else if (strcmp(argv[i], "--enable-auto-reconnect") == 0) {
            auto_reconnect_sse = true;
        } else if (strcmp(argv[i], "--disable-auto-reconnect") == 0) {
            auto_reconnect_sse = false;
        } else if (strcmp(argv[i], "--help") == 0) {
             printf("Usage: %s [OPTIONS]\n\n", argv[0]);
             printf("Options:\n");
             printf("  --stdio                   Use stdio transport (default)\n");
             printf("  --tcp                     Use TCP transport\n");
             printf("  --http                    Use HTTP transport\n");
             printf("  --https                   Use HTTPS transport\n");
             printf("  --sthttp                  Use Streamable HTTP transport\n");
             printf("  --sthttps                 Use Streamable HTTPS transport\n");
             printf("  --websocket, --ws         Use WebSocket transport\n");
             printf("  --ws-path=PATH            Set WebSocket path (default: /ws)\n");
             printf("  --mqtt                    Use MQTT transport\n");
             printf("  --mqtt-client-id=ID       Set MQTT client ID\n");
             printf("  --mqtt-username=USER      Set MQTT username\n");
             printf("  --mqtt-password=PASS      Set MQTT password\n");
             printf("  --mqtt-topic-prefix=PREFIX Set MQTT topic prefix (default: mcp/)\n");
             printf("  --mqtt-qos=QOS            Set MQTT QoS level (0, 1, or 2, default: 1)\n");
             printf("  --mqtt-clean-session      Use MQTT clean session (default)\n");
             printf("  --mqtt-persistent-session Use MQTT persistent session\n");
             printf("  --host=HOST               Set host to connect to (default: 127.0.0.1)\n");
             printf("  --port=PORT               Set port to connect to (default: 8080)\n");
             printf("  --host HOST               Set host to connect to (default: 127.0.0.1)\n");
             printf("  --port PORT               Set port to connect to (default: 8080)\n");
             printf("  --api-key=KEY             Set API key for authentication\n");
             printf("  --api-key KEY             Set API key for authentication\n");
             printf("  --timeout=MS              Set request timeout in milliseconds (default: 30000)\n");
             printf("  --timeout MS              Set request timeout in milliseconds (default: 30000)\n");
             printf("  --enable-sessions         Enable session support for Streamable HTTP (default)\n");
             printf("  --disable-sessions        Disable session support for Streamable HTTP\n");
             printf("  --enable-sse              Enable SSE streams for Streamable HTTP (default)\n");
             printf("  --disable-sse             Disable SSE streams for Streamable HTTP\n");
             printf("  --enable-auto-reconnect   Enable auto-reconnect for SSE (default)\n");
             printf("  --disable-auto-reconnect  Disable auto-reconnect for SSE\n");
             printf("  --help                    Show this help message\n\n");
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
            mcp_log_error("Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    // Create transport
    mcp_transport_t* transport = NULL;
    if (strcmp(transport_type, "stdio") == 0) {
        mcp_log_info("Using stdio transport");
        transport = mcp_transport_stdio_create();
    } else if (strcmp(transport_type, "tcp") == 0) {
        mcp_log_info("Using TCP client transport (%s:%d)", host, port);
        transport = mcp_transport_tcp_client_create(host, port);
    } else if (strcmp(transport_type, "http") == 0) {
        mcp_log_info("Using HTTP%s client transport (%s:%d)", use_ssl ? "S" : "", host, port);

        // Create HTTP client configuration
        mcp_http_client_config_t config = {0};
        config.host = host;
        config.port = port;
        config.use_ssl = use_ssl;
        config.cert_path = NULL; // Could add command line options for these
        config.key_path = NULL;
        config.timeout_ms = timeout_ms;
        config.api_key = api_key;

        transport = mcp_transport_http_client_create_with_config(&config);
    } else if (strcmp(transport_type, "sthttp") == 0) {
        mcp_log_info("Using Streamable HTTP%s client transport (%s:%d)", use_ssl ? "S" : "", host, port);

        // Create Streamable HTTP client configuration
        mcp_sthttp_client_config_t config = MCP_STHTTP_CLIENT_CONFIG_DEFAULT;
        config.host = host;
        config.port = port;
        config.use_ssl = use_ssl;
        config.api_key = api_key;
        config.connect_timeout_ms = timeout_ms;
        config.request_timeout_ms = timeout_ms;
        config.enable_sessions = enable_sessions;
        config.enable_sse_streams = enable_sse_streams;
        config.auto_reconnect_sse = auto_reconnect_sse;

        transport = mcp_transport_sthttp_client_create(&config);
    } else if (strcmp(transport_type, "websocket") == 0) {
        mcp_log_info("Using WebSocket client transport (%s:%d%s)", host, port, ws_path);

        // Create WebSocket transport configuration
        mcp_transport_config_t config = {0};
        config.ws.host = host;
        config.ws.port = port;
        config.ws.path = ws_path;
        config.ws.use_ssl = use_ssl ? 1 : 0;
        config.ws.connect_timeout_ms = timeout_ms;

        transport = mcp_transport_factory_create(MCP_TRANSPORT_WS_CLIENT, &config);
    } else if (strcmp(transport_type, "mqtt") == 0) {
        mcp_log_info("Using MQTT client transport (%s:%d)", host, port);

        // Create MQTT client configuration
        mcp_mqtt_client_config_t config = MCP_MQTT_CLIENT_CONFIG_DEFAULT;
        config.base.host = host;
        config.base.port = port;
        config.base.client_id = mqtt_client_id;
        config.base.username = mqtt_username;
        config.base.password = mqtt_password;
        config.base.topic_prefix = mqtt_topic_prefix;
        config.base.qos = mqtt_qos;
        config.base.clean_session = mqtt_clean_session;
        config.base.use_ssl = use_ssl;
        config.base.connect_timeout_ms = timeout_ms;
        config.base.message_timeout_ms = timeout_ms;
        config.auto_reconnect = true;
        config.enable_metrics = true;

        transport = mcp_transport_mqtt_client_create_with_config(&config);
    } else {
        mcp_log_error("Unknown transport type: %s", transport_type);
        return 1;
    }

    if (transport == NULL) {
        mcp_log_error("Failed to create transport");
        return 1;
    }

    // Create client configuration
    mcp_client_config_t client_config;
    client_config.request_timeout_ms = 50000;

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
            break;
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
                    if (resources[i]->name)
                        printf("    Name: %s\n", resources[i]->name);
                    if (resources[i]->description)
                        printf("    Desc: %s\n", resources[i]->description);
                    mcp_resource_free(resources[i]);
                }
                free(resources);
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
                    if (templates[i]->name)
                        printf("    Name: %s\n", templates[i]->name);
                    if (templates[i]->description)
                        printf("    Desc: %s\n", templates[i]->description);
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
                            if (content[i]->mime_type)
                                printf("    MIME: %s\n", content[i]->mime_type);
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
                     if (content[i]->mime_type)
                         printf("    MIME: %s\n", content[i]->mime_type);
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
                    if (tools[i]->description)
                        printf("    Desc: %s\n", tools[i]->description);
                    if (tools[i]->input_schema_count > 0) {
                        printf("    Params:\n");
                        for(size_t j=0; j < tools[i]->input_schema_count; ++j) {
                            printf("      - %s (%s)%s%s\n",
                                   tools[i]->input_schema[j].name,
                                   tools[i]->input_schema[j].type,
                                   tools[i]->input_schema[j].required ? " [required]" : "",
                                   tools[i]->input_schema[j].description ? ": " : "");
                            if(tools[i]->input_schema[j].description)
                                printf("        %s\n", tools[i]->input_schema[j].description);
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
                         if (content[i]->mime_type)
                             printf("    MIME: %s\n", content[i]->mime_type);
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
    // This will also stop and destroy the transport
    mcp_client_destroy(client);

    // Clean up thread-local arena
    mcp_arena_destroy_current_thread();
    mcp_log_close();

    return 0;
}
