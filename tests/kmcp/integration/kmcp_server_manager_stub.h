#ifndef KMCP_SERVER_MANAGER_STUB_H
#define KMCP_SERVER_MANAGER_STUB_H

#include "kmcp_server_manager.h"
#include "kmcp_client.h"

/**
 * @brief Check if a server exists in the manager
 *
 * @param manager Server manager (must not be NULL)
 * @param name Server name (must not be NULL)
 * @return bool Returns true if the server exists, false otherwise
 */
bool kmcp_server_manager_has_server(kmcp_server_manager_t* manager, const char* name);

/**
 * @brief Add a server to the manager
 *
 * @param manager Server manager (must not be NULL)
 * @param config Server configuration (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_server_manager_add_server(kmcp_server_manager_t* manager, const kmcp_server_config_t* config);

/**
 * @brief Start a server
 *
 * @param manager Server manager (must not be NULL)
 * @param name Server name (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_server_manager_start_server(kmcp_server_manager_t* manager, const char* name);

/**
 * @brief Check if a server is running
 *
 * @param manager Server manager (must not be NULL)
 * @param name Server name (must not be NULL)
 * @return bool Returns true if the server is running, false otherwise
 */
bool kmcp_server_manager_is_server_running(kmcp_server_manager_t* manager, const char* name);

/**
 * @brief Stop a server
 *
 * @param manager Server manager (must not be NULL)
 * @param name Server name (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_server_manager_stop_server(kmcp_server_manager_t* manager, const char* name);

/**
 * @brief Set the server manager for a client
 *
 * @param client Client (must not be NULL)
 * @param manager Server manager (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_client_set_manager(kmcp_client_t* client, kmcp_server_manager_t* manager);

/**
 * @brief Call a tool on a specific server
 *
 * @param client Client (must not be NULL)
 * @param server_name Server name (must not be NULL)
 * @param tool_name Tool name (must not be NULL)
 * @param params_json Parameter JSON string (must not be NULL)
 * @param result_json Pointer to result JSON string, memory allocated by function, caller responsible for freeing
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_client_call_tool_on_server(
    kmcp_client_t* client,
    const char* server_name,
    const char* tool_name,
    const char* params_json,
    char** result_json
);

#endif /* KMCP_SERVER_MANAGER_STUB_H */
