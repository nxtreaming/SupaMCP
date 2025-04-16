#include "internal/server_internal.h"
#include "mcp_performance_collector.h"
#include "mcp_json_rpc.h"
#include "mcp_log.h"
#include "mcp_json.h"
#include "mcp_server.h"
#include <string.h>
#include <stdlib.h>

/**
 * @internal
 * @brief Handles the 'get_performance_metrics' request.
 *
 * Returns the current performance metrics as a JSON object.
 */
char* handle_get_performance_metrics_request(mcp_server_t* server, mcp_arena_t* arena,
                                           const mcp_request_t* request,
                                           const mcp_auth_context_t* auth_context,
                                           int* error_code) {
    PROFILE_START("handle_get_performance_metrics");

    // Basic parameter validation
    if (server == NULL || request == NULL || error_code == NULL) {
        if (error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        PROFILE_END("handle_get_performance_metrics");
        return NULL;
    }

    // Unused parameters
    (void)arena;
    (void)auth_context;

    *error_code = MCP_ERROR_NONE;

    // Get performance metrics as JSON
    char metrics_json[4096];
    int result = mcp_performance_get_metrics_json(metrics_json, sizeof(metrics_json));
    if (result < 0) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        PROFILE_END("handle_get_performance_metrics");
        return create_error_response(request->id, *error_code, "Failed to get performance metrics");
    }

    // Parse the metrics JSON into a JSON object
    mcp_json_t* metrics_obj = mcp_json_parse(metrics_json);
    if (!metrics_obj) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        PROFILE_END("handle_get_performance_metrics");
        return create_error_response(request->id, *error_code, "Failed to parse performance metrics");
    }

    // Create a success response with the metrics object
    char* metrics_str = mcp_json_stringify(metrics_obj);
    mcp_json_destroy(metrics_obj);

    if (!metrics_str) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        PROFILE_END("handle_get_performance_metrics");
        return create_error_response(request->id, *error_code, "Failed to stringify performance metrics");
    }

    char* response = create_success_response(request->id, metrics_str);
    // Note: create_success_response takes ownership of metrics_str

    PROFILE_END("handle_get_performance_metrics");
    return response;
}

/**
 * @internal
 * @brief Handles the 'reset_performance_metrics' request.
 *
 * Resets all performance metrics to their initial values.
 */
char* handle_reset_performance_metrics_request(mcp_server_t* server, mcp_arena_t* arena,
                                             const mcp_request_t* request,
                                             const mcp_auth_context_t* auth_context,
                                             int* error_code) {
    PROFILE_START("handle_reset_performance_metrics");

    // Basic parameter validation
    if (server == NULL || request == NULL || error_code == NULL) {
        if (error_code) *error_code = MCP_ERROR_INVALID_PARAMS;
        PROFILE_END("handle_reset_performance_metrics");
        return NULL;
    }

    // Unused parameters
    (void)arena;
    (void)auth_context;

    *error_code = MCP_ERROR_NONE;

    // Reset performance metrics
    mcp_performance_metrics_reset();

    // Create a simple success response
    mcp_json_t* result = mcp_json_object_create();
    if (!result) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        PROFILE_END("handle_reset_performance_metrics");
        return create_error_response(request->id, *error_code, "Failed to create response object");
    }

    mcp_json_t* success = mcp_json_boolean_create(true);
    if (!success) {
        mcp_json_destroy(result);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        PROFILE_END("handle_reset_performance_metrics");
        return create_error_response(request->id, *error_code, "Failed to create response value");
    }

    if (mcp_json_object_set_property(result, "success", success) != 0) {
        mcp_json_destroy(result);
        mcp_json_destroy(success);
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        PROFILE_END("handle_reset_performance_metrics");
        return create_error_response(request->id, *error_code, "Failed to set response property");
    }

    char* result_str = mcp_json_stringify(result);
    mcp_json_destroy(result);

    if (!result_str) {
        *error_code = MCP_ERROR_INTERNAL_ERROR;
        PROFILE_END("handle_reset_performance_metrics");
        return create_error_response(request->id, *error_code, "Failed to stringify response");
    }

    char* response = create_success_response(request->id, result_str);
    // Note: create_success_response takes ownership of result_str

    PROFILE_END("handle_reset_performance_metrics");
    return response;
}
