#ifndef MCP_JSON_UTILS_H
#define MCP_JSON_UTILS_H

#include <stddef.h>
#include "mcp_json.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Escapes a string according to JSON string rules (RFC 8259).
 *
 * Escapes characters like backslash, double quote, and control characters (U+0000 to U+001F).
 * Writes the escaped string to the provided output buffer.
 *
 * If the output buffer is NULL or output_size is 0, the function calculates
 * the required buffer size (including the null terminator) without writing anything.
 *
 * If the output buffer is provided and output_size is sufficient, the escaped
 * string is written to the buffer, including a null terminator.
 *
 * If the output buffer is provided but output_size is insufficient, the output
 * might be truncated (but will still be null-terminated if output_size > 0).
 * The function still returns the total size required for the full escaped string.
 *
 * @param input The null-terminated input string to escape. Must not be NULL.
 * @param output The buffer to write the escaped string to. Can be NULL to calculate size.
 * @param output_size The size of the output buffer in bytes.
 * @return The total number of bytes required for the escaped string (including
 *         the null terminator). If the return value is greater than or equal to
 *         output_size, the output was truncated (or not written if output was NULL).
 *         Returns -1 on error (e.g., input is NULL).
 */
int mcp_json_escape_string(const char* input, char* output, size_t output_size);

/**
 * @brief Checks if a JSON value is a string.
 *
 * @param json Pointer to the JSON value to check.
 * @return true if the JSON value is a string, false otherwise.
 */
bool mcp_json_is_string(const mcp_json_t* json);

/**
 * @brief Gets the string value from a JSON string.
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_STRING.
 * @return The string value, or NULL if the JSON value is not a string.
 */
const char* mcp_json_string_value(const mcp_json_t* json);

/**
 * @brief Checks if a JSON value is a number.
 *
 * @param json Pointer to the JSON value to check.
 * @return true if the JSON value is a number, false otherwise.
 */
bool mcp_json_is_number(const mcp_json_t* json);

/**
 * @brief Gets the number value from a JSON number.
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_NUMBER.
 * @return The number value, or 0.0 if the JSON value is not a number.
 */
double mcp_json_number_value(const mcp_json_t* json);

/**
 * @brief Checks if a JSON value is a boolean.
 *
 * @param json Pointer to the JSON value to check.
 * @return true if the JSON value is a boolean, false otherwise.
 */
bool mcp_json_is_boolean(const mcp_json_t* json);

/**
 * @brief Gets the boolean value from a JSON boolean.
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_BOOLEAN.
 * @return The boolean value, or false if the JSON value is not a boolean.
 */
bool mcp_json_boolean_value(const mcp_json_t* json);

/**
 * @brief Checks if a JSON value is null.
 *
 * @param json Pointer to the JSON value to check.
 * @return true if the JSON value is null, false otherwise.
 */
bool mcp_json_is_null(const mcp_json_t* json);

/**
 * @brief Checks if a JSON value is an array.
 *
 * @param json Pointer to the JSON value to check.
 * @return true if the JSON value is an array, false otherwise.
 */
bool mcp_json_is_array(const mcp_json_t* json);

/**
 * @brief Checks if a JSON value is an object.
 *
 * @param json Pointer to the JSON value to check.
 * @return true if the JSON value is an object, false otherwise.
 */
bool mcp_json_is_object(const mcp_json_t* json);

/**
 * @brief Gets the size of a JSON object.
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_OBJECT.
 * @return The number of properties in the object, or 0 if the JSON value is not an object.
 */
size_t mcp_json_object_size(const mcp_json_t* json);

/**
 * @brief Gets a property from a JSON object by index.
 *
 * @param json Pointer to the JSON value. Must be of type MCP_JSON_OBJECT.
 * @param index The index of the property to get.
 * @param[out] name Pointer to a variable that will receive the property name.
 * @param[out] value Pointer to a variable that will receive the property value.
 * @return 0 on success, non-zero on error.
 */
int mcp_json_object_get_at(const mcp_json_t* json, size_t index, const char** name, mcp_json_t** value);


#ifdef __cplusplus
}
#endif

#endif // MCP_JSON_UTILS_H
