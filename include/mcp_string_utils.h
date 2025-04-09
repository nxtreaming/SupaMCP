#ifndef MCP_STRING_UTILS_H
#define MCP_STRING_UTILS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Performs simple wildcard matching.
 *
 * Supports only a single trailing asterisk '*' as a wildcard.
 * - If pattern ends with '*', it matches any text that starts with the
 *   part of the pattern before the '*'.
 * - If pattern is just "*", it matches any text.
 * - Otherwise, requires an exact match between pattern and text.
 *
 * @param pattern The pattern string (potentially with a trailing '*').
 * @param text The text string to match against the pattern.
 * @return True if the text matches the pattern, false otherwise. Returns false if either input is NULL.
 */
bool mcp_wildcard_match(const char* pattern, const char* text);

/**
 * @brief Duplicates a string using malloc.
 *
 * @param str The null-terminated string to duplicate.
 * @return A pointer to the newly allocated string, or NULL if allocation fails or str is NULL.
 *         The caller is responsible for freeing the returned string.
 */
char* mcp_strdup(const char* str);


#ifdef __cplusplus
}
#endif

#endif // MCP_STRING_UTILS_H
