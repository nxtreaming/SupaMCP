#include "internal/json_internal.h"
#include "mcp_json_utils.h"
#include "mcp_json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// Stringification uses malloc/realloc for the output buffer, not arena.
// Forward declaration for estimating JSON size
static size_t estimate_json_size(const mcp_json_t* json);

// Optimized buffer capacity management with smarter initial sizing
static int ensure_output_capacity(char** output, size_t* output_size, size_t* output_capacity, size_t additional) {
    if (*output_size + additional > *output_capacity) {
        // More aggressive growth for larger buffers to reduce reallocation frequency
        size_t new_capacity;
        if (*output_capacity == 0) {
            // For initial allocation, try to estimate a good size based on the JSON structure
            new_capacity = 256; // Minimum starting size
        } else if (*output_capacity < 1024) {
            // For small buffers, double the size
            new_capacity = *output_capacity * 2;
        } else if (*output_capacity < 1024 * 1024) {
            // For medium buffers, grow by 50%
            new_capacity = *output_capacity + (*output_capacity / 2);
        } else {
            // For large buffers, grow by 25% to avoid excessive memory usage
            new_capacity = *output_capacity + (*output_capacity / 4);
        }

        // Ensure the new capacity is at least enough for the current request
        while (new_capacity < *output_size + additional) {
            new_capacity = new_capacity + (new_capacity / 2);
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

// Context structure for the stringify object callback
typedef struct {
    char** output;
    size_t* output_size;
    size_t* output_capacity;
    bool first_property;
    int error_code; // To signal errors from callback
} stringify_object_context_t;

int stringify_value(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity);

// Callback function for mcp_hashtable_foreach used in stringify_object
static void stringify_object_callback(const void* key, void* value, void* user_data) {
    stringify_object_context_t* ctx = (stringify_object_context_t*)user_data;
    if (ctx->error_code != 0) return; // Stop if an error already occurred

    const char* name = (const char*)key;
    mcp_json_t* json_value = (mcp_json_t*)value;

    if (!ctx->first_property) {
        if (append_string(ctx->output, ctx->output_size, ctx->output_capacity, ",") != 0) {
            ctx->error_code = -1;
            return;
        }
    } else {
        ctx->first_property = false;
    }

    if (stringify_string(name, ctx->output, ctx->output_size, ctx->output_capacity) != 0) {
        ctx->error_code = -1;
        return;
    }
    if (append_string(ctx->output, ctx->output_size, ctx->output_capacity, ":") != 0) {
        ctx->error_code = -1;
        return;
    }
    if (stringify_value(json_value, ctx->output, ctx->output_size, ctx->output_capacity) != 0) {
        ctx->error_code = -1;
        return;
    }
}

static int stringify_object(const mcp_json_t* json, char** output, size_t* output_size, size_t* output_capacity) {
    if (json->object_table == NULL) {
        // Handle case where object might be created but table allocation failed
        return append_string(output, output_size, output_capacity, "{}");
    }

    if (append_string(output, output_size, output_capacity, "{") != 0) return -1;

    stringify_object_context_t context = {
        .output = output,
        .output_size = output_size,
        .output_capacity = output_capacity,
        .first_property = true,
        .error_code = 0
    };

    // Iterate using the generic hash table foreach
    mcp_hashtable_foreach(json->object_table, stringify_object_callback, &context);

    // Check if an error occurred during iteration
    if (context.error_code != 0) {
        return -1;
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

// Estimate the size of a JSON value when stringified
// This helps reduce reallocations by providing a better initial buffer size
static size_t estimate_json_size(const mcp_json_t* json) {
    if (json == NULL) {
        return 4; // "null"
    }

    size_t estimate = 0;

    switch (json->type) {
        case MCP_JSON_NULL:
            return 4; // "null"

        case MCP_JSON_BOOLEAN:
            return json->boolean_value ? 4 : 5; // "true" or "false"

        case MCP_JSON_NUMBER:
            // Numbers typically need around 20 chars max for double precision
            return 20;

        case MCP_JSON_STRING: {
            // String length plus quotes, plus some extra for escapes
            size_t str_len = json->string_value ? strlen(json->string_value) : 0;
            // Assume ~10% of characters need escaping (2 chars each)
            return str_len + (str_len / 10) + 2; // +2 for quotes
        }

        case MCP_JSON_ARRAY: {
            // Start with 2 for [ and ]
            estimate = 2;
            // Add commas between items
            if (json->array.count > 0) {
                estimate += json->array.count - 1;
            }
            // Sample up to 10 items to get an average size
            size_t sample_count = json->array.count < 10 ? json->array.count : 10;
            size_t sample_total = 0;

            for (size_t i = 0; i < sample_count; i++) {
                sample_total += estimate_json_size(json->array.items[i]);
            }

            // Extrapolate to full array size
            if (sample_count > 0) {
                estimate += (sample_total / sample_count) * json->array.count;
            }

            return estimate;
        }

        case MCP_JSON_OBJECT: {
            // Start with 2 for { and }
            estimate = 2;

            // Get property names
            char** names = NULL;
            size_t count = 0;
            if (mcp_json_object_get_property_names(json, &names, &count) != 0) {
                return 16; // Fallback if we can't get names
            }

            // Add commas between properties
            if (count > 0) {
                estimate += count - 1;
            }

            // Sample up to 10 properties to get an average size
            size_t sample_count = count < 10 ? count : 10;
            size_t sample_total = 0;

            for (size_t i = 0; i < sample_count && i < count; i++) {
                // Add property name length plus quotes and colon
                sample_total += strlen(names[i]) + 3;
                // Add property value size
                mcp_json_t* value = mcp_json_object_get_property(json, names[i]);
                if (value) {
                    sample_total += estimate_json_size(value);
                }
            }

            // Clean up
            for (size_t i = 0; i < count; i++) {
                free(names[i]);
            }
            free(names);

            // Extrapolate to full object size
            if (sample_count > 0) {
                estimate += (sample_total / sample_count) * count;
            }

            return estimate;
        }

        default:
            return 16; // Default fallback
    }
}

// Internal function for stringification with a specified initial capacity
static char* stringify_with_capacity(const mcp_json_t* json, size_t initial_capacity) {
    // Ensure minimum capacity
    if (initial_capacity < 256) initial_capacity = 256; // Minimum size

    char* output = (char*)malloc(initial_capacity);
    if (output == NULL) {
        return NULL;
    }

    size_t output_size = 0;
    size_t output_capacity = initial_capacity;

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

    // Optionally shrink buffer to fit if there's significant waste
    if (output_capacity > output_size + 1024) {
        char* new_output = (char*)realloc(output, output_size + 1);
        if (new_output != NULL) {
            output = new_output;
        }
    }

    return output; // Caller must free this string
}

// Public API function for stringification
char* mcp_json_stringify(const mcp_json_t* json) {
    // Estimate initial buffer size to reduce reallocations
    size_t initial_capacity = estimate_json_size(json);
    return stringify_with_capacity(json, initial_capacity);
}

// Public API function for stringification with a specified initial capacity
char* mcp_json_stringify_with_capacity(const mcp_json_t* json, size_t initial_capacity) {
    return stringify_with_capacity(json, initial_capacity);
}

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
