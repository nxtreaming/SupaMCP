/**
 * @file kmcp_server_connection.h
 * @brief Server connection structure definition
 */

#ifndef KMCP_SERVER_CONNECTION_H
#define KMCP_SERVER_CONNECTION_H

#include <stddef.h>
#include <stdbool.h>
#include "mcp_types.h"
#include "mcp_transport.h"
#include "mcp_client.h"
#include "kmcp_process.h"
#include "kmcp_http_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Server connection structure
 */
struct kmcp_server_connection {
    struct {
        char* name;                /**< Server name */
        char* command;             /**< Launch command (for local processes) */
        char** args;               /**< Command arguments */
        size_t args_count;         /**< Number of arguments */
        char* url;                 /**< HTTP URL (for HTTP connections) */
        char* api_key;             /**< API key (for HTTP connections) */
        char** env;                /**< Environment variables */
        size_t env_count;          /**< Number of environment variables */
        bool is_http;              /**< Whether this is an HTTP connection */
    } config;                      /**< Server configuration */

    mcp_transport_t* transport;    /**< Transport layer handle */
    mcp_client_t* client;          /**< MCP client */
    kmcp_http_client_t* http_client; /**< HTTP client (for HTTP connections) */
    bool is_connected;             /**< Connection status */
    char** supported_tools;        /**< List of supported tools */
    size_t supported_tools_count;  /**< Number of tools */
    char** supported_resources;    /**< List of supported resources */
    size_t supported_resources_count; /**< Number of resources */
    kmcp_process_t* process;       /**< Local process (if it's a local server) */
};

typedef struct kmcp_server_connection kmcp_server_connection_t;

#ifdef __cplusplus
}
#endif

#endif /* KMCP_SERVER_CONNECTION_H */
