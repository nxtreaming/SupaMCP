#include "internal/json_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// --- JSON Parser Implementation ---
// Simple recursive descent parser.
// Uses the thread-local arena for allocating mcp_json_t nodes.
// String values are always duplicated using malloc/mcp_strdup.

// Forward declarations for static parser helper functions
static void skip_whitespace(const char** json);
static char* parse_string(const char** json); // Uses malloc/mcp_strdup
static mcp_json_t* parse_object(const char** json, int depth);
static mcp_json_t* parse_array(const char** json, int depth);
static mcp_json_t* parse_number(const char** json);
// parse_value is declared in the internal header

static void skip_whitespace(const char** json) {
    while (**json == ' ' || **json == '\t' || **json == '\n' || **json == '\r') {
        (*json)++;
    }
}

// Parses a JSON string, handling basic escapes. Uses malloc.
// NOTE: Does not handle \uXXXX escapes correctly in this simplified version.
static char* parse_string(const char** json) {
    if (**json != '"') {
        return NULL;
    }
    (*json)++; // Skip opening quote

    // First pass: calculate required length and check for invalid escapes/chars
    size_t required_len = 0;
    const char* p = *json;
    while (*p != '"' && *p != '\0') {
        if (*p < 32 && *p != '\t' && *p != '\n' && *p != '\r' && *p != '\b' && *p != '\f') {
             fprintf(stderr, "Error: Invalid control character in JSON string.\n");
             return NULL; // Invalid control character
        }
        if (*p == '\\') {
            p++; // Skip backslash
            switch (*p) {
                case '"': case '\\': case '/': case 'b':
                case 'f': case 'n': case 'r': case 't':
                    required_len++;
                    p++;
                    break;
                case 'u': // Unicode escape - basic parser just skips 4 hex digits
                    p++;
                    for (int i = 0; i < 4; i++) {
                        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))) {
                             fprintf(stderr, "Error: Invalid hex digit in \\u escape.\n");
                             return NULL; // Invalid hex
                         }
                        p++;
                    }
                    // TODO: Proper UTF-8 conversion needed here for correct length/value
                    required_len++; // Placeholder: Assume 1 char for now
                    break;
                default:
                    fprintf(stderr, "Error: Invalid escape sequence '\\%c'.\n", *p);
                    return NULL; // Invalid escape
            }
        } else {
            if (*p == '\0') { // Check for embedded null byte
                 fprintf(stderr, "Error: Embedded null byte found in JSON string.\n");
                 return NULL;
            }
            required_len++;
            p++;
        }
    }

    if (*p != '"') {
        return NULL; // Unterminated string
    }

    // Allocate buffer for the unescaped string
    char* result = (char*)malloc(required_len + 1);
    if (result == NULL) {
        return NULL;
    }

    // Second pass: copy characters and handle escapes
    p = *json; // Reset pointer to start of string content
    char* q = result;
    while (*p != '"') {
        if (*p == '\\') {
            p++; // Skip backslash
            switch (*p) {
                case '"':  *q++ = '"'; p++; break;
                case '\\': *q++ = '\\'; p++; break;
                case '/':  *q++ = '/'; p++; break;
                case 'b':  *q++ = '\b'; p++; break;
                case 'f':  *q++ = '\f'; p++; break;
                case 'n':  *q++ = '\n'; p++; break;
                case 'r':  *q++ = '\r'; p++; break;
                case 't':  *q++ = '\t'; p++; break;
                case 'u': // Unicode escape - basic parser puts '?'
                    p++;    // Advance past 'u'
                    p += 4; // Advance past XXXX
                    *q++ = '?'; // Placeholder
                    // No extra p++ needed here
                    break;
                // No default needed due to first pass validation
            }
            // Removed the p++ that was here, advancement is handled in cases now
        } else {
            *q++ = *p++;
        }
    }
    *q = '\0'; // Null-terminate the result

    *json = p + 1; // Move original pointer past the closing quote
    return result;
}

static mcp_json_t* parse_object(const char** json, int depth) {
    if (depth > MCP_JSON_MAX_PARSE_DEPTH) {
        mcp_log_error("JSON parsing depth exceeded limit (%d).", MCP_JSON_MAX_PARSE_DEPTH);
        return NULL; // Depth limit exceeded
    }
    if (**json != '{') {
        return NULL;
    }
    mcp_json_t* object = mcp_json_object_create(); // Uses thread-local arena
    if (object == NULL) {
        // Error already logged by create function
        return NULL;
    }
    (*json)++; // Skip '{'
    skip_whitespace(json);
    if (**json == '}') {
        (*json)++; // Skip '}'
        return object; // Empty object
    }
    while (1) {
        skip_whitespace(json);
        char* name = parse_string(json); // Name uses malloc
        if (name == NULL) {
            // Don't destroy object here, let caller handle cleanup via arena reset/destroy
            return NULL; // Invalid key
        }
        skip_whitespace(json);
        if (**json != ':') {
            mcp_log_error("JSON parse error: Expected ':' after object key '%s'.", name);
            free(name);
            return NULL; // Expected colon
        }
        (*json)++; // Skip ':'
        skip_whitespace(json);
        mcp_json_t* value = parse_value(json, depth + 1);
        if (value == NULL) {
            mcp_log_error("JSON parse error: Failed to parse value for object key '%s'.", name);
            free(name);
            return NULL; // Invalid value
        }
        // Set property - uses malloc for entry/name, value is from thread-local arena
        if (mcp_json_object_table_set(&object->object, name, value) != 0) { // Pass table directly
            mcp_log_error("JSON parse error: Failed to set property '%s'.", name);
            free(name);
            // Don't destroy value (it's in arena), don't destroy object
            return NULL; // Set property failed
        }
        free(name); // Free the malloc'd name string
        skip_whitespace(json);
        if (**json == '}') {
            (*json)++; // Skip '}'
            return object;
        }
        if (**json != ',') {
            mcp_log_error("JSON parse error: Expected ',' or '}' after object property.");
            return NULL; // Expected comma or closing brace
        }
        (*json)++; // Skip ','
    }
}

static mcp_json_t* parse_array(const char** json, int depth) {
     if (depth > MCP_JSON_MAX_PARSE_DEPTH) {
        mcp_log_error("JSON parsing depth exceeded limit (%d).", MCP_JSON_MAX_PARSE_DEPTH);
        return NULL; // Depth limit exceeded
    }
    if (**json != '[') {
        return NULL;
    }
    mcp_json_t* array = mcp_json_array_create(); // Uses thread-local arena
    if (array == NULL) {
        // Error logged by create function
        return NULL;
    }
    (*json)++; // Skip '['
    skip_whitespace(json);
    if (**json == ']') {
        (*json)++; // Skip ']'
        return array; // Empty array
    }
    while (1) {
        skip_whitespace(json);
        mcp_json_t* value = parse_value(json, depth + 1);
        if (value == NULL) {
            mcp_log_error("JSON parse error: Failed to parse value in array.");
            // Don't destroy array, let caller handle via arena
            return NULL; // Invalid value in array
        }
        // Add item uses realloc for backing store, not arena
        if (mcp_json_array_add_item(array, value) != 0) {
            mcp_log_error("JSON parse error: Failed to add item to array.");
            // Don't destroy value (it's in arena)
            return NULL; // Add item failed
        }
        skip_whitespace(json);
        if (**json == ']') {
            (*json)++; // Skip ']'
            return array;
        }
        if (**json != ',') {
            mcp_log_error("JSON parse error: Expected ',' or ']' after array element.");
            return NULL; // Expected comma or closing bracket
        }
        (*json)++; // Skip ','
    }
}

static mcp_json_t* parse_number(const char** json) {
    const char* start = *json;
    if (**json == '-') (*json)++;
    if (**json < '0' || **json > '9') return NULL; // Must have at least one digit
    while (**json >= '0' && **json <= '9') (*json)++;
    if (**json == '.') {
        (*json)++;
        if (**json < '0' || **json > '9') return NULL; // Digit must follow '.'
        while (**json >= '0' && **json <= '9') (*json)++;
    }
    if (**json == 'e' || **json == 'E') {
        (*json)++;
        if (**json == '+' || **json == '-') (*json)++;
        if (**json < '0' || **json > '9') return NULL; // Digit must follow 'e'/'E'
        while (**json >= '0' && **json <= '9') (*json)++;
    }
    char* end;
    double value = strtod(start, &end);
    if (end != *json) {
        mcp_log_error("JSON parse error: Invalid number format near '%s'.", start);
        return NULL; // Invalid number format
    }
    return mcp_json_number_create(value); // Uses thread-local arena
}

// Main recursive parsing function
mcp_json_t* parse_value(const char** json, int depth) {
    skip_whitespace(json);
    switch (**json) {
        case '{': return parse_object(json, depth);
        case '[': return parse_array(json, depth);
        case '"': {
            char* string = parse_string(json); // Uses malloc
            if (string == NULL) return NULL;
            mcp_json_t* result = mcp_json_string_create(string); // Uses thread-local arena for node
            free(string); // Free malloc'd string
            return result;
        }
        case 'n':
            if (strncmp(*json, "null", 4) == 0) {
                *json += 4;
                 return mcp_json_null_create(); // Uses thread-local arena
             }
              mcp_log_error("JSON parse error: Expected 'null'.");
             return NULL;
         case 't':
            if (strncmp(*json, "true", 4) == 0) {
                *json += 4;
                 return mcp_json_boolean_create(true); // Uses thread-local arena
             }
              mcp_log_error("JSON parse error: Expected 'true'.");
             return NULL;
         case 'f':
            if (strncmp(*json, "false", 5) == 0) {
                *json += 5;
                 return mcp_json_boolean_create(false); // Uses thread-local arena
             }
              mcp_log_error("JSON parse error: Expected 'false'.");
             return NULL;
         case '-':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
             return parse_number(json);
         default:
              mcp_log_error("JSON parse error: Unexpected character '%c'.", **json);
             return NULL; // Invalid character
     }
}

// Public API function for parsing
mcp_json_t* mcp_json_parse(const char* json) {
    if (json == NULL) {
        return NULL;
    }
    const char* current = json; // Use a temporary pointer
    skip_whitespace(&current);
    mcp_json_t* result = parse_value(&current, 0); // Start parsing at depth 0
     if (result == NULL) {
         // Parsing failed, thread-local arena contains partially allocated nodes.
         // Caller should reset/destroy the thread-local arena if appropriate.
         mcp_log_error("JSON parsing failed.");
         return NULL;
     }
     skip_whitespace(&current);
     if (*current != '\0') {
         mcp_log_error("JSON parse error: Trailing characters found after valid JSON: '%s'", current);
         // Trailing characters after valid JSON
         // Don't destroy result (it's in thread-local arena), let caller handle arena.
        return NULL;
    }
    return result;
}
