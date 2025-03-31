#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../../include/mcp_json.h"
#include "../include/mcp_client.h"
#include "../../include/mcp_transport.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

// Max line length for placeholder synchronous receive logic
#define MAX_LINE_LENGTH 4096

/**
 * MCP client structure (Internal definition)
 */
struct mcp_client {
    mcp_client_config_t config;     // Store configuration
    mcp_transport_t* transport;     // Transport handle (owned by client)
    uint64_t next_id;               // Counter for request IDs
    // Add state for handling asynchronous responses if needed later
    // e.g., mutex, condition variable, response map
};

/**
 * @brief Create an MCP client instance.
 */
mcp_client_t* mcp_client_create(const mcp_client_config_t* config, mcp_transport_t* transport) {
    if (config == NULL || transport == NULL) {
        return NULL; // Config and transport are required
    }

    mcp_client_t* client = (mcp_client_t*)malloc(sizeof(mcp_client_t));
    if (client == NULL) {
        // If client allocation fails, should we destroy the passed transport?
        // The API doc says client takes ownership, so yes.
        mcp_transport_destroy(transport);
        return NULL;
    }

    // Store config and transport
    client->config = *config; // Copy config struct
    client->transport = transport;
    client->next_id = 1;

    // TODO: Initialize any synchronization primitives if adding async support

    // Start the transport's receive mechanism (e.g., read thread)
    // We need a callback to handle incoming responses.
    // For now, the send_request function will handle receiving synchronously.
    // If we implement a proper receive callback later, call mcp_transport_start here.
    /*
    if (mcp_transport_start(client->transport, mcp_client_receive_callback, client) != 0) {
        mcp_client_destroy(client); // Will destroy transport
        return NULL;
    }
    */

    return client;
}

/**
 * Destroy an MCP client
 */
void mcp_client_destroy(mcp_client_t* client) {
    if (client == NULL) {
        return;
    }

    // Transport is stopped and destroyed here
    if (client->transport != NULL) {
        mcp_transport_stop(client->transport); // Ensure it's stopped
        mcp_transport_destroy(client->transport);
        client->transport = NULL;
    }

    // TODO: Clean up any synchronization primitives

    free(client);
}

// Connect/Disconnect functions are removed as transport is handled at creation/destruction.

/**
 * Send a request to the MCP server and receive a response
 */
static int mcp_client_send_request(
    mcp_client_t* client,
    const char* method,
    const char* params,
    char** result,
    mcp_error_code_t* error_code,
    char** error_message
) {
    if (client == NULL || method == NULL || result == NULL || error_code == NULL || error_message == NULL) {
        return -1;
    }

    if (client->transport == NULL) {
        return -1;
    }

    // Initialize result and error message
    *result = NULL;
    *error_message = NULL;
    *error_code = MCP_ERROR_NONE;

    // Create request JSON
    char* request_json = NULL;
    if (params != NULL) {
        request_json = mcp_json_format_request(client->next_id, method, params);
    } else {
        request_json = mcp_json_format_request(client->next_id, method, "{}");
    }
    if (request_json == NULL) {
        return -1;
    }

    // Send request
    if (mcp_transport_send(client->transport, request_json, strlen(request_json)) != 0) {
        free(request_json);
        return -1;
    }
    free(request_json);

    // Receive response
    // This is currently blocking and synchronous. A real client might use a
    // separate receive thread or asynchronous I/O, started via mcp_transport_start,
    // which would use a callback to handle responses and match them to requests.
    // For this simple example, we assume a synchronous request-response model
    // and that the transport layer doesn't have its own receive loop running.
    // We also lack timeout handling here based on client->config.request_timeout_ms.
    char* response_json = NULL;
    // size_t response_size = 0; // No longer used with fgets placeholder
    // The generic mcp_transport_receive function was removed in the interface refactor.
    // We need a way to receive. For stdio, this means reading from stdin.
    // This synchronous approach is problematic for robust clients.
    // Let's simulate a blocking read for now (assuming stdio).
    char line_buffer[MAX_LINE_LENGTH]; // Assuming MAX_LINE_LENGTH is defined somewhere accessible
    if (fgets(line_buffer, sizeof(line_buffer), stdin) == NULL) {
         fprintf(stderr, "Failed to read response from transport.\n");
         return -1; // Read error or EOF
    }
    line_buffer[strcspn(line_buffer, "\r\n")] = 0; // Remove newline
    response_json = strdup(line_buffer); // Use malloc for the response string
    if (!response_json) {
         return -1; // Allocation failure
    }
    // response_size = strlen(response_json); // Not strictly needed for parsing

    // TODO: Implement proper receive logic, potentially using the transport start callback mechanism.
    // if (mcp_transport_receive(client->transport, &response_json, &response_size) != 0) {
    //     return -1;
    // }

    // Parse response
    uint64_t id;
    if (mcp_json_parse_response(response_json, &id, error_code, error_message, result) != 0) {
        free(response_json);
        return -1;
    }
    free(response_json);

    // Check if the response ID matches the request ID
    if (id != client->next_id) {
        free(*result);
        *result = NULL;
        free(*error_message);
        *error_message = NULL;
        return -1;
    }

    // Increment the next ID
    client->next_id++;

    return 0;
}

/**
 * List resources from the MCP server
 */
int mcp_client_list_resources(
    mcp_client_t* client,
    mcp_resource_t*** resources,
    size_t* count
) {
    if (client == NULL || resources == NULL || count == NULL) {
        return -1;
    }

    // Initialize resources and count
    *resources = NULL;
    *count = 0;

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "list_resources", NULL, &result, &error_code, &error_message) != 0) {
        free(error_message);
        return -1;
    }

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_resources(result, resources, count) != 0) {
        free(error_message);
        free(result);
        return -1;
    }

    free(error_message);
    free(result);
    return 0;
}

/**
 * List resource templates from the MCP server
 */
int mcp_client_list_resource_templates(
    mcp_client_t* client,
    mcp_resource_template_t*** templates,
    size_t* count
) {
    if (client == NULL || templates == NULL || count == NULL) {
        return -1;
    }

    // Initialize templates and count
    *templates = NULL;
    *count = 0;

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "list_resource_templates", NULL, &result, &error_code, &error_message) != 0) {
        free(error_message);
        return -1;
    }

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_resource_templates(result, templates, count) != 0) {
        free(error_message);
        free(result);
        return -1;
    }

    free(error_message);
    free(result);
    return 0;
}

/**
 * Read a resource from the MCP server
 */
int mcp_client_read_resource(
    mcp_client_t* client,
    const char* uri,
    mcp_content_item_t*** content,
    size_t* count
) {
    if (client == NULL || uri == NULL || content == NULL || count == NULL) {
        return -1;
    }

    // Initialize content and count
    *content = NULL;
    *count = 0;

    // Create params
    char* params = mcp_json_format_read_resource_params(uri);
    if (params == NULL) {
        return -1;
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "read_resource", params, &result, &error_code, &error_message) != 0) {
        free(params);
        free(error_message);
        return -1;
    }
    free(params);

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_content(result, content, count) != 0) {
        free(error_message);
        free(result);
        return -1;
    }

    free(error_message);
    free(result);
    return 0;
}

/**
 * List tools from the MCP server
 */
int mcp_client_list_tools(
    mcp_client_t* client,
    mcp_tool_t*** tools,
    size_t* count
) {
    if (client == NULL || tools == NULL || count == NULL) {
        return -1;
    }

    // Initialize tools and count
    *tools = NULL;
    *count = 0;

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "list_tools", NULL, &result, &error_code, &error_message) != 0) {
        free(error_message);
        return -1;
    }

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_tools(result, tools, count) != 0) {
        free(error_message);
        free(result);
        return -1;
    }

    free(error_message);
    free(result);
    return 0;
}

/**
 * Call a tool on the MCP server
 */
int mcp_client_call_tool(
    mcp_client_t* client,
    const char* name,
    const char* arguments,
    mcp_content_item_t*** content,
    size_t* count,
    bool* is_error
) {
    if (client == NULL || name == NULL || content == NULL || count == NULL || is_error == NULL) {
        return -1;
    }

    // Initialize content, count, and is_error
    *content = NULL;
    *count = 0;
    *is_error = false;

    // Create params
    char* params = mcp_json_format_call_tool_params(name, arguments);
    if (params == NULL) {
        return -1;
    }

    // Send request
    char* result = NULL;
    mcp_error_code_t error_code;
    char* error_message = NULL;
    if (mcp_client_send_request(client, "call_tool", params, &result, &error_code, &error_message) != 0) {
        free(params);
        free(error_message);
        return -1;
    }
    free(params);

    // Check for error
    if (error_code != MCP_ERROR_NONE) {
        free(error_message);
        free(result);
        return -1;
    }

    // Parse result
    if (mcp_json_parse_tool_result(result, content, count, is_error) != 0) {
        free(error_message);
        free(result);
        return -1;
    }

    free(error_message);
    free(result);
    return 0;
}

/**
 * Free an array of resources
 */
void mcp_client_free_resources(mcp_resource_t** resources, size_t count) {
    if (resources == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        mcp_resource_free(resources[i]);
    }
    free(resources);
}

/**
 * Free an array of resource templates
 */
void mcp_client_free_resource_templates(mcp_resource_template_t** templates, size_t count) {
    if (templates == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        mcp_resource_template_free(templates[i]);
    }
    free(templates);
}

/**
 * Free an array of content items
 */
void mcp_client_free_content(mcp_content_item_t** content, size_t count) {
    if (content == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        mcp_content_item_free(content[i]);
    }
    free(content);
}

/**
 * Free an array of tools
 */
void mcp_client_free_tools(mcp_tool_t** tools, size_t count) {
    if (tools == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        mcp_tool_free(tools[i]);
    }
    free(tools);
}
