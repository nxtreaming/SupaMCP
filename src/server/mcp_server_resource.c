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
            mcp_resource_free(server->resources[i]);
            
            // Shift remaining resources left
            for (size_t j = i; j < server->resource_count - 1; j++) {
                server->resources[j] = server->resources[j + 1];
            }
            
            server->resource_count--;
            return 0;
        }
    }
    return -1;
}
