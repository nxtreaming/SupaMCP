#include "internal/json_internal.h"
#include "mcp_json_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// --- JSON Stringification Implementation ---
// Stringification uses malloc/realloc for the output buffer, not arena.

// Static forward declarations for internal helper functions within this file
static int stringify_value(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity);
static int ensure_output_capacity(char** output, size_t* output_size, size_t* output_capacity, size_t additional);
static int append_string(char** output, size_t* output_size, size_t* output_capacity, const char* string);
static int stringify_string(const char* string, char** output, size_t* output_size, size_t* output_capacity);
static int stringify_object(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity);
static int stringify_array(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity);


static int ensure_output_capacity(char** output, size_t* output_size, size_t* output_capacity, size_t additional) {
    if (*output_size + additional > *output_capacity) {
        size_t new_capacity = *output_capacity == 0 ? 256 : *output_capacity * 2;
        while (new_capacity < *output_size + additional) {
            new_capacity *= 2;
        }
        char* new_output = (char*)realloc(*output, new_capacity);
        if (new_output == NULL) {
            return -1;
        }
        *output = new_output;
        *output_capacity = new_capacity;
    }
    return 0;
}

static int append_string(char** output, size_t* output_size, size_t* output_capacity, const char* string) {
    size_t length = strlen(string);
    if (ensure_output_capacity(output, output_size, output_capacity, length) != 0) {
        return -1;
    }
    memcpy(*output + *output_size, string, length);
    *output_size += length;
    return 0;
}

static int stringify_string(const char* string, char** output, size_t* output_size, size_t* output_capacity) {
    // 1. Calculate required size for the escaped string content (excluding quotes)
    int required_escaped_len = mcp_json_escape_string(string, NULL, 0);
    if (required_escaped_len < 0) {
        return -1; // Error during calculation
    }
    // required_escaped_len includes the null terminator, content length is required_escaped_len - 1
    size_t escaped_content_len = (size_t)required_escaped_len - 1;
    size_t total_needed_space = 2 + escaped_content_len; // 2 for quotes + content length

    // 2. Ensure output buffer has enough capacity
    if (ensure_output_capacity(output, output_size, output_capacity, total_needed_space) != 0) {
        return -1; // Failed to allocate memory
    }

    // 3. Append opening quote
    (*output)[(*output_size)++] = '"';

    // 4. Append the escaped string content directly into the buffer
    // We pass the remaining capacity, which we know is sufficient.
    // The function will write escaped_content_len bytes + a null terminator.
    int written_len = mcp_json_escape_string(string, *output + *output_size, *output_capacity - *output_size);
    if (written_len < 0 || (size_t)written_len != (size_t)required_escaped_len) {
        // This shouldn't happen if ensure_output_capacity worked and calculation was correct
        return -1; // Error during escaping/writing
    }

    // 5. Update the output size by the length of the *content* written
    *output_size += escaped_content_len;

    // 6. Append closing quote
    (*output)[(*output_size)++] = '"';

    return 0;
}


static int stringify_object(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity) {
    if (append_string(output, output_size, output_capacity, "{") != 0) return -1;
    bool first_property = true;
    const mcp_json_object_table_t* table = &json->object;
    for (size_t i = 0; i < table->capacity; i++) {
        mcp_json_object_entry_t* entry = table->buckets[i];
        while (entry != NULL) {
            if (!first_property) {
                if (append_string(output, output_size, output_capacity, ",") != 0) return -1;
            } else {
                first_property = false;
            }
            if (stringify_string(entry->name, output, output_size, output_capacity) != 0) return -1;
            if (append_string(output, output_size, output_capacity, ":") != 0) return -1;
            if (stringify_value(entry->value, output, output_size, output_capacity) != 0) return -1;
            entry = entry->next;
        }
    }
    if (append_string(output, output_size, output_capacity, "}") != 0) return -1;
    return 0;
}

static int stringify_array(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity) {
    if (append_string(output, output_size, output_capacity, "[") != 0) return -1;
    for (size_t i = 0; i < json->array.count; i++) {
        if (i > 0) {
            if (append_string(output, output_size, output_capacity, ",") != 0) return -1;
        }
        if (stringify_value(json->array.items[i], output, output_size, output_capacity) != 0) return -1;
    }
    if (append_string(output, output_size, output_capacity, "]") != 0) return -1;
    return 0;
}

// Main recursive stringification function
int stringify_value(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity) {
    if (json == NULL) {
        return append_string(output, output_size, output_capacity, "null");
    }
    switch (json->type) {
        case MCP_JSON_NULL:    return append_string(output, output_size, output_capacity, "null");
        case MCP_JSON_BOOLEAN: return append_string(output, output_size, output_capacity, json->boolean_value ? "true" : "false");
        case MCP_JSON_NUMBER: {
            char buffer[32];
            // Use snprintf for robust number formatting, ensuring locale independence if possible
            // %.17g is generally good for preserving double precision
            int len = snprintf(buffer, sizeof(buffer), "%.17g", json->number_value);
            if (len < 0 || (size_t)len >= sizeof(buffer)) return -1; // Encoding error or buffer too small
            return append_string(output, output_size, output_capacity, buffer);
        }
        case MCP_JSON_STRING:  return stringify_string(json->string_value, output, output_size, output_capacity);
        case MCP_JSON_ARRAY:   return stringify_array(json, output, output_size, output_capacity);
        case MCP_JSON_OBJECT:  return stringify_object(json, output, output_size, output_capacity);
        default:               return -1; // Should not happen
    }
}

// Public API function for stringification
char* mcp_json_stringify(const mcp_json_t* json) {
    char* output = NULL;
    size_t output_size = 0;
    size_t output_capacity = 0;
    if (stringify_value(json, &output, &output_size, &output_capacity) != 0) {
        free(output);
        return NULL;
    }
    // Add null terminator
    if (ensure_output_capacity(&output, &output_size, &output_capacity, 1) != 0) {
        free(output);
        return NULL;
    }
    output[output_size] = '\0';
    // Optionally, shrink buffer to fit: realloc(output, output_size + 1);
    return output; // Caller must free this string
}
