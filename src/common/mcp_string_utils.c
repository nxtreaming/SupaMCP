#include "mcp_string_utils.h"
#include <string.h>
#include <stdlib.h>

bool mcp_wildcard_match(const char* pattern, const char* text) {
    if (!pattern || !text) return false; // Handle NULL inputs

    size_t pattern_len = strlen(pattern);
    size_t text_len = strlen(text);

    if (pattern_len == 0) {
        return text_len == 0; // Empty pattern matches only empty text
    }

    if (pattern[pattern_len - 1] == '*') {
        if (pattern_len == 1) return true; // Pattern "*" matches everything
        // Match prefix if pattern ends with '*' (and pattern is longer than just "*")
        // Ensure text is at least as long as the pattern prefix
        return pattern_len - 1 <= text_len &&
               strncmp(pattern, text, pattern_len - 1) == 0;
    } else {
        // Exact match required
        return pattern_len == text_len && strcmp(pattern, text) == 0;
    }
}

char* mcp_strdup(const char* str) {
    if (str == NULL) {
        return NULL;
    }
    size_t len = strlen(str);
    // Allocate memory for the new string (+1 for null terminator)
    char* new_str = (char*)malloc(len + 1);
    if (new_str == NULL) {
        // Allocation failed
        return NULL;
    }
    // Copy the string content
    memcpy(new_str, str, len + 1); // Copy includes the null terminator
    return new_str;
}
