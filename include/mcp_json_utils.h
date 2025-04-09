#ifndef MCP_JSON_UTILS_H
#define MCP_JSON_UTILS_H

#include <stddef.h>

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


#ifdef __cplusplus
}
#endif

#endif // MCP_JSON_UTILS_H
