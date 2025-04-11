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

#define DYN_BUF_MIN_CAPACITY 64

// Ensures the buffer has enough capacity for 'additional_len' more characters + null terminator.
// Returns 0 on success, -1 on allocation failure.
static int dyn_buf_ensure_capacity(dyn_buf_t* db, size_t additional_len) {
    if (!db) return -1;
    size_t required_capacity = db->length + additional_len + 1; // +1 for null terminator
    if (required_capacity <= db->capacity) {
        return 0; // Enough space already
    }

    // Calculate new capacity (double until sufficient, or use required if larger)
    size_t new_capacity = db->capacity > 0 ? db->capacity : DYN_BUF_MIN_CAPACITY;
    while (new_capacity < required_capacity) {
        new_capacity *= 2;
    }

    char* new_buffer = (char*)realloc(db->buffer, new_capacity);
    if (!new_buffer) {
        // Allocation failed
        return -1;
    }

    db->buffer = new_buffer;
    db->capacity = new_capacity;
    return 0;
}

int dyn_buf_init(dyn_buf_t* db, size_t initial_capacity) {
    if (!db) return -1;
    db->length = 0;
    db->capacity = (initial_capacity > 0) ? initial_capacity : DYN_BUF_MIN_CAPACITY;
    db->buffer = (char*)malloc(db->capacity);
    if (!db->buffer) {
        db->capacity = 0;
        return -1; // Allocation failed
    }
    db->buffer[0] = '\0'; // Start with an empty, null-terminated string
    return 0;
}

int dyn_buf_append(dyn_buf_t* db, const char* str) {
    if (!db || !str) return -1;
    size_t str_len = strlen(str);
    if (dyn_buf_ensure_capacity(db, str_len) != 0) {
        return -1; // Failed to ensure capacity
    }
    // Use memcpy for potentially better performance than strcat
    memcpy(db->buffer + db->length, str, str_len);
    db->length += str_len;
    db->buffer[db->length] = '\0'; // Ensure null termination
    return 0;
}

int dyn_buf_append_char(dyn_buf_t* db, char c) {
    if (!db) return -1;
    if (dyn_buf_ensure_capacity(db, 1) != 0) {
        return -1;
    }
    db->buffer[db->length++] = c;
    db->buffer[db->length] = '\0';
    return 0;
}

// Appends JSON escaped string. Simplified version.
int dyn_buf_append_json_string(dyn_buf_t* db, const char* str) {
    if (!db || !str) return -1;

    // Estimate required capacity (worst case: every char needs escaping + quotes)
    size_t max_additional = strlen(str) * 2 + 2; // *2 for \X, +2 for quotes
    if (dyn_buf_ensure_capacity(db, max_additional) != 0) {
        return -1;
    }

    dyn_buf_append_char(db, '"');
    while (*str) {
        char c = *str++;
        switch (c) {
            case '"':  dyn_buf_append(db, "\\\""); break;
            case '\\': dyn_buf_append(db, "\\\\"); break;
            case '\b': dyn_buf_append(db, "\\b"); break;
            case '\f': dyn_buf_append(db, "\\f"); break;
            case '\n': dyn_buf_append(db, "\\n"); break;
            case '\r': dyn_buf_append(db, "\\r"); break;
            case '\t': dyn_buf_append(db, "\\t"); break;
            // TODO: Handle other control characters (U+0000 to U+001F) if necessary
            default:   dyn_buf_append_char(db, c); break;
        }
        // Check for errors during append (though capacity was pre-checked)
        // This basic version assumes append succeeds if ensure_capacity worked.
    }
    dyn_buf_append_char(db, '"');
    return 0;
}

char* dyn_buf_finalize(dyn_buf_t* db) {
    if (!db || !db->buffer) return NULL;
    char* final_str = db->buffer;
    // Optionally, shrink buffer to fit? For now, just return it.
    // char* shrunk_str = realloc(db->buffer, db->length + 1);
    // if (shrunk_str) final_str = shrunk_str; // Use shrunk if successful

    // Reset the structure but leave buffer ownership to caller
    db->buffer = NULL;
    db->capacity = 0;
    db->length = 0;
    return final_str;
}

void dyn_buf_free(dyn_buf_t* db) {
    if (db && db->buffer) {
        free(db->buffer);
        db->buffer = NULL;
        db->capacity = 0;
        db->length = 0;
    }
}
