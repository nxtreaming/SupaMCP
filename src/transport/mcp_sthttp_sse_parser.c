/**
 * @file mcp_sthttp_sse_parser.c
 * @brief Optimized SSE (Server-Sent Events) parser for Streamable HTTP transport
 *
 * This file implements an incremental SSE parser that processes events
 * directly from the receive buffer using a state machine approach,
 * reducing memory allocations and improving performance.
 */
#include "internal/sthttp_transport_internal.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>

// SSE parser states and context are defined in sthttp_transport_internal.h

/**
 * @brief Create SSE parser context
 */
sse_parser_context_t* sse_parser_create(void) {
    sse_parser_context_t* parser = (sse_parser_context_t*)calloc(1, sizeof(sse_parser_context_t));
    if (parser == NULL) {
        mcp_log_error("Failed to allocate SSE parser context");
        return NULL;
    }
    
    parser->state = SSE_PARSE_STATE_FIELD_NAME;
    
    // Initialize buffers with reasonable sizes
    parser->line_buffer_capacity = 512;
    parser->line_buffer = (char*)malloc(parser->line_buffer_capacity);
    if (parser->line_buffer == NULL) {
        free(parser);
        return NULL;
    }
    
    parser->field_name_capacity = 64;
    parser->current_field_name = (char*)malloc(parser->field_name_capacity);
    if (parser->current_field_name == NULL) {
        free(parser->line_buffer);
        free(parser);
        return NULL;
    }
    
    parser->field_value_capacity = 256;
    parser->current_field_value = (char*)malloc(parser->field_value_capacity);
    if (parser->current_field_value == NULL) {
        free(parser->line_buffer);
        free(parser->current_field_name);
        free(parser);
        return NULL;
    }
    
    parser->data_accumulator_capacity = 1024;
    parser->data_accumulator = (char*)malloc(parser->data_accumulator_capacity);
    if (parser->data_accumulator == NULL) {
        free(parser->line_buffer);
        free(parser->current_field_name);
        free(parser->current_field_value);
        free(parser);
        return NULL;
    }
    
    return parser;
}

/**
 * @brief Destroy SSE parser context
 */
void sse_parser_destroy(sse_parser_context_t* parser) {
    if (parser == NULL) {
        return;
    }
    
    free(parser->line_buffer);
    free(parser->current_field_name);
    free(parser->current_field_value);
    free(parser->data_accumulator);
    free(parser->event_id);
    free(parser->event_type);
    free(parser->event_data);
    free(parser);
}

/**
 * @brief Reset parser for new event
 */
void sse_parser_reset(sse_parser_context_t* parser) {
    if (parser == NULL) {
        return;
    }
    
    parser->state = SSE_PARSE_STATE_FIELD_NAME;
    parser->line_buffer_length = 0;
    parser->field_name_length = 0;
    parser->field_value_length = 0;
    parser->data_accumulator_length = 0;
    parser->in_field_value = false;
    parser->field_value_started = false;
    
    free(parser->event_id);
    free(parser->event_type);
    free(parser->event_data);
    parser->event_id = NULL;
    parser->event_type = NULL;
    parser->event_data = NULL;
}

/**
 * @brief Expand buffer if needed
 */
static int expand_buffer(char** buffer, size_t* capacity, size_t needed_size) {
    if (buffer == NULL || capacity == NULL) {
        return -1;
    }
    
    if (needed_size <= *capacity) {
        return 0;
    }
    
    size_t new_capacity = *capacity;
    while (new_capacity < needed_size) {
        new_capacity *= 2;
    }
    
    char* new_buffer = (char*)realloc(*buffer, new_capacity);
    if (new_buffer == NULL) {
        mcp_log_error("Failed to expand SSE parser buffer");
        return -1;
    }
    
    *buffer = new_buffer;
    *capacity = new_capacity;
    return 0;
}

/**
 * @brief Process field when complete
 */
static int process_field(sse_parser_context_t* parser) {
    if (parser == NULL) {
        return -1;
    }
    
    // Null-terminate field name and value
    parser->current_field_name[parser->field_name_length] = '\0';
    parser->current_field_value[parser->field_value_length] = '\0';
    
    // Process known fields
    if (strcmp(parser->current_field_name, "id") == 0) {
        free(parser->event_id);
        parser->event_id = mcp_strdup(parser->current_field_value);
    } else if (strcmp(parser->current_field_name, "event") == 0) {
        free(parser->event_type);
        parser->event_type = mcp_strdup(parser->current_field_value);
    } else if (strcmp(parser->current_field_name, "data") == 0) {
        // Accumulate data (may be multi-line)
        size_t value_len = parser->field_value_length;
        size_t needed_size = parser->data_accumulator_length + value_len + 2; // +1 for newline, +1 for null
        
        if (expand_buffer(&parser->data_accumulator, &parser->data_accumulator_capacity, needed_size) != 0) {
            return -1;
        }
        
        if (parser->data_accumulator_length > 0) {
            parser->data_accumulator[parser->data_accumulator_length++] = '\n';
        }
        
        memcpy(parser->data_accumulator + parser->data_accumulator_length, 
               parser->current_field_value, value_len);
        parser->data_accumulator_length += value_len;
        parser->data_accumulator[parser->data_accumulator_length] = '\0';
    }
    
    // Reset field parsing state
    parser->field_name_length = 0;
    parser->field_value_length = 0;
    parser->in_field_value = false;
    parser->field_value_started = false;
    
    return 0;
}

/**
 * @brief Process complete line
 */
static int process_line(sse_parser_context_t* parser) {
    if (parser == NULL) {
        return -1;
    }
    
    // Empty line indicates end of event
    if (parser->line_buffer_length == 0) {
        // Finalize event data
        if (parser->data_accumulator_length > 0) {
            free(parser->event_data);
            parser->event_data = mcp_strdup(parser->data_accumulator);
            parser->data_accumulator_length = 0;
        }
        
        parser->state = SSE_PARSE_STATE_EVENT_COMPLETE;
        return 0;
    }
    
    // Parse field from line
    parser->state = SSE_PARSE_STATE_FIELD_NAME;
    parser->in_field_value = false;
    parser->field_value_started = false;
    parser->field_name_length = 0;
    parser->field_value_length = 0;
    
    for (size_t i = 0; i < parser->line_buffer_length; i++) {
        char c = parser->line_buffer[i];
        
        if (!parser->in_field_value && c == ':') {
            // Switch to field value
            parser->in_field_value = true;
            parser->field_value_started = false;
            continue;
        }
        
        if (!parser->in_field_value) {
            // Field name
            if (expand_buffer(&parser->current_field_name, &parser->field_name_capacity, 
                             parser->field_name_length + 2) != 0) {
                return -1;
            }
            parser->current_field_name[parser->field_name_length++] = c;
        } else {
            // Field value - skip leading space
            if (!parser->field_value_started && c == ' ') {
                continue;
            }
            parser->field_value_started = true;
            
            if (expand_buffer(&parser->current_field_value, &parser->field_value_capacity, 
                             parser->field_value_length + 2) != 0) {
                return -1;
            }
            parser->current_field_value[parser->field_value_length++] = c;
        }
    }
    
    // Process the completed field
    return process_field(parser);
}

/**
 * @brief Process data chunk incrementally
 */
int sse_parser_process(sse_parser_context_t* parser, const char* data, size_t length, 
                      sse_event_t* event_out) {
    if (parser == NULL || data == NULL) {
        return -1;
    }
    
    const char* ptr = data;
    const char* end = data + length;
    
    while (ptr < end && parser->state != SSE_PARSE_STATE_EVENT_COMPLETE && 
           parser->state != SSE_PARSE_STATE_ERROR) {
        
        char c = *ptr++;
        
        if (c == '\n') {
            // End of line - process it
            if (process_line(parser) != 0) {
                parser->state = SSE_PARSE_STATE_ERROR;
                return -1;
            }
            
            parser->line_buffer_length = 0;
            
            if (parser->state == SSE_PARSE_STATE_EVENT_COMPLETE) {
                break;
            }
        } else if (c != '\r') {
            // Add character to line buffer (skip \r)
            if (expand_buffer(&parser->line_buffer, &parser->line_buffer_capacity, 
                             parser->line_buffer_length + 2) != 0) {
                parser->state = SSE_PARSE_STATE_ERROR;
                return -1;
            }
            
            parser->line_buffer[parser->line_buffer_length++] = c;
        }
    }
    
    // Fill event structure if parsing is complete
    if (parser->state == SSE_PARSE_STATE_EVENT_COMPLETE && event_out != NULL) {
        memset(event_out, 0, sizeof(sse_event_t));
        
        if (parser->event_id) {
            event_out->id = mcp_strdup(parser->event_id);
        }
        if (parser->event_type) {
            event_out->event = mcp_strdup(parser->event_type);
        }
        if (parser->event_data) {
            event_out->data = mcp_strdup(parser->event_data);
        }
        
        // Reset for next event
        sse_parser_reset(parser);
        return 1; // Event complete
    } else if (parser->state == SSE_PARSE_STATE_ERROR) {
        return -1; // Error
    }
    
    return 0; // Need more data
}

/**
 * @brief Check if event parsing is complete
 */
bool sse_parser_is_complete(const sse_parser_context_t* parser) {
    return parser != NULL && parser->state == SSE_PARSE_STATE_EVENT_COMPLETE;
}

/**
 * @brief Check if there was a parsing error
 */
bool sse_parser_has_error(const sse_parser_context_t* parser) {
    return parser != NULL && parser->state == SSE_PARSE_STATE_ERROR;
}
