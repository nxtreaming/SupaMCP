#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mcp_client.h"
#include "mcp_template.h"
#include "mcp_log.h"
#include "mcp_memory_pool.h"
#include "mcp_thread_cache.h"
#include "mcp_arena.h"
#include "mcp_string_utils.h"

// Performance optimization constants
#define MAX_TEMPLATE_SIZE 4096
#define MAX_PARAMS_SIZE 8192

/**
 * @brief Expand a resource template with parameters
 *
 * This optimized function efficiently expands a URI template by replacing
 * placeholders with values from the provided JSON parameters.
 *
 * @param client The MCP client instance
 * @param template_uri The URI template to expand
 * @param params_json The JSON parameters for template expansion
 * @param expanded_uri Pointer to store the expanded URI
 * @return 0 on success, -1 on failure
 */
int mcp_client_expand_template(
    mcp_client_t* client,
    const char* template_uri,
    const char* params_json,
    char** expanded_uri
) {
    if (client == NULL || template_uri == NULL || params_json == NULL || expanded_uri == NULL) {
        mcp_log_error("Invalid parameters for template expansion");
        return -1;
    }

    // Check template and params size for reasonable limits
    size_t template_len = strlen(template_uri);
    size_t params_len = strlen(params_json);

    if (template_len == 0 || template_len > MAX_TEMPLATE_SIZE) {
        mcp_log_error("Template URI size invalid: %zu bytes", template_len);
        return -1;
    }

    if (params_len == 0 || params_len > MAX_PARAMS_SIZE) {
        mcp_log_error("Parameters JSON size invalid: %zu bytes", params_len);
        return -1;
    }

    // Initialize output parameter
    *expanded_uri = NULL;

    // Log template expansion request at debug level
    mcp_log_debug("Expanding template: %s with params: %s",
                 template_uri,
                 params_len > 100 ? "[large params]" : params_json);

    // Parse parameters JSON
    mcp_json_t* params = mcp_json_parse(params_json);
    if (params == NULL) {
        mcp_log_error("Failed to parse template parameters JSON: %s", params_json);
        return -1;
    }

    // Expand the template using standard expansion
    char* uri = mcp_template_expand(template_uri, params);

    // Clean up JSON parameters
    mcp_json_destroy(params);

    // Handle expansion failure
    if (uri == NULL) {
        mcp_log_error("Failed to expand template '%s'", template_uri);
        return -1;
    }

    // Success - set output parameter
    *expanded_uri = uri;

    // Log successful expansion at debug level
    mcp_log_debug("Template expanded to: %s", uri);

    return 0;
}

/**
 * @brief Read a resource using a template and parameters
 *
 * This optimized function efficiently reads a resource by first expanding a URI template
 * with the provided parameters, then fetching the resource at the expanded URI.
 *
 * @param client The MCP client instance
 * @param template_uri The URI template to expand
 * @param params_json The JSON parameters for template expansion
 * @param content Pointer to store the content items array
 * @param count Pointer to store the number of content items
 * @return 0 on success, -1 on failure
 */
int mcp_client_read_resource_with_template(
    mcp_client_t* client,
    const char* template_uri,
    const char* params_json,
    mcp_content_item_t*** content,
    size_t* count
) {
    if (client == NULL || template_uri == NULL || params_json == NULL ||
        content == NULL || count == NULL) {
        mcp_log_error("Invalid parameters for reading resource with template");
        return -1;
    }

    // Initialize output parameters
    *content = NULL;
    *count = 0;

    // Log the resource request at debug level
    mcp_log_debug("Reading resource with template: %s", template_uri);

    // Expand the template
    char* expanded_uri = NULL;
    int expand_result = mcp_client_expand_template(client, template_uri, params_json, &expanded_uri);

    if (expand_result != 0 || expanded_uri == NULL) {
        mcp_log_error("Failed to expand template for resource: %s", template_uri);
        return -1;
    }

    // Read the resource using the expanded URI
    mcp_log_debug("Reading resource at expanded URI: %s", expanded_uri);
    int result = mcp_client_read_resource(client, expanded_uri, content, count);

    // Log the result
    if (result == 0) {
        mcp_log_debug("Successfully read resource: %zu content items", *count);
    } else {
        mcp_log_error("Failed to read resource at: %s", expanded_uri);
    }

    // Clean up the expanded URI
    free(expanded_uri);

    return result;
}

/**
 * @brief Read multiple resources using the same template with different parameters
 *
 * This optimized function efficiently reads multiple resources by expanding the same
 * template with different parameter sets, then fetching each resource.
 *
 * @param client The MCP client instance
 * @param template_uri The URI template to expand
 * @param params_json_array Array of JSON parameter strings
 * @param params_count Number of parameter sets in the array
 * @param content_array Pointer to store array of content item arrays
 * @param count_array Pointer to store array of content item counts
 * @param result_array Pointer to store array of result codes
 * @return Number of successful resource reads, or -1 on critical failure
 */
int mcp_client_read_resources_with_template_batch(
    mcp_client_t* client,
    const char* template_uri,
    const char** params_json_array,
    size_t params_count,
    mcp_content_item_t**** content_array,
    size_t** count_array,
    int** result_array
) {
    if (client == NULL || template_uri == NULL || params_json_array == NULL ||
        params_count == 0 || content_array == NULL || count_array == NULL ||
        result_array == NULL) {
        mcp_log_error("Invalid parameters for batch resource template reading");
        return -1;
    }

    // Allocate arrays for results using thread cache for better performance
    *content_array = (mcp_content_item_t***)mcp_thread_cache_alloc(
        params_count * sizeof(mcp_content_item_t**));
    *count_array = (size_t*)mcp_thread_cache_alloc(params_count * sizeof(size_t));
    *result_array = (int*)mcp_thread_cache_alloc(params_count * sizeof(int));

    if (*content_array == NULL || *count_array == NULL || *result_array == NULL) {
        // Clean up any successful allocations
        if (*content_array) mcp_thread_cache_free(*content_array,
                                                params_count * sizeof(mcp_content_item_t**));
        if (*count_array) mcp_thread_cache_free(*count_array,
                                              params_count * sizeof(size_t));
        if (*result_array) mcp_thread_cache_free(*result_array,
                                               params_count * sizeof(int));

        *content_array = NULL;
        *count_array = NULL;
        *result_array = NULL;

        mcp_log_error("Failed to allocate memory for batch resource reading");
        return -1;
    }

    // Initialize arrays
    memset(*content_array, 0, params_count * sizeof(mcp_content_item_t**));
    memset(*count_array, 0, params_count * sizeof(size_t));
    for (size_t i = 0; i < params_count; i++) {
        (*result_array)[i] = -1; // Initialize all results to failure
    }

    // Log the batch request
    mcp_log_debug("Reading %zu resources with template: %s", params_count, template_uri);

    // Process each parameter set
    int success_count = 0;
    for (size_t i = 0; i < params_count; i++) {
        // Skip NULL parameter sets
        if (params_json_array[i] == NULL) {
            mcp_log_warn("Skipping NULL parameter set at index %zu", i);
            continue;
        }

        // Read resource with this parameter set
        (*result_array)[i] = mcp_client_read_resource_with_template(
            client,
            template_uri,
            params_json_array[i],
            &((*content_array)[i]),
            &((*count_array)[i])
        );

        // Count successful reads
        if ((*result_array)[i] == 0) {
            success_count++;
        }
    }

    mcp_log_debug("Batch resource reading complete: %d/%zu successful",
                 success_count, params_count);

    return success_count;
}

/**
 * @brief Free resources allocated by mcp_client_read_resources_with_template_batch
 *
 * This function properly cleans up all memory allocated during a batch resource read
 * operation, including content items, arrays, and other resources.
 *
 * @param content_array Array of content item arrays
 * @param count_array Array of content item counts
 * @param result_array Array of result codes
 * @param params_count Number of parameter sets processed
 */
void mcp_client_free_batch_resources(
    mcp_content_item_t*** content_array,
    size_t* count_array,
    int* result_array,
    size_t params_count
) {
    if (content_array == NULL || count_array == NULL || result_array == NULL || params_count == 0) {
        mcp_log_warn("Invalid parameters for freeing batch resources");
        return;
    }

    // Free each content item array
    for (size_t i = 0; i < params_count; i++) {
        if (content_array[i] != NULL) {
            // Free each content item in the array
            for (size_t j = 0; j < count_array[i]; j++) {
                if (content_array[i][j] != NULL) {
                    mcp_content_item_free(content_array[i][j]);
                }
            }

            // Free the array of content item pointers using thread cache
            mcp_thread_cache_free(content_array[i],
                                 count_array[i] * sizeof(mcp_content_item_t*));
        }
    }

    // Free the arrays themselves using thread cache
    mcp_thread_cache_free(content_array, params_count * sizeof(mcp_content_item_t**));
    mcp_thread_cache_free(count_array, params_count * sizeof(size_t));
    mcp_thread_cache_free(result_array, params_count * sizeof(int));

    mcp_log_debug("Freed resources for %zu batch items", params_count);
}
