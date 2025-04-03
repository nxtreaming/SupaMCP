#include "internal/mcp_server_internal.h"
#include <stdlib.h>

// --- Internal Response Helper Functions ---

/**
 * @internal
 * @brief Helper function to construct a JSON-RPC error response string.
 * @param id The request ID.
 * @param code The MCP error code.
 * @param message The error message string (typically a const literal).
 * @return A malloc'd JSON string representing the error response, or NULL on allocation failure.
 */
char* create_error_response(uint64_t id, mcp_error_code_t code, const char* message) {
    mcp_response_t response; // Stack allocation is fine here
    response.id = id;
    response.error_code = code;
    response.error_message = message; // String literal, no copy needed
    response.result = NULL;

    mcp_message_t msg; // Stack allocation
    msg.type = MCP_MESSAGE_TYPE_RESPONSE;
    msg.response = response;

    // Stringify the response message (allocates the final string)
    return mcp_json_stringify_message(&msg);
}

/**
 * @internal
 * @brief Helper function to construct a JSON-RPC success response string.
 * @param id The request ID.
 * @param result_str A malloc'd string containing the JSON representation of the result.
 *                   This function takes ownership of this string and frees it.
 * @return A malloc'd JSON string representing the success response, or NULL on allocation failure.
 */
char* create_success_response(uint64_t id, char* result_str) {
    mcp_response_t response; // Stack allocation
    response.id = id;
    response.error_code = MCP_ERROR_NONE;
    response.error_message = NULL;
    // Temporarily assign the result string pointer. The stringify function
    // will handle embedding it correctly.
    response.result = result_str;

    mcp_message_t msg; // Stack allocation
    msg.type = MCP_MESSAGE_TYPE_RESPONSE;
    msg.response = response;

    // Stringify the complete response message (allocates the final string)
    char* response_msg_str = mcp_json_stringify_message(&msg);

    // Free the original result string now that stringify is done with it.
    free(result_str);

    return response_msg_str;
}
