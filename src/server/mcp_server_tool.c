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
            mcp_tool_free(server->tools[i]);
            
            // Shift remaining tools left
            for (size_t j = i; j < server->tool_count - 1; j++) {
                server->tools[j] = server->tools[j + 1];
            }
            
            server->tool_count--;
            return 0;
        }
    }
    return -1;
}
