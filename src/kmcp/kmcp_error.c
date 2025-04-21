#include "kmcp_error.h"
#include "mcp_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Get error message for a KMCP error code
 *
 * @param error_code The error code to get a message for
 * @return const char* Returns a human-readable error message
 */
const char* kmcp_error_message(kmcp_error_t error_code) {
    switch (error_code) {
        case KMCP_SUCCESS:
            return "Success";

        /* System category errors */
        case KMCP_ERROR_INVALID_PARAMETER:
            return "Invalid parameter";
        case KMCP_ERROR_MEMORY_ALLOCATION:
            return "Memory allocation failed";
        case KMCP_ERROR_FILE_NOT_FOUND:
            return "File not found";
        case KMCP_ERROR_PARSE_FAILED:
            return "Parse failed";
        case KMCP_ERROR_TIMEOUT:
            return "Operation timed out";
        case KMCP_ERROR_NOT_IMPLEMENTED:
            return "Feature not implemented";
        case KMCP_ERROR_PERMISSION_DENIED:
            return "Permission denied";
        case KMCP_ERROR_PROCESS_FAILED:
            return "Process operation failed";
        case KMCP_ERROR_THREAD_CREATION:
            return "Thread creation failed";
        case KMCP_ERROR_IO:
            return "Input/output error";
        case KMCP_ERROR_NOT_FOUND:
            return "Item not found";
        case KMCP_ERROR_ALREADY_EXISTS:
            return "Item already exists";
        case KMCP_ERROR_INVALID_OPERATION:
            return "Invalid operation";

        /* Network category errors */
        case KMCP_ERROR_CONNECTION_FAILED:
            return "Connection failed";
        case KMCP_ERROR_NETWORK_ERROR:
            return "Network error";
        case KMCP_ERROR_SSL_CERTIFICATE:
            return "SSL certificate error";
        case KMCP_ERROR_SSL_HANDSHAKE:
            return "SSL handshake failed";

        /* Protocol category errors */
        case KMCP_ERROR_PROTOCOL_ERROR:
            return "Protocol error";

        /* Resource category errors */
        case KMCP_ERROR_RESOURCE_NOT_FOUND:
            return "Resource not found";
        case KMCP_ERROR_RESOURCE_BUSY:
            return "Resource is busy";

        /* Configuration category errors */
        case KMCP_ERROR_CONFIG_INVALID:
            return "Invalid configuration";

        /* Tool category errors */
        case KMCP_ERROR_TOOL_NOT_FOUND:
            return "Tool not found";
        case KMCP_ERROR_TOOL_EXECUTION:
            return "Tool execution failed";

        /* Server category errors */
        case KMCP_ERROR_SERVER_NOT_FOUND:
            return "Server not found";
        case KMCP_ERROR_SERVER_ERROR:
            return "Server returned an error";

        /* Client category errors */
        case KMCP_ERROR_OPERATION_CANCELED:
            return "Operation was canceled";

        /* Internal category errors */
        case KMCP_ERROR_INTERNAL:
            return "Internal error";

        default:
            return "Unknown error";
    }
}

/**
 * @brief Get the error category for an error code
 *
 * @param error_code Error code
 * @return kmcp_error_category_t Error category
 */
kmcp_error_category_t kmcp_error_get_category(kmcp_error_t error_code) {
    if (error_code == KMCP_SUCCESS) {
        return KMCP_ERROR_CATEGORY_NONE;
    }

    // Determine category based on error code range
    if (error_code >= -99 && error_code < 0) {
        return KMCP_ERROR_CATEGORY_SYSTEM;
    } else if (error_code >= -199 && error_code <= -100) {
        return KMCP_ERROR_CATEGORY_NETWORK;
    } else if (error_code >= -299 && error_code <= -200) {
        return KMCP_ERROR_CATEGORY_PROTOCOL;
    } else if (error_code >= -399 && error_code <= -300) {
        return KMCP_ERROR_CATEGORY_RESOURCE;
    } else if (error_code >= -499 && error_code <= -400) {
        return KMCP_ERROR_CATEGORY_CONFIGURATION;
    } else if (error_code >= -599 && error_code <= -500) {
        return KMCP_ERROR_CATEGORY_SECURITY;
    } else if (error_code >= -699 && error_code <= -600) {
        return KMCP_ERROR_CATEGORY_TOOL;
    } else if (error_code >= -799 && error_code <= -700) {
        return KMCP_ERROR_CATEGORY_SERVER;
    } else if (error_code >= -899 && error_code <= -800) {
        return KMCP_ERROR_CATEGORY_CLIENT;
    } else if (error_code >= -999 && error_code <= -900) {
        return KMCP_ERROR_CATEGORY_INTERNAL;
    }

    return KMCP_ERROR_CATEGORY_INTERNAL; // Default for unknown error codes
}

/**
 * @brief Get the error severity for an error code
 *
 * @param error_code Error code
 * @return kmcp_error_severity_t Error severity
 */
kmcp_error_severity_t kmcp_error_get_severity(kmcp_error_t error_code) {
    if (error_code == KMCP_SUCCESS) {
        return KMCP_ERROR_SEVERITY_NONE;
    }

    // Determine severity based on error category
    kmcp_error_category_t category = kmcp_error_get_category(error_code);

    switch (category) {
        case KMCP_ERROR_CATEGORY_SYSTEM:
            // Some system errors are fatal, others are just errors
            if (error_code == KMCP_ERROR_MEMORY_ALLOCATION) {
                return KMCP_ERROR_SEVERITY_FATAL;
            }
            return KMCP_ERROR_SEVERITY_ERROR;

        case KMCP_ERROR_CATEGORY_NETWORK:
            // Network errors are usually recoverable
            return KMCP_ERROR_SEVERITY_ERROR;

        case KMCP_ERROR_CATEGORY_SECURITY:
            // Security errors are usually fatal
            return KMCP_ERROR_SEVERITY_FATAL;

        case KMCP_ERROR_CATEGORY_INTERNAL:
            // Internal errors are usually fatal
            return KMCP_ERROR_SEVERITY_FATAL;

        default:
            // Most other errors are just errors
            return KMCP_ERROR_SEVERITY_ERROR;
    }
}

/**
 * @brief Convert MCP error code to KMCP error code
 *
 * @param mcp_error The MCP error code to convert
 * @return kmcp_error_t Returns the corresponding KMCP error code
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
 * @brief Create a new error context
 *
 * @param error_code Error code
 * @param file Source file where the error occurred
 * @param line Line number where the error occurred
 * @param function Function where the error occurred
 * @param format Format string for the error message
 * @param ... Additional arguments for the format string
 * @return kmcp_error_context_t* Error context, or NULL if memory allocation failed
 */
kmcp_error_context_t* kmcp_error_context_create(kmcp_error_t error_code,
                                              const char* file,
                                              int line,
                                              const char* function,
                                              const char* format, ...) {
    va_list args;
    va_start(args, format);
    kmcp_error_context_t* context = kmcp_error_context_create_va(error_code, file, line, function, format, args);
    va_end(args);
    return context;
}

/**
 * @brief Create a new error context with a va_list
 *
 * @param error_code Error code
 * @param file Source file where the error occurred
 * @param line Line number where the error occurred
 * @param function Function where the error occurred
 * @param format Format string for the error message
 * @param args Variable arguments list
 * @return kmcp_error_context_t* Error context, or NULL if memory allocation failed
 */
kmcp_error_context_t* kmcp_error_context_create_va(kmcp_error_t error_code,
                                                 const char* file,
                                                 int line,
                                                 const char* function,
                                                 const char* format,
                                                 va_list args) {
    // Allocate memory for the error context
    kmcp_error_context_t* context = (kmcp_error_context_t*)malloc(sizeof(kmcp_error_context_t));
    if (!context) {
        return NULL;
    }

    // Initialize the error context
    context->error_code = error_code;
    context->category = kmcp_error_get_category(error_code);
    context->severity = kmcp_error_get_severity(error_code);
    context->file = file ? file : "unknown";
    context->line = line;
    context->function = function ? function : "unknown";
    context->next = NULL;

    // Format the error message
    vsnprintf(context->message, KMCP_ERROR_CONTEXT_MAX_LENGTH, format, args);

    return context;
}

/**
 * @brief Free an error context
 *
 * @param context Error context to free
 */
void kmcp_error_context_free(kmcp_error_context_t* context) {
    if (!context) {
        return;
    }

    // Free all nested error contexts
    kmcp_error_context_t* next = context->next;
    while (next) {
        kmcp_error_context_t* current = next;
        next = current->next;
        free(current);
    }

    // Free the error context itself
    free(context);
}

/**
 * @brief Add a nested error to an error context
 *
 * @param context Error context to add the nested error to
 * @param nested_context Nested error context to add
 * @return kmcp_error_context_t* The original error context
 */
kmcp_error_context_t* kmcp_error_context_add_nested(kmcp_error_context_t* context,
                                                  kmcp_error_context_t* nested_context) {
    if (!context || !nested_context) {
        return context;
    }

    // Find the last error context in the chain
    kmcp_error_context_t* last = context;
    while (last->next) {
        last = last->next;
    }

    // Add the nested error context to the chain
    last->next = nested_context;

    return context;
}

/**
 * @brief Format an error context as a string
 *
 * @param context Error context to format
 * @param buffer Buffer to store the formatted string
 * @param buffer_size Size of the buffer
 * @return size_t Number of characters written to the buffer (excluding the null terminator)
 */
size_t kmcp_error_context_format(const kmcp_error_context_t* context,
                               char* buffer,
                               size_t buffer_size) {
    if (!context || !buffer || buffer_size == 0) {
        return 0;
    }

    // Format the error context
    int written = snprintf(buffer, buffer_size,
                          "Error %d (%s) in %s:%d [%s]: %s",
                          context->error_code,
                          kmcp_error_message(context->error_code),
                          context->file,
                          context->line,
                          context->function,
                          context->message);

    if (written < 0 || (size_t)written >= buffer_size) {
        // Error or truncation occurred
        return buffer_size - 1;
    }

    // Format nested error contexts
    size_t remaining = buffer_size - written;
    char* current_pos = buffer + written;
    const kmcp_error_context_t* current = context->next;
    int depth = 1;

    while (current && remaining > 0) {
        // Add a separator
        int separator_len = snprintf(current_pos, remaining, "\n%*sCaused by: ", depth * 2, "");
        if (separator_len < 0 || (size_t)separator_len >= remaining) {
            break;
        }

        current_pos += separator_len;
        remaining -= separator_len;

        // Add the nested error context
        int nested_len = snprintf(current_pos, remaining,
                                 "Error %d (%s) in %s:%d [%s]: %s",
                                 current->error_code,
                                 kmcp_error_message(current->error_code),
                                 current->file,
                                 current->line,
                                 current->function,
                                 current->message);

        if (nested_len < 0 || (size_t)nested_len >= remaining) {
            break;
        }

        current_pos += nested_len;
        remaining -= nested_len;
        current = current->next;
        depth++;
    }

    return buffer_size - remaining;
}

/**
 * @brief Log an error context
 *
 * @param context Error context to log
 */
void kmcp_error_context_log(const kmcp_error_context_t* context) {
    if (!context) {
        return;
    }

    // Format the error context
    char buffer[4096]; // Large buffer to accommodate nested errors
    kmcp_error_context_format(context, buffer, sizeof(buffer));

    // Log the error based on severity
    switch (context->severity) {
        case KMCP_ERROR_SEVERITY_INFO:
            mcp_log_info("%s", buffer);
            break;
        case KMCP_ERROR_SEVERITY_WARNING:
            mcp_log_warn("%s", buffer);
            break;
        case KMCP_ERROR_SEVERITY_ERROR:
            mcp_log_error("%s", buffer);
            break;
        case KMCP_ERROR_SEVERITY_FATAL:
            mcp_log_error("%s", buffer); // Using error since fatal may not exist
            break;
        default:
            mcp_log_error("%s", buffer);
            break;
    }
}

/**
 * @brief Log an error with the given error code and message
 *
 * @param error_code Error code
 * @param file Source file where the error occurred
 * @param line Line number where the error occurred
 * @param function Function where the error occurred
 * @param format Format string for the error message
 * @param ... Additional arguments for the format string
 * @return kmcp_error_t The same error code that was passed in
 */
kmcp_error_t kmcp_error_log_ex(kmcp_error_t error_code,
                             const char* file,
                             int line,
                             const char* function,
                             const char* format, ...) {
    if (error_code == KMCP_SUCCESS) {
        return KMCP_SUCCESS;
    }

    // Create an error context
    va_list args;
    va_start(args, format);
    kmcp_error_context_t* context = kmcp_error_context_create_va(error_code, file, line, function, format, args);
    va_end(args);

    if (context) {
        // Log the error context
        kmcp_error_context_log(context);

        // Free the error context
        kmcp_error_context_free(context);
    } else {
        // Fallback if context creation fails
        char message[1024];
        va_list fallback_args;
        va_start(fallback_args, format);
        vsnprintf(message, sizeof(message), format, fallback_args);
        va_end(fallback_args);

        mcp_log_error("%s:%d [%s] %s: %s",
                      file ? file : "unknown",
                      line,
                      function ? function : "unknown",
                      message,
                      kmcp_error_message(error_code));
    }

    return error_code;
}

/**
 * @brief Backward compatibility function for kmcp_error_log
 *
 * @param error_code Error code
 * @param format Format string for the error message
 * @param ... Additional arguments for the format string
 * @return kmcp_error_t The same error code that was passed in
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
