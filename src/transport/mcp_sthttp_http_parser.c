/**
 * @file mcp_sthttp_http_parser.c
 * @brief Lightweight HTTP parser for Streamable HTTP transport
 *
 * This file implements a streaming HTTP parser that processes responses
 * incrementally without buffering entire responses, improving performance
 * and memory usage.
 */
#include "internal/sthttp_transport_internal.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// HTTP parser states and context are defined in sthttp_transport_internal.h

/**
 * @brief Create HTTP parser context
 */
http_parser_context_t* http_parser_create(void) {
    http_parser_context_t* parser = (http_parser_context_t*)calloc(1, sizeof(http_parser_context_t));
    if (parser == NULL) {
        mcp_log_error("Failed to allocate HTTP parser context");
        return NULL;
    }
    
    parser->state = HTTP_PARSE_STATE_STATUS_LINE;
    parser->line_buffer_size = 1024;
    parser->line_buffer = (char*)malloc(parser->line_buffer_size);
    if (parser->line_buffer == NULL) {
        mcp_log_error("Failed to allocate HTTP parser line buffer");
        free(parser);
        return NULL;
    }
    
    return parser;
}

/**
 * @brief Destroy HTTP parser context
 */
void http_parser_destroy(http_parser_context_t* parser) {
    if (parser == NULL) {
        return;
    }
    
    free(parser->line_buffer);
    free(parser->current_header_name);
    free(parser->current_header_value);
    free(parser);
}

/**
 * @brief Reset parser for new request
 */
void http_parser_reset(http_parser_context_t* parser) {
    if (parser == NULL) {
        return;
    }
    
    parser->state = HTTP_PARSE_STATE_STATUS_LINE;
    parser->status_code = 0;
    parser->content_length = 0;
    parser->has_content_length = false;
    parser->is_chunked = false;
    parser->connection_close = false;
    parser->line_buffer_used = 0;
    parser->body_bytes_received = 0;
    parser->chunk_size = 0;
    parser->in_chunk_data = false;
    
    free(parser->current_header_name);
    free(parser->current_header_value);
    parser->current_header_name = NULL;
    parser->current_header_value = NULL;
    parser->header_name_len = 0;
    parser->header_value_len = 0;
}

/**
 * @brief Parse status line
 */
static int parse_status_line(http_parser_context_t* parser, const char* line) {
    if (parser == NULL || line == NULL) {
        return -1;
    }
    
    // Parse "HTTP/1.x STATUS_CODE REASON"
    if (sscanf(line, "HTTP/1.%*d %d", &parser->status_code) != 1) {
        mcp_log_error("Invalid HTTP status line: %s", line);
        return -1;
    }
    
    parser->state = HTTP_PARSE_STATE_HEADERS;
    return 0;
}

/**
 * @brief Parse header line
 */
static int parse_header_line(http_parser_context_t* parser, const char* line) {
    if (parser == NULL || line == NULL) {
        return -1;
    }
    
    // Empty line indicates end of headers
    if (strlen(line) == 0) {
        parser->state = parser->has_content_length || parser->is_chunked ? 
                       HTTP_PARSE_STATE_BODY : HTTP_PARSE_STATE_COMPLETE;
        return 0;
    }
    
    // Find colon separator
    const char* colon = strchr(line, ':');
    if (colon == NULL) {
        mcp_log_error("Invalid header line: %s", line);
        return -1;
    }
    
    // Extract header name
    size_t name_len = colon - line;
    char* name = (char*)malloc(name_len + 1);
    if (name == NULL) {
        return -1;
    }
    memcpy(name, line, name_len);
    name[name_len] = '\0';
    
    // Convert to lowercase for case-insensitive comparison
    for (size_t i = 0; i < name_len; i++) {
        name[i] = (char)tolower(name[i]);
    }
    
    // Extract header value (skip colon and leading spaces)
    const char* value_start = colon + 1;
    while (*value_start == ' ' || *value_start == '\t') {
        value_start++;
    }
    
    // Process important headers
    if (strcmp(name, "content-length") == 0) {
        parser->content_length = strtoul(value_start, NULL, 10);
        parser->has_content_length = true;
    } else if (strcmp(name, "transfer-encoding") == 0) {
        if (strstr(value_start, "chunked") != NULL) {
            parser->is_chunked = true;
        }
    } else if (strcmp(name, "connection") == 0) {
        if (strstr(value_start, "close") != NULL) {
            parser->connection_close = true;
        }
    }
    
    free(name);
    return 0;
}

/**
 * @brief Expand line buffer if needed
 */
static int expand_line_buffer(http_parser_context_t* parser, size_t needed_size) {
    if (parser == NULL) {
        return -1;
    }
    
    if (needed_size <= parser->line_buffer_size) {
        return 0;
    }
    
    size_t new_size = parser->line_buffer_size;
    while (new_size < needed_size) {
        new_size *= 2;
    }
    
    char* new_buffer = (char*)realloc(parser->line_buffer, new_size);
    if (new_buffer == NULL) {
        mcp_log_error("Failed to expand HTTP parser line buffer");
        return -1;
    }
    
    parser->line_buffer = new_buffer;
    parser->line_buffer_size = new_size;
    return 0;
}

/**
 * @brief Process data chunk
 */
int http_parser_process(http_parser_context_t* parser, const char* data, size_t length, 
                       http_response_t* response) {
    if (parser == NULL || data == NULL || response == NULL) {
        return -1;
    }
    
    const char* ptr = data;
    const char* end = data + length;
    
    while (ptr < end && parser->state != HTTP_PARSE_STATE_COMPLETE && 
           parser->state != HTTP_PARSE_STATE_ERROR) {
        
        if (parser->state == HTTP_PARSE_STATE_BODY) {
            // Handle body data
            size_t remaining_body = parser->content_length - parser->body_bytes_received;
            size_t available = end - ptr;
            size_t to_consume = (remaining_body < available) ? remaining_body : available;
            
            parser->body_bytes_received += to_consume;
            ptr += to_consume;
            
            if (parser->body_bytes_received >= parser->content_length) {
                parser->state = HTTP_PARSE_STATE_COMPLETE;
            }
            continue;
        }
        
        // Process line-based data (status line and headers)
        while (ptr < end) {
            char c = *ptr++;
            
            if (c == '\n') {
                // End of line found
                parser->line_buffer[parser->line_buffer_used] = '\0';
                
                // Remove trailing \r if present
                if (parser->line_buffer_used > 0 && 
                    parser->line_buffer[parser->line_buffer_used - 1] == '\r') {
                    parser->line_buffer[parser->line_buffer_used - 1] = '\0';
                }
                
                // Process the line
                int result = 0;
                if (parser->state == HTTP_PARSE_STATE_STATUS_LINE) {
                    result = parse_status_line(parser, parser->line_buffer);
                } else if (parser->state == HTTP_PARSE_STATE_HEADERS) {
                    result = parse_header_line(parser, parser->line_buffer);
                }
                
                if (result != 0) {
                    parser->state = HTTP_PARSE_STATE_ERROR;
                    return -1;
                }
                
                parser->line_buffer_used = 0;
                break;
            } else {
                // Add character to line buffer
                if (expand_line_buffer(parser, parser->line_buffer_used + 2) != 0) {
                    parser->state = HTTP_PARSE_STATE_ERROR;
                    return -1;
                }
                
                parser->line_buffer[parser->line_buffer_used++] = c;
            }
        }
    }
    
    // Fill response structure if parsing is complete
    if (parser->state == HTTP_PARSE_STATE_COMPLETE) {
        response->status_code = parser->status_code;
        response->content_length = parser->content_length;
        return 1; // Complete
    } else if (parser->state == HTTP_PARSE_STATE_ERROR) {
        return -1; // Error
    }
    
    return 0; // Need more data
}

/**
 * @brief Check if parsing is complete
 */
bool http_parser_is_complete(const http_parser_context_t* parser) {
    return parser != NULL && parser->state == HTTP_PARSE_STATE_COMPLETE;
}

/**
 * @brief Check if there was a parsing error
 */
bool http_parser_has_error(const http_parser_context_t* parser) {
    return parser != NULL && parser->state == HTTP_PARSE_STATE_ERROR;
}
