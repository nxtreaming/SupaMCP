#include "mcp_string_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Define platform-specific inline directives */
#if defined(_MSC_VER)
#define MCP_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define MCP_INLINE __attribute__((always_inline)) inline
#else
#define MCP_INLINE inline
#endif

/* Memory alignment for better performance */
#define MCP_ALIGN_SIZE 8

/* Align a value to the specified alignment */
#define MCP_ALIGN_UP(value, alignment) (((value) + ((alignment) - 1)) & ~((alignment) - 1))

bool mcp_wildcard_match(const char* pattern, const char* text) {
    if (!pattern || !text)
        return false;

    // Fast path for "*" pattern
    if (pattern[0] == '*' && pattern[1] == '\0')
        return true;

    // Fast path for exact match (no wildcard)
    if (!strchr(pattern, '*'))
        return strcmp(pattern, text) == 0;

    // Handle trailing wildcard pattern
    size_t pattern_len = strlen(pattern);
    if (pattern[pattern_len - 1] == '*') {
        // Match prefix if pattern ends with '*'
        return strncmp(pattern, text, pattern_len - 1) == 0;
    }

    // This implementation only supports trailing wildcards
    // For more complex wildcard patterns, we would need a more sophisticated algorithm
    return strcmp(pattern, text) == 0;
}

char* mcp_strdup(const char* str) {
    if (str == NULL)
        return NULL;

#if defined(_MSC_VER)
    // Use the built-in function on Windows
    return _strdup(str);
#else
    size_t len = strlen(str);
    // Allocate memory for the new string (+1 for null terminator)
    char* new_str = (char*)malloc(len + 1);
    if (new_str == NULL)
        return NULL;

    // Copy the string content
    // Copy includes the null terminator
    memcpy(new_str, str, len + 1);
    return new_str;
#endif
}

#define DYN_BUF_MIN_CAPACITY 64

// Ensures the buffer has enough capacity for 'additional_len' more characters + null terminator.
// Returns 0 on success, -1 on allocation failure.
static MCP_INLINE int dyn_buf_ensure_capacity(dyn_buf_t* db, size_t additional_len) {
    if (!db)
        return -1;
    // +1 for null terminator
    size_t required_capacity = db->length + additional_len + 1;
    if (required_capacity <= db->capacity)
        return 0;

    // Calculate new capacity (double until sufficient, or use required if larger)
    size_t new_capacity = db->capacity > 0 ? db->capacity : DYN_BUF_MIN_CAPACITY;
    while (new_capacity < required_capacity)
        new_capacity *= 2;

    // Align capacity to MCP_ALIGN_SIZE for better memory access performance
    new_capacity = MCP_ALIGN_UP(new_capacity, MCP_ALIGN_SIZE);

    char* new_buffer = (char*)realloc(db->buffer, new_capacity);
    if (!new_buffer)
        return -1;

    db->buffer = new_buffer;
    db->capacity = new_capacity;
    return 0;
}

int dyn_buf_init(dyn_buf_t* db, size_t initial_capacity) {
    if (!db)
        return -1;

    // Initialize structure
    db->length = 0;

    // Ensure minimum capacity
    if (initial_capacity < DYN_BUF_MIN_CAPACITY)
        initial_capacity = DYN_BUF_MIN_CAPACITY;

    // Align capacity to MCP_ALIGN_SIZE for better memory access performance
    db->capacity = MCP_ALIGN_UP(initial_capacity, MCP_ALIGN_SIZE);

    // Allocate buffer
    db->buffer = (char*)malloc(db->capacity);
    if (!db->buffer) {
        db->capacity = 0;
        return -1;
    }

    // Start with an empty, null-terminated string
    db->buffer[0] = '\0';
    return 0;
}

int dyn_buf_append(dyn_buf_t* db, const char* str) {
    if (!db || !str)
        return -1;

    size_t str_len = strlen(str);
    if (str_len == 0)
        return 0;

    if (dyn_buf_ensure_capacity(db, str_len) != 0)
        return -1;

    // Use memcpy for better performance than strcat
    memcpy(db->buffer + db->length, str, str_len);
    db->length += str_len;
    db->buffer[db->length] = '\0';
    return 0;
}

MCP_INLINE int dyn_buf_append_char(dyn_buf_t* db, char c) {
    if (!db)
        return -1;

    if (dyn_buf_ensure_capacity(db, 1) != 0)
        return -1;

    db->buffer[db->length++] = c;
    db->buffer[db->length] = '\0';
    return 0;
}

// Appends JSON escaped string with optimized implementation
int dyn_buf_append_json_string(dyn_buf_t* db, const char* str) {
    if (!db || !str)
        return -1;

    // First pass: calculate required capacity
    // Start with 2 for the quotes
    size_t additional_len = 2;
    const char* s = str;
    while (*s) {
        char c = *s++;
        switch (c) {
            case '"':
            case '\\':
            case '\b':
            case '\f':
            case '\n':
            case '\r':
            case '\t':
                // Escaped character needs 2 bytes
                additional_len += 2;
                break;
            default:
                // Normal character
                additional_len += 1;
                break;
        }
    }

    // Ensure we have enough capacity
    if (dyn_buf_ensure_capacity(db, additional_len) != 0)
        return -1;

    // Now we know we have enough space, append directly to buffer
    char* dest = db->buffer + db->length;
    // Opening quote
    *dest++ = '"';

    s = str;
    while (*s) {
        char c = *s++;
        switch (c) {
            case '"':  *dest++ = '\\'; *dest++ = '"'; break;
            case '\\': *dest++ = '\\'; *dest++ = '\\'; break;
            case '\b': *dest++ = '\\'; *dest++ = 'b'; break;
            case '\f': *dest++ = '\\'; *dest++ = 'f'; break;
            case '\n': *dest++ = '\\'; *dest++ = 'n'; break;
            case '\r': *dest++ = '\\'; *dest++ = 'r'; break;
            case '\t': *dest++ = '\\'; *dest++ = 't'; break;
            default:   *dest++ = c; break;
        }
    }

    // Closing quote
    *dest++ = '"';
    // Null terminator
    *dest = '\0';

    // Update length
    db->length = dest - db->buffer;
    return 0;
}

char* dyn_buf_finalize(dyn_buf_t* db) {
    if (!db || !db->buffer)
        return NULL;

    char* final_str = db->buffer;

    // Optionally shrink buffer to exact size if there's significant waste
    if (db->length + 1 < db->capacity / 2) {
        char* shrunk_str = (char*)realloc(db->buffer, db->length + 1);
        if (shrunk_str)
            final_str = shrunk_str;
    }

    // Reset the structure but leave buffer ownership to caller
    db->buffer = NULL;
    db->capacity = 0;
    db->length = 0;

    return final_str;
}

void dyn_buf_free(dyn_buf_t* db) {
    if (!db || !db->buffer)
        return;

    free(db->buffer);
    db->buffer = NULL;
    db->capacity = 0;
    db->length = 0;
}

/**
 * @brief Formats a string using printf-style formatting and returns a newly allocated string.
 *
 * @param format The format string.
 * @param ... The arguments to format.
 * @return A newly allocated string containing the formatted result, or NULL on error.
 *         The caller is responsible for freeing the returned string with free().
 */
char* mcp_format_string(const char* format, ...) {
    if (!format)
        return NULL;

    va_list args, args_copy;
    va_start(args, format);
    va_copy(args_copy, args);

    // First, determine the required buffer size
    // +1 for null terminator
    int size = vsnprintf(NULL, 0, format, args) + 1;
    va_end(args);

    if (size <= 0) {
        va_end(args_copy);
        return NULL;
    }

    // Allocate buffer
    char* buffer = (char*)malloc(size);

    if (!buffer) {
        va_end(args_copy);
        return NULL;
    }

    // Format the string
    vsnprintf(buffer, size, format, args_copy);
    va_end(args_copy);

    return buffer;
}

/**
 * @brief Performs a case-insensitive string comparison.
 *
 * @param s1 The first string.
 * @param s2 The second string.
 * @return An integer less than, equal to, or greater than zero if s1 is found,
 *         respectively, to be less than, to match, or be greater than s2.
 */
int mcp_stricmp(const char* s1, const char* s2) {
    if (!s1)
        return s2 ? -1 : 0;
    if (!s2)
        return 1;

#if defined(_MSC_VER)
    // Use the built-in function on Windows
    return _stricmp(s1, s2);
#else
    // Custom implementation for other platforms
    unsigned char c1, c2;
    do {
        c1 = (unsigned char)(*s1++);
        c2 = (unsigned char)(*s2++);

        // Convert to lowercase
        if (c1 >= 'A' && c1 <= 'Z')
            c1 += 'a' - 'A';
        if (c2 >= 'A' && c2 <= 'Z')
            c2 += 'a' - 'A';

        if (c1 == '\0')
            break;
    } while (c1 == c2);

    return c1 - c2;
#endif
}

/**
 * @brief Checks if a string starts with a given prefix (case-sensitive).
 *
 * @param str The string to check.
 * @param prefix The prefix to look for.
 * @return true if str starts with prefix, false otherwise.
 */
bool mcp_str_starts_with(const char* str, const char* prefix) {
    if (!str || !prefix)
        return false;

    size_t prefix_len = strlen(prefix);
    return strncmp(str, prefix, prefix_len) == 0;
}

/**
 * @brief Checks if a string ends with a given suffix (case-sensitive).
 *
 * @param str The string to check.
 * @param suffix The suffix to look for.
 * @return true if str ends with suffix, false otherwise.
 */
bool mcp_str_ends_with(const char* str, const char* suffix) {
    if (!str || !suffix)
        return false;

    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > str_len)
        return false;

    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

/**
 * @brief Splits a string into tokens based on a delimiter character.
 *
 * This function modifies the input string by replacing delimiter characters with null terminators.
 * The returned array of pointers points into the modified input string.
 *
 * @param str The string to split (will be modified).
 * @param delimiter The delimiter character.
 * @param count Pointer to a size_t variable that will receive the number of tokens.
 * @return An array of pointers to the tokens, or NULL on error.
 *         The caller is responsible for freeing the returned array with free().
 */
char** mcp_str_split(char* str, char delimiter, size_t* count) {
    if (!str || !count)
        return NULL;

    // First, count the number of tokens
    // At least one token
    size_t num_tokens = 1;
    for (char* p = str; *p; p++) {
        if (*p == delimiter)
            num_tokens++;
    }

    // Allocate array of pointers
    char** tokens = (char**)malloc(num_tokens * sizeof(char*));
    if (!tokens) {
        *count = 0;
        return NULL;
    }

    // Split the string
    size_t i = 0;
    char* token = str;
    char* p = str;

    while (*p) {
        if (*p == delimiter) {
            // Replace delimiter with null terminator
            *p = '\0';
            tokens[i++] = token;
            token = p + 1;
        }
        p++;
    }

    // Add the last token
    tokens[i] = token;

    *count = num_tokens;
    return tokens;
}
