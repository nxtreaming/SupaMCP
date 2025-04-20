#include "kmcp_error.h"
#include "mcp_log.h"
#include <stdarg.h>
#include <stdio.h>

/**
 * @brief Get error message for a KMCP error code
 */
const char* kmcp_error_message(kmcp_error_t error_code) {
    switch (error_code) {
        case KMCP_SUCCESS:
            return "Success";
        case KMCP_ERROR_INVALID_PARAMETER:
            return "Invalid parameter";
        case KMCP_ERROR_MEMORY_ALLOCATION:
            return "Memory allocation failed";
        case KMCP_ERROR_FILE_NOT_FOUND:
            return "File not found";
        case KMCP_ERROR_PARSE_FAILED:
            return "Parse failed";
        case KMCP_ERROR_CONNECTION_FAILED:
            return "Connection failed";
        case KMCP_ERROR_TIMEOUT:
            return "Operation timed out";
        case KMCP_ERROR_NOT_IMPLEMENTED:
            return "Feature not implemented";
        case KMCP_ERROR_PERMISSION_DENIED:
            return "Permission denied";
        case KMCP_ERROR_PROCESS_FAILED:
            return "Process operation failed";
        case KMCP_ERROR_SERVER_NOT_FOUND:
            return "Server not found";
        case KMCP_ERROR_TOOL_NOT_FOUND:
            return "Tool not found";
        case KMCP_ERROR_RESOURCE_NOT_FOUND:
            return "Resource not found";
        case KMCP_ERROR_TOOL_EXECUTION:
            return "Tool execution failed";
        case KMCP_ERROR_THREAD_CREATION:
            return "Thread creation failed";
        case KMCP_ERROR_SSL_CERTIFICATE:
            return "SSL certificate error";
        case KMCP_ERROR_SSL_HANDSHAKE:
            return "SSL handshake failed";
        case KMCP_ERROR_CONFIG_INVALID:
            return "Invalid configuration";
        case KMCP_ERROR_SERVER_ERROR:
            return "Server returned an error";
        case KMCP_ERROR_NETWORK_ERROR:
            return "Network error";
        case KMCP_ERROR_PROTOCOL_ERROR:
            return "Protocol error";
        case KMCP_ERROR_RESOURCE_BUSY:
            return "Resource is busy";
        case KMCP_ERROR_OPERATION_CANCELED:
            return "Operation was canceled";
        case KMCP_ERROR_IO:
            return "Input/output error";
        case KMCP_ERROR_NOT_FOUND:
            return "Item not found";
        case KMCP_ERROR_ALREADY_EXISTS:
            return "Item already exists";
        case KMCP_ERROR_INVALID_OPERATION:
            return "Invalid operation";
        case KMCP_ERROR_INTERNAL:
            return "Internal error";
        default:
            return "Unknown error";
    }
}

/**
 * @brief Convert MCP error code to KMCP error code
 */
kmcp_error_t kmcp_error_from_mcp(int mcp_error) {
    // MCP error codes are defined in mcp_types.h
    // Map MCP error codes to KMCP error codes
    if (mcp_error == 0) { // MCP_ERROR_NONE
        return KMCP_SUCCESS;
    }

    switch (mcp_error) {
        case 1: // MCP_ERROR_INVALID_PARAMETER
            return KMCP_ERROR_INVALID_PARAMETER;
        case 2: // MCP_ERROR_MEMORY_ALLOCATION
            return KMCP_ERROR_MEMORY_ALLOCATION;
        case 3: // MCP_ERROR_FILE_NOT_FOUND
            return KMCP_ERROR_FILE_NOT_FOUND;
        case 4: // MCP_ERROR_PARSE_FAILED
            return KMCP_ERROR_PARSE_FAILED;
        case 5: // MCP_ERROR_CONNECTION_FAILED
            return KMCP_ERROR_CONNECTION_FAILED;
        case 6: // MCP_ERROR_TIMEOUT
            return KMCP_ERROR_TIMEOUT;
        case 7: // MCP_ERROR_NOT_IMPLEMENTED
            return KMCP_ERROR_NOT_IMPLEMENTED;
        case 8: // MCP_ERROR_PERMISSION_DENIED
            return KMCP_ERROR_PERMISSION_DENIED;
        case 11: // MCP_ERROR_TOOL_NOT_FOUND
            return KMCP_ERROR_TOOL_NOT_FOUND;
        case 12: // MCP_ERROR_RESOURCE_NOT_FOUND
            return KMCP_ERROR_RESOURCE_NOT_FOUND;
        case 13: // MCP_ERROR_TOOL_EXECUTION
            return KMCP_ERROR_TOOL_EXECUTION;
        default:
            return KMCP_ERROR_INTERNAL;
    }
}

/**
 * @brief Log an error with the given error code and message
 */
kmcp_error_t kmcp_error_log(kmcp_error_t error_code, const char* format, ...) {
    if (error_code == KMCP_SUCCESS) {
        return KMCP_SUCCESS;
    }

    // Format the error message
    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    // Log the error
    mcp_log_error("%s: %s", message, kmcp_error_message(error_code));

    return error_code;
}
