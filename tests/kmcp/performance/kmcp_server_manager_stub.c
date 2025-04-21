#include "kmcp_server_manager_stub.h"
#include "kmcp_error.h"
#include "mcp_string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool kmcp_server_manager_has_server(kmcp_server_manager_t* manager, const char* name) {
    // This is a stub implementation for testing
    // In a real implementation, this would check if the server exists in the manager
    // For testing, we'll just return true if the manager and name are not NULL
    return (manager != NULL && name != NULL);
}

kmcp_error_t kmcp_server_manager_add_server(kmcp_server_manager_t* manager, const kmcp_server_config_t* config) {
    // This is a stub implementation for testing
    // In a real implementation, this would add the server to the manager
    // For testing, we'll just return success if the manager and config are not NULL
    if (manager == NULL || config == NULL) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }
    return KMCP_SUCCESS;
}

kmcp_error_t kmcp_server_manager_start_server(kmcp_server_manager_t* manager, const char* name) {
    // This is a stub implementation for testing
    // In a real implementation, this would start the server
    // For testing, we'll just return success if the manager and name are not NULL
    if (manager == NULL || name == NULL) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }
    return KMCP_SUCCESS;
}

bool kmcp_server_manager_is_server_running(kmcp_server_manager_t* manager, const char* name) {
    // This is a stub implementation for testing
    // In a real implementation, this would check if the server is running
    // For testing, we'll just return true if the manager and name are not NULL
    return (manager != NULL && name != NULL);
}

kmcp_error_t kmcp_server_manager_stop_server(kmcp_server_manager_t* manager, const char* name) {
    // This is a stub implementation for testing
    // In a real implementation, this would stop the server
    // For testing, we'll just return success if the manager and name are not NULL
    if (manager == NULL || name == NULL) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }
    return KMCP_SUCCESS;
}

kmcp_error_t kmcp_client_set_manager(kmcp_client_t* client, kmcp_server_manager_t* manager) {
    // This is a stub implementation for testing
    // In a real implementation, this would set the server manager for the client
    // For testing, we'll just return success if the client and manager are not NULL
    if (client == NULL || manager == NULL) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }
    return KMCP_SUCCESS;
}

kmcp_error_t kmcp_client_call_tool_on_server(
    kmcp_client_t* client,
    const char* server_name,
    const char* tool_name,
    const char* params_json,
    char** result_json
) {
    // This is a stub implementation for testing
    // In a real implementation, this would call the tool on the specified server
    // For testing, we'll just return success if all parameters are not NULL
    if (client == NULL || server_name == NULL || tool_name == NULL || params_json == NULL || result_json == NULL) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Allocate a simple result string
    *result_json = mcp_strdup("{\"result\":\"success\"}");
    if (*result_json == NULL) {
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    return KMCP_SUCCESS;
}
