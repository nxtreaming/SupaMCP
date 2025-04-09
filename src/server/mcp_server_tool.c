#include "internal/server_internal.h"
#include <string.h>

const mcp_tool_t* mcp_server_find_tool(mcp_server_t* server, const char* name) {
    if (!server || !name) return NULL;
    
    for (size_t i = 0; i < server->tool_count; i++) {
        if (strcmp(server->tools[i]->name, name) == 0) {
            return server->tools[i];
        }
    }
    return NULL;
}

int mcp_server_remove_tool(mcp_server_t* server, const char* name) {
    if (!server || !name) return -1;
    
    for (size_t i = 0; i < server->tool_count; i++) {
        if (strcmp(server->tools[i]->name, name) == 0) {
            // Free the tool
            mcp_tool_free(server->tools[i]); // Free the found tool

            // Move the last element into the freed slot (if it's not the last one)
            if (i < server->tool_count - 1) {
                server->tools[i] = server->tools[server->tool_count - 1];
            }
            server->tools[server->tool_count - 1] = NULL; // Optional: Clear the last pointer
            server->tool_count--; // Decrement count

            // TODO: Add mutex unlock if locking is implemented
            return 0; // Success
        }
    }
    return -1;
}
