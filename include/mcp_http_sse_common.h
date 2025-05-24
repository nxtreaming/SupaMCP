/**
 * @file mcp_http_sse_common.h
 * @brief Common Server-Sent Events (SSE) definitions and functions
 *
 * This header defines the common data structures and functions used for
 * Server-Sent Events (SSE) that are shared between client and server.
 */
#ifndef MCP_HTTP_SSE_COMMON_H
#define MCP_HTTP_SSE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Structure representing an SSE event
 */
typedef struct {
    char* id;           /**< Event ID (optional) */
    char* event;        /**< Event type (optional) */
    char* data;         /**< Event data (required) */
    time_t timestamp;   /**< When the event was created/received */
} sse_event_t;

/**
 * @brief SSE field constants (defined in .c file)
 */
extern const char* const SSE_FIELD_EVENT;
extern const char* const SSE_FIELD_ID;
extern const char* const SSE_FIELD_DATA;

/**
 * @brief Frees an SSE event and all its data fields
 * 
 * This function frees all dynamically allocated fields of the event
 * structure but does not free the event structure itself.
 * 
 * @param event Pointer to the event to clear
 */
 void sse_event_clear(sse_event_t* event);

 /**
  * @brief Frees an SSE event and all its data
  * 
  * This function frees all dynamically allocated fields of the event
  * and the event structure itself. Only use this for dynamically
  * allocated events.
  * 
  * @param event Pointer to the event to free
  */
 void sse_event_free(sse_event_t* event);
 
 /**
  * @brief Creates a new SSE event
  * 
  * @param id Event ID (can be NULL)
  * @param event Event type (can be NULL)
  * @param data Event data (can be NULL)
  * @return sse_event_t* New event or NULL on failure
  */
 sse_event_t* sse_event_create(const char* id, const char* event, const char* data);
 
 /**
  * @brief Validates if text is safe for SSE
  * 
  * @param str String to validate
  * @return bool true if valid, false if contains invalid characters
  */
 bool is_valid_sse_text(const char* str);
 
 /**
  * @brief Safely frees a string and sets the pointer to NULL
  * 
  * @param str Pointer to string pointer to free
  */
 void safe_free_string(char** str);
 
#ifdef __cplusplus
}
#endif

#endif /* MCP_HTTP_SSE_COMMON_H */
