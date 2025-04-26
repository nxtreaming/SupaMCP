/**
 * @file http_static_res.h
 * @brief Functions for creating static resource files for the HTTP server
 */

#ifndef HTTP_STATIC_RES_H
#define HTTP_STATIC_RES_H

#include <stdint.h>

/**
 * @brief Check if a file exists
 * 
 * @param path Path to the file
 * @return int 1 if the file exists, 0 otherwise
 */
int http_file_exists(const char* path);

/**
 * @brief Create a simple index.html file
 * 
 * @param index_html Path to the index.html file
 * @param host Host name or IP address
 * @param port Port number
 */
void http_create_index_html(const char* index_html, const char* host, uint16_t port);

/**
 * @brief Create a styles.css file
 * 
 * @param styles_css Path to the styles.css file
 */
void http_create_styles_css(const char* styles_css);

/**
 * @brief Create a CSS file for SSE test
 * 
 * @param sse_test_css Path to the sse_test.css file
 */
void http_create_sse_test_css(const char* sse_test_css);

/**
 * @brief Create a JavaScript file for SSE test
 * 
 * @param sse_test_js Path to the sse_test.js file
 */
void http_create_sse_test_js(const char* sse_test_js);

/**
 * @brief Create an HTML file for SSE test
 * 
 * @param sse_test_html Path to the sse_test.html file
 */
void http_create_sse_test_html(const char* sse_test_html);

#endif /* HTTP_STATIC_RES_H */
