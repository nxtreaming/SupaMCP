#include "mcp_json_utils.h"
#include "mcp_json.h"
#include <stdlib.h>
#include <string.h>

bool mcp_json_is_string(const mcp_json_t* json) {
    return json != NULL && mcp_json_get_type(json) == MCP_JSON_STRING;
}

const char* mcp_json_string_value(const mcp_json_t* json) {
    const char* value = NULL;
    if (mcp_json_is_string(json) && mcp_json_get_string(json, &value) == 0) {
        return value;
    }
    return NULL;
}

bool mcp_json_is_number(const mcp_json_t* json) {
    return json != NULL && mcp_json_get_type(json) == MCP_JSON_NUMBER;
}

double mcp_json_number_value(const mcp_json_t* json) {
    double value = 0.0;
    if (mcp_json_is_number(json) && mcp_json_get_number(json, &value) == 0) {
        return value;
    }
    return 0.0;
}

bool mcp_json_is_boolean(const mcp_json_t* json) {
    return json != NULL && mcp_json_get_type(json) == MCP_JSON_BOOLEAN;
}

bool mcp_json_boolean_value(const mcp_json_t* json) {
    bool value = false;
    if (mcp_json_is_boolean(json) && mcp_json_get_boolean(json, &value) == 0) {
        return value;
    }
    return false;
}

bool mcp_json_is_null(const mcp_json_t* json) {
    return json == NULL || mcp_json_get_type(json) == MCP_JSON_NULL;
}

bool mcp_json_is_array(const mcp_json_t* json) {
    return json != NULL && mcp_json_get_type(json) == MCP_JSON_ARRAY;
}

bool mcp_json_is_object(const mcp_json_t* json) {
    return json != NULL && mcp_json_get_type(json) == MCP_JSON_OBJECT;
}

size_t mcp_json_object_size(const mcp_json_t* json) {
    if (!mcp_json_is_object(json)) {
        return 0;
    }
    
    char** names = NULL;
    size_t count = 0;
    if (mcp_json_object_get_property_names(json, &names, &count) != 0) {
        return 0;
    }
    
    // Free the property names
    for (size_t i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
    
    return count;
}

int mcp_json_object_get_at(const mcp_json_t* json, size_t index, const char** name, mcp_json_t** value) {
    if (!mcp_json_is_object(json) || name == NULL || value == NULL) {
        return -1;
    }
    
    char** names = NULL;
    size_t count = 0;
    if (mcp_json_object_get_property_names(json, &names, &count) != 0) {
        return -1;
    }
    
    if (index >= count) {
        // Free the property names
        for (size_t i = 0; i < count; i++) {
            free(names[i]);
        }
        free(names);
        return -1;
    }
    
    *name = names[index];
    *value = mcp_json_object_get_property(json, names[index]);
    
    // Free the property names (except the one we're returning)
    for (size_t i = 0; i < count; i++) {
        if (i != index) {
            free(names[i]);
        }
    }
    free(names);
    
    return 0;
}
