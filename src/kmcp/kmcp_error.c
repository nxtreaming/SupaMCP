#include "kmcp_error.h"

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
        case KMCP_ERROR_INTERNAL:
            return "Internal error";
        default:
            return "Unknown error";
    }
}
