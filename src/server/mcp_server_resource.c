#include "internal/server_internal.h"
#include <string.h>

const mcp_resource_t* mcp_server_find_resource(mcp_server_t* server, const char* uri) {
    if (!server || !uri) return NULL;
    
    for (size_t i = 0; i < server->resource_count; i++) {
        if (strcmp(server->resources[i]->uri, uri) == 0) {
            return server->resources[i];
        }
    }
    return NULL;
}

int mcp_server_remove_resource(mcp_server_t* server, const char* uri) {
    if (!server || !uri) return -1;
    
    for (size_t i = 0; i < server->resource_count; i++) {
        if (strcmp(server->resources[i]->uri, uri) == 0) {
            // Free the resource
            mcp_resource_free(server->resources[i]); // Free the found resource

            // Move the last element into the freed slot (if it's not the last one)
            if (i < server->resource_count - 1) {
                server->resources[i] = server->resources[server->resource_count - 1];
            }
            server->resources[server->resource_count - 1] = NULL; // Optional: Clear the last pointer
            server->resource_count--; // Decrement count

            // TODO: Add mutex unlock if locking is implemented
            return 0; // Success
        }
    }
    return -1;
}
