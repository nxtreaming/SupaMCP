#include "internal/json_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// --- JSON Parser Implementation ---
// Simple recursive descent parser.
// Uses the thread-local arena for allocating mcp_json_t nodes.
// String values are always duplicated using malloc/mcp_strdup.

// Helper function to check if a byte is a UTF-8 continuation byte (10xxxxxx)
static inline bool is_utf8_continuation(unsigned char c) {
    return (c & 0xC0) == 0x80; // 10xxxxxx
}

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

// Helper function to convert a 4-digit hex Unicode escape to UTF-8
static int hex_to_utf8(const char* hex, char* utf8) {
    // Convert 4 hex digits to a Unicode code point
    unsigned int code_point = 0;
    for (int i = 0; i < 4; i++) {
        code_point <<= 4;
        if (hex[i] >= '0' && hex[i] <= '9') {
            code_point |= hex[i] - '0';
        } else if (hex[i] >= 'a' && hex[i] <= 'f') {
            code_point |= hex[i] - 'a' + 10;
        } else if (hex[i] >= 'A' && hex[i] <= 'F') {
            code_point |= hex[i] - 'A' + 10;
        } else {
            return 0; // Invalid hex digit
        }
    }

    // Convert code point to UTF-8
    if (code_point < 0x80) {
        // 1-byte UTF-8 (ASCII)
        utf8[0] = (char)code_point;
        return 1;
    } else if (code_point < 0x800) {
        // 2-byte UTF-8
        utf8[0] = (char)(0xC0 | (code_point >> 6));
        utf8[1] = (char)(0x80 | (code_point & 0x3F));
        return 2;
    } else {
        // 3-byte UTF-8
        utf8[0] = (char)(0xE0 | (code_point >> 12));
        utf8[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (code_point & 0x3F));
        return 3;
    }
}

// Optimized single-pass JSON string parser with proper Unicode handling
static char* parse_string(const char** json) {
    if (**json != '"') {
        return NULL;
    }
    (*json)++; // Skip opening quote

    // Use a dynamic buffer approach with initial capacity
    size_t capacity = 32; // Start with a reasonable size
    size_t length = 0;
    char* result = (char*)malloc(capacity);
    if (result == NULL) {
        return NULL;
    }

    const char* p = *json;

    // Single pass: parse and build the string directly
    while (*p != '"' && *p != '\0') {
        // Ensure we have space for at least 4 more bytes (max UTF-8 char size)
        if (length + 4 >= capacity) {
            capacity *= 2;
            char* new_result = (char*)realloc(result, capacity);
            if (new_result == NULL) {
                free(result);
                return NULL;
            }
            result = new_result;
        }

        // Check for control characters
        unsigned char c = (unsigned char)*p;
        if (c < 32 && c != '\t' && c != '\n' && c != '\r' && c != '\b' && c != '\f') {
            if (c < 128 || !is_utf8_continuation(c)) {
                mcp_log_error("Invalid control character in JSON string.");
                free(result);
                return NULL;
            }
        }

        if (*p == '\\') {
            p++; // Skip backslash
            switch (*p) {
                case '"':  result[length++] = '"'; p++; break;
                case '\\': result[length++] = '\\'; p++; break;
                case '/':  result[length++] = '/'; p++; break;
                case 'b':  result[length++] = '\b'; p++; break;
                case 'f':  result[length++] = '\f'; p++; break;
                case 'n':  result[length++] = '\n'; p++; break;
                case 'r':  result[length++] = '\r'; p++; break;
                case 't':  result[length++] = '\t'; p++; break;
                case 'u': { // Unicode escape - convert to UTF-8
                    p++; // Skip 'u'
                    // Validate hex digits
                    for (int i = 0; i < 4; i++) {
                        if (!((p[i] >= '0' && p[i] <= '9') ||
                              (p[i] >= 'a' && p[i] <= 'f') ||
                              (p[i] >= 'A' && p[i] <= 'F'))) {
                            mcp_log_error("Invalid hex digit in \\u escape.");
                            free(result);
                            return NULL;
                        }
                    }

                    // Convert \uXXXX to UTF-8
                    char utf8_buf[4]; // Max 3 bytes for BMP + null terminator
                    int utf8_len = hex_to_utf8(p, utf8_buf);
                    if (utf8_len == 0) {
                        mcp_log_error("Failed to convert Unicode escape to UTF-8.");
                        free(result);
                        return NULL;
                    }

                    // Copy UTF-8 bytes to result
                    for (int i = 0; i < utf8_len; i++) {
                        result[length++] = utf8_buf[i];
                    }

                    p += 4; // Skip the 4 hex digits
                    break;
                }
                default:
                    mcp_log_error("Invalid escape sequence '\\%c'.", *p);
                    free(result);
                    return NULL;
            }
        } else {
            if (*p == '\0') {
                mcp_log_error("Embedded null byte found in JSON string.");
                free(result);
                return NULL;
            }
            result[length++] = *p++;
        }
    }

    if (*p != '"') {
        mcp_log_error("Unterminated JSON string.");
        free(result);
        return NULL;
    }

    // Ensure null termination
    if (length + 1 > capacity) {
        char* new_result = (char*)realloc(result, length + 1);
        if (new_result == NULL) {
            free(result);
            return NULL;
        }
        result = new_result;
    }
    result[length] = '\0';

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
        // Set property using generic hash table
        // mcp_hashtable_put will handle key duplication (using mcp_hashtable_string_dup)
        // and freeing old value if key exists (using mcp_json_hashtable_value_free)
        if (mcp_hashtable_put(object->object_table, name, value) != 0) {
            mcp_log_error("JSON parse error: Failed to set property '%s' using mcp_hashtable_put.", name);
            free(name); // Free the parsed name string
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
