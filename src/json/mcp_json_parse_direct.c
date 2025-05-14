#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mcp_json_rpc.h"
#include "mcp_log.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"

/**
 * Parse resources from a JSON-RPC response without using the thread-local arena.
 * This is a direct implementation that uses a simple JSON parsing approach.
 */
int mcp_json_parse_resources_direct(
    const char* json_str,
    mcp_resource_t*** resources,
    size_t* count
) {
    if (json_str == NULL || resources == NULL || count == NULL) {
        return -1;
    }

    // Initialize output parameters
    *resources = NULL;
    *count = 0;

    // Find the resources array in the JSON
    const char* resources_start = strstr(json_str, "\"resources\"");
    if (resources_start == NULL) {
        mcp_log_error("Failed to find resources array in JSON");
        return -1;
    }

    // Find the start of the array
    const char* array_start = strchr(resources_start, '[');
    if (array_start == NULL) {
        mcp_log_error("Failed to find start of resources array");
        return -1;
    }

    // Count the number of resources (count the number of "uri" occurrences)
    size_t resource_count = 0;
    const char* uri_pos = array_start;
    while ((uri_pos = strstr(uri_pos + 1, "\"uri\"")) != NULL) {
        resource_count++;
    }

    if (resource_count == 0) {
        // No resources found, but this is valid
        return 0;
    }

    // Allocate the resources array
    *resources = (mcp_resource_t**)calloc(resource_count, sizeof(mcp_resource_t*));
    if (*resources == NULL) {
        mcp_log_error("Failed to allocate resources array");
        return -1;
    }

    *count = resource_count;

    // Parse each resource
    const char* pos = array_start;
    for (size_t i = 0; i < resource_count; i++) {
        // Find the start of the resource object
        pos = strchr(pos, '{');
        if (pos == NULL) {
            mcp_log_error("Failed to find resource object %zu", i);
            goto parse_error;
        }

        // Extract URI (required)
        const char* uri_start = strstr(pos, "\"uri\"");
        if (uri_start == NULL) {
            mcp_log_error("Failed to find URI for resource %zu", i);
            goto parse_error;
        }

        uri_start = strchr(uri_start, ':');
        if (uri_start == NULL) {
            mcp_log_error("Failed to find URI value for resource %zu", i);
            goto parse_error;
        }

        uri_start = strchr(uri_start, '"');
        if (uri_start == NULL) {
            mcp_log_error("Failed to find URI string for resource %zu", i);
            goto parse_error;
        }
        uri_start++; // Skip the opening quote

        const char* uri_end = strchr(uri_start, '"');
        if (uri_end == NULL) {
            mcp_log_error("Failed to find end of URI string for resource %zu", i);
            goto parse_error;
        }

        size_t uri_len = uri_end - uri_start;
        char* uri = (char*)malloc(uri_len + 1);
        if (uri == NULL) {
            mcp_log_error("Failed to allocate URI string for resource %zu", i);
            goto parse_error;
        }
        memcpy(uri, uri_start, uri_len);
        uri[uri_len] = '\0';

        // Extract name (optional)
        char* name = NULL;
        const char* name_start = strstr(pos, "\"name\"");
        if (name_start != NULL && name_start < strchr(pos, '}')) {
            name_start = strchr(name_start, ':');
            if (name_start != NULL) {
                name_start = strchr(name_start, '"');
                if (name_start != NULL) {
                    name_start++; // Skip the opening quote
                    const char* name_end = strchr(name_start, '"');
                    if (name_end != NULL) {
                        size_t name_len = name_end - name_start;
                        name = (char*)malloc(name_len + 1);
                        if (name != NULL) {
                            memcpy(name, name_start, name_len);
                            name[name_len] = '\0';
                        }
                    }
                }
            }
        }

        // Extract MIME type (optional)
        char* mime_type = NULL;
        const char* mime_start = strstr(pos, "\"mimeType\"");
        if (mime_start != NULL && mime_start < strchr(pos, '}')) {
            mime_start = strchr(mime_start, ':');
            if (mime_start != NULL) {
                mime_start = strchr(mime_start, '"');
                if (mime_start != NULL) {
                    mime_start++; // Skip the opening quote
                    const char* mime_end = strchr(mime_start, '"');
                    if (mime_end != NULL) {
                        size_t mime_len = mime_end - mime_start;
                        mime_type = (char*)malloc(mime_len + 1);
                        if (mime_type != NULL) {
                            memcpy(mime_type, mime_start, mime_len);
                            mime_type[mime_len] = '\0';
                        }
                    }
                }
            }
        }

        // Extract description (optional)
        char* description = NULL;
        const char* desc_start = strstr(pos, "\"description\"");
        if (desc_start != NULL && desc_start < strchr(pos, '}')) {
            desc_start = strchr(desc_start, ':');
            if (desc_start != NULL) {
                desc_start = strchr(desc_start, '"');
                if (desc_start != NULL) {
                    desc_start++; // Skip the opening quote
                    const char* desc_end = strchr(desc_start, '"');
                    if (desc_end != NULL) {
                        size_t desc_len = desc_end - desc_start;
                        description = (char*)malloc(desc_len + 1);
                        if (description != NULL) {
                            memcpy(description, desc_start, desc_len);
                            description[desc_len] = '\0';
                        }
                    }
                }
            }
        }

        // Create the resource
        (*resources)[i] = mcp_resource_create(uri, name, mime_type, description);

        // Free the temporary strings
        free(uri);
        free(name);
        free(mime_type);
        free(description);

        if ((*resources)[i] == NULL) {
            mcp_log_error("Failed to create resource %zu", i);
            goto parse_error;
        }

        // Move to the next resource
        pos = strchr(pos, '}');
        if (pos == NULL) {
            mcp_log_error("Failed to find end of resource object %zu", i);
            goto parse_error;
        }
    }

    return 0;

parse_error:
    // Cleanup partially created resources
    if (*resources) {
        for (size_t j = 0; j < *count; j++) {
            if ((*resources)[j] != NULL) {
                mcp_resource_free((*resources)[j]);
            }
        }
        free(*resources);
        *resources = NULL;
    }
    *count = 0;
    return -1;
}
