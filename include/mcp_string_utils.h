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


// --- Dynamic Buffer for String Building ---

/** @brief Structure for dynamically growing string buffer. */
typedef struct {
    char* buffer;   /**< Pointer to the character buffer. */
    size_t length;  /**< Current length of the string in the buffer (excluding null terminator). */
    size_t capacity;/**< Current allocated capacity of the buffer. */
} dyn_buf_t;

/**
 * @brief Initializes a dynamic buffer.
 * @param db Pointer to the dyn_buf_t structure.
 * @param initial_capacity The initial capacity to allocate.
 * @return 0 on success, -1 on allocation failure.
 */
int dyn_buf_init(dyn_buf_t* db, size_t initial_capacity);

/**
 * @brief Appends a null-terminated string to the dynamic buffer, resizing if necessary.
 * @param db Pointer to the dyn_buf_t structure.
 * @param str The string to append.
 * @return 0 on success, -1 on allocation failure.
 */
int dyn_buf_append(dyn_buf_t* db, const char* str);

/**
 * @brief Appends a single character to the dynamic buffer, resizing if necessary.
 * @param db Pointer to the dyn_buf_t structure.
 * @param c The character to append.
 * @return 0 on success, -1 on allocation failure.
 */
int dyn_buf_append_char(dyn_buf_t* db, char c);

/**
 * @brief Appends a string, escaping characters necessary for JSON embedding.
 * @param db Pointer to the dyn_buf_t structure.
 * @param str The string to append and escape.
 * @return 0 on success, -1 on allocation failure.
 */
int dyn_buf_append_json_string(dyn_buf_t* db, const char* str);

/**
 * @brief Finalizes the dynamic buffer, returning the allocated string.
 * The caller takes ownership of the returned string and must free it.
 * The dyn_buf_t structure is reset but not freed.
 * @param db Pointer to the dyn_buf_t structure.
 * @return Pointer to the finalized, null-terminated string, or NULL on failure.
 */
char* dyn_buf_finalize(dyn_buf_t* db);

/**
 * @brief Frees the internal buffer of a dynamic buffer structure.
 * Does not free the dyn_buf_t structure itself.
 * @param db Pointer to the dyn_buf_t structure.
 */
void dyn_buf_free(dyn_buf_t* db);


#ifdef __cplusplus
}
#endif

#endif // MCP_STRING_UTILS_H
