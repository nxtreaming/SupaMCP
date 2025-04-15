#include "mcp_json_utils.h"
#include "mcp_json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @brief Format a C string as a JSON string value.
 * 
 * This function takes a C string and formats it as a JSON string value,
 * properly escaping special characters and adding quotes.
 * 
 * @param str The C string to format
 * @return A newly allocated JSON string or NULL on error (must be freed by the caller)
 */
char* mcp_json_format_string(const char* str) {
    if (str == NULL) {
        return NULL;
    }
    
    // Create a JSON string node
    mcp_json_t* json_str = mcp_json_string_create(str);
    if (json_str == NULL) {
        return NULL;
    }
    
    // Stringify the JSON node
    char* result = mcp_json_stringify(json_str);
    
    // Clean up
    mcp_json_destroy(json_str);
    
    return result;
}
