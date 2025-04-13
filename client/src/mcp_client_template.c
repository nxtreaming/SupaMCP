#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mcp_client.h"
#include "mcp_template.h"
#include "mcp_log.h"

/**
 * Expand a resource template with parameters
 */
int mcp_client_expand_template(
    mcp_client_t* client,
    const char* template_uri,
    const char* params_json,
    char** expanded_uri
) {
    if (client == NULL || template_uri == NULL || params_json == NULL || expanded_uri == NULL) {
        return -1;
    }

    // Initialize output parameter
    *expanded_uri = NULL;

    // Parse parameters JSON
    mcp_json_t* params = mcp_json_parse(params_json);
    if (params == NULL) {
        mcp_log_error("Failed to parse template parameters JSON");
        return -1;
    }

    // Expand the template
    char* uri = mcp_template_expand(template_uri, params);
    mcp_json_destroy(params);

    if (uri == NULL) {
        mcp_log_error("Failed to expand template '%s' with parameters '%s'", template_uri, params_json);
        return -1;
    }

    *expanded_uri = uri;
    return 0;
}

/**
 * Read a resource using a template and parameters
 */
int mcp_client_read_resource_with_template(
    mcp_client_t* client,
    const char* template_uri,
    const char* params_json,
    mcp_content_item_t*** content,
    size_t* count
) {
    if (client == NULL || template_uri == NULL || params_json == NULL || content == NULL || count == NULL) {
        return -1;
    }

    // Initialize output parameters
    *content = NULL;
    *count = 0;

    // Expand the template
    char* expanded_uri = NULL;
    if (mcp_client_expand_template(client, template_uri, params_json, &expanded_uri) != 0) {
        return -1;
    }

    // Read the resource using the expanded URI
    int result = mcp_client_read_resource(client, expanded_uri, content, count);
    free(expanded_uri);

    return result;
}
