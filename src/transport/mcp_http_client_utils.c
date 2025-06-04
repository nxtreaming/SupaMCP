#include "internal/http_client_utils.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

// JSON-RPC ID field identifier
#define JSON_RPC_ID_FIELD "\"id\":"
// Default ID value when extraction fails
#define DEFAULT_ID_VALUE 0

/**
 * @brief Helper function to check if a character is a JSON whitespace character.
 *
 * @param c The character to check
 * @return int Non-zero if the character is a whitespace, 0 otherwise
 */
static int is_json_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/**
 * @brief Helper function to safely check if we're within the bounds of the JSON data.
 *
 * @param current Current position in the JSON data
 * @param start Start of the JSON data
 * @param size Size of the JSON data
 * @return int Non-zero if within bounds, 0 otherwise
 */
static int is_within_bounds(const char* current, const char* start, size_t size) {
    return (current >= start) && ((size_t)(current - start) < size);
}

/**
 * @brief Extract request ID from JSON-RPC request.
 *
 * This function extracts the request ID from a JSON-RPC request string.
 * It uses a simple string search approach rather than full JSON parsing.
 *
 * @param json_data The JSON-RPC request string
 * @param size Size of the JSON data
 * @return uint64_t The extracted request ID, or 0 if not found or on error
 */
uint64_t extract_request_id(const char* json_data, size_t size) {
    if (json_data == NULL || size == 0) {
        mcp_log_error("Invalid parameters for extract_request_id");
        return DEFAULT_ID_VALUE;
    }

    // Look for "id": field
    const char* id_str = JSON_RPC_ID_FIELD;
    const size_t id_str_len = strlen(id_str);

    // Use strstr to find the ID field
    const char* id_pos = strstr(json_data, id_str);
    if (id_pos == NULL) {
        mcp_log_debug("JSON-RPC ID field not found");
        return DEFAULT_ID_VALUE;
    }

    // Make sure we don't go out of bounds
    if (!is_within_bounds(id_pos + id_str_len, json_data, size)) {
        mcp_log_error("JSON-RPC ID field found but data is truncated");
        return DEFAULT_ID_VALUE;
    }

    // Skip "id": and any whitespace
    id_pos += id_str_len;
    while (is_within_bounds(id_pos, json_data, size) && is_json_whitespace(*id_pos)) {
        id_pos++;
    }

    // Check if we're still within bounds
    if (!is_within_bounds(id_pos, json_data, size)) {
        mcp_log_error("JSON-RPC ID field value is missing");
        return DEFAULT_ID_VALUE;
    }

    // Parse the ID value
    uint64_t id_value = DEFAULT_ID_VALUE;

    if (*id_pos == '"') {
        // String ID - extract the numeric value if it's a number in quotes
        id_pos++; // Skip opening quote

        // Check if the string contains only digits
        const char* digit_start = id_pos;
        bool is_numeric = true;

        while (is_within_bounds(id_pos, json_data, size) && *id_pos != '"') {
            if (!isdigit((unsigned char)*id_pos)) {
                is_numeric = false;
                break;
            }
            id_pos++;
        }

        // If it's a numeric string and we found the closing quote, convert it
        if (is_numeric && is_within_bounds(id_pos, json_data, size) && *id_pos == '"' &&
            id_pos > digit_start) {
            // Create a temporary null-terminated string for conversion
            size_t str_len = id_pos - digit_start;
            char* temp = (char*)malloc(str_len + 1);
            if (temp != NULL) {
                memcpy(temp, digit_start, str_len);
                temp[str_len] = '\0';

                // Convert to integer
                id_value = (uint64_t)strtoull(temp, NULL, 10);
                free(temp);

                mcp_log_debug("Extracted string ID as number: %llu", (unsigned long long)id_value);
            }
        } else {
            mcp_log_debug("String ID found but not converted (non-numeric or invalid format)");
        }
    } else if (isdigit((unsigned char)*id_pos) || *id_pos == '-') {
        // Numeric ID - use strtoull for safer conversion
        char* end_ptr = NULL;
        id_value = (uint64_t)strtoull(id_pos, &end_ptr, 10);

        // Verify that conversion stopped at a valid delimiter
        if (end_ptr != NULL && is_within_bounds(end_ptr, json_data, size) &&
            (*end_ptr == ',' || *end_ptr == '}' || is_json_whitespace(*end_ptr))) {
            mcp_log_debug("Extracted numeric ID: %llu", (unsigned long long)id_value);
        } else {
            mcp_log_error("Invalid numeric ID format");
            id_value = DEFAULT_ID_VALUE;
        }
    } else {
        mcp_log_error("Unsupported ID format: neither string nor number");
    }

    return id_value;
}
