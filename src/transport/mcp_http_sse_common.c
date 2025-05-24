/**
 * @file mcp_http_sse_common.c
 * @brief Common Server-Sent Events (SSE) functionality shared between client and server
 *
 * This file contains shared functionality for handling Server-Sent Events (SSE)
 * that is common between the client and server implementations.
 */
#include "mcp_http_sse_common.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief SSE field constants
 */
const char* const SSE_FIELD_EVENT = "event: ";  /**< SSE event field prefix */
const char* const SSE_FIELD_ID = "id: ";        /**< SSE ID field prefix */
const char* const SSE_FIELD_DATA = "data: ";    /**< SSE data field prefix */

/**
 * @brief Creates an SSE event structure with the specified properties.
 *
 * This function allocates and initializes a new SSE event structure,
 * copying the provided ID, event type, and data strings if they are not NULL.
 * The event timestamp is set to the current time.
 *
 * @param id The event ID (can be NULL)
 * @param event The event type (can be NULL)
 * @param data The event data (can be NULL)
 * @return sse_event_t* Newly created event or NULL on failure
 */
sse_event_t* sse_event_create(const char* id, const char* event, const char* data) {
    sse_event_t* sse_event = (sse_event_t*)malloc(sizeof(sse_event_t));
    if (sse_event == NULL) {
        mcp_log_error("Failed to allocate memory for SSE event");
        return NULL;
    }

    // Initialize all fields to NULL/0
    sse_event->id = NULL;
    sse_event->event = NULL;
    sse_event->data = NULL;
    sse_event->timestamp = time(NULL);

    // Copy ID if provided
    if (id != NULL) {
        sse_event->id = mcp_strdup(id);
        if (sse_event->id == NULL && id[0] != '\0') {
            mcp_log_error("Failed to allocate memory for SSE event ID");
            sse_event_free(sse_event);
            return NULL;
        }
    }

    // Copy event type if provided
    if (event != NULL) {
        sse_event->event = mcp_strdup(event);
        if (sse_event->event == NULL && event[0] != '\0') {
            mcp_log_error("Failed to allocate memory for SSE event type");
            sse_event_free(sse_event);
            return NULL;
        }
    }

    // Copy data if provided
    if (data != NULL) {
        sse_event->data = mcp_strdup(data);
        if (sse_event->data == NULL && data[0] != '\0') {
            mcp_log_error("Failed to allocate memory for SSE event data");
            sse_event_free(sse_event);
            return NULL;
        }
    }

    mcp_log_debug("Created SSE event: id=%s, type=%s, data_length=%zu",
                 id ? id : "(none)",
                 event ? event : "(default)",
                 data ? strlen(data) : 0);

    return sse_event;
}

/**
 * @brief Clears an SSE event by freeing all its data fields
 * 
 * This function frees all dynamically allocated fields of the event
 * structure but does not free the event structure itself.
 * 
 * @param event Pointer to the event to clear
 */
void sse_event_clear(sse_event_t* event) {
    if (event == NULL) {
        return;
    }

    // Free all allocated string fields
    if (event->id != NULL) {
        free(event->id);
        event->id = NULL;
    }
    
    if (event->event != NULL) {
        free(event->event);
        event->event = NULL;
    }
    
    if (event->data != NULL) {
        free(event->data);
        event->data = NULL;
    }

    // Reset timestamp to a known value
    event->timestamp = 0;
}

/**
 * @brief Frees an SSE event and all its data
 * 
 * This function frees all dynamically allocated fields of the event
 * and the event structure itself. Only use this for dynamically
 * allocated events.
 * 
 * @param event Pointer to the event to free
 */
void sse_event_free(sse_event_t* event) {
    if (event == NULL) {
        return;
    }
    
    // First clear all the data fields
    sse_event_clear(event);
    
    // Then free the event structure itself
    free(event);
}

/**
 * @brief Validate that a string contains only valid characters for SSE text
 *
 * This function checks if a string contains only valid characters for SSE text.
 * It rejects control characters (except for newline, carriage return, and tab)
 * which could cause security or parsing issues.
 *
 * @param str The string to validate
 * @return bool true if the string is valid, false otherwise
 */
bool is_valid_sse_text(const char* str) {
    if (!str) return false;

    for (const char* p = str; *p; p++) {
        // Reject control characters except newline, carriage return, and tab
        if ((unsigned char)*p < 0x20 && *p != '\n' && *p != '\r' && *p != '\t') {
            mcp_log_warn("Invalid control character (0x%02x) found in SSE text", (unsigned char)*p);
            return false;
        }
    }
    return true;
}

/**
 * @brief Helper function to safely free a string pointer and set it to NULL.
 *
 * @param str Pointer to the string pointer to free
 */
void safe_free_string(char** str) {
    if (str != NULL && *str != NULL) {
        free(*str);
        *str = NULL;
    }
}
