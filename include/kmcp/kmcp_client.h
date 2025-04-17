/**
 * @file kmcp_client.h
 * @brief Advanced MCP client API with multi-server management support
 */

#ifndef KMCP_CLIENT_H
#define KMCP_CLIENT_H

#include <stddef.h>
#include <stdbool.h>
#include "kmcp_server_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Client configuration
 */
typedef struct {
    char* name;                    /**< Client name */
    char* version;                 /**< Client version */
    bool use_manager;              /**< Whether to use server manager */
    uint32_t timeout_ms;           /**< Request timeout in milliseconds */
} kmcp_client_config_t;

/**
 * @brief Client structure
 */
typedef struct kmcp_client kmcp_client_t;

/**
 * @brief Create a client
 *
 * @param config Client configuration
 * @return kmcp_client_t* Returns client pointer on success, NULL on failure
 */
kmcp_client_t* kmcp_client_create(kmcp_client_config_t* config);

/**
 * @brief Create a client from a configuration file
 *
 * @param config_file Configuration file path
 * @return kmcp_client_t* Returns client pointer on success, NULL on failure
 */
kmcp_client_t* kmcp_client_create_from_file(const char* config_file);

/**
 * @brief Call a tool
 *
 * @param client Client
 * @param tool_name Tool name
 * @param params_json Parameter JSON string
 * @param result_json Pointer to result JSON string, memory allocated by function, caller responsible for freeing
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_client_call_tool(
    kmcp_client_t* client,
    const char* tool_name,
    const char* params_json,
    char** result_json
);

/**
 * @brief Get a resource
 *
 * @param client Client
 * @param resource_uri Resource URI
 * @param content Pointer to resource content, memory allocated by function, caller responsible for freeing
 * @param content_type Pointer to content type, memory allocated by function, caller responsible for freeing
 * @return int Returns 0 on success, non-zero error code on failure
 */
int kmcp_client_get_resource(
    kmcp_client_t* client,
    const char* resource_uri,
    char** content,
    char** content_type
);

/**
 * @brief Get the server manager
 *
 * @param client Client
 * @return kmcp_server_manager_t* Server manager pointer
 */
kmcp_server_manager_t* kmcp_client_get_manager(kmcp_client_t* client);

/**
 * @brief Close the client
 *
 * @param client Client
 */
void kmcp_client_close(kmcp_client_t* client);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_CLIENT_H */
