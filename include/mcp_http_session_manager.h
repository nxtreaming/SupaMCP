/**
 * @file mcp_http_session_manager.h
 * @brief HTTP Session Manager for Streamable HTTP Transport
 *
 * This header defines the session management functionality for the
 * Streamable HTTP transport as specified in MCP 2025-03-26.
 */
#ifndef MCP_HTTP_SESSION_MANAGER_H
#define MCP_HTTP_SESSION_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum length of a session ID
 */
#define MCP_SESSION_ID_MAX_LENGTH 128

/**
 * @brief Default session timeout in seconds (1 hour)
 */
#define MCP_SESSION_DEFAULT_TIMEOUT_SECONDS 3600

/**
 * @brief HTTP header name for session ID
 */
#define MCP_SESSION_HEADER_NAME "Mcp-Session-Id"

/**
 * @brief Session state enumeration
 */
typedef enum {
    MCP_SESSION_STATE_ACTIVE = 0,
    MCP_SESSION_STATE_EXPIRED,
    MCP_SESSION_STATE_TERMINATED
} mcp_session_state_t;

/**
 * @brief Opaque session handle
 */
typedef struct mcp_http_session mcp_http_session_t;

/**
 * @brief Opaque session manager handle
 */
typedef struct mcp_http_session_manager mcp_http_session_manager_t;

/**
 * @brief Session event callback function type
 *
 * @param session_id Session ID that triggered the event
 * @param state New session state
 * @param user_data User data provided when setting the callback
 */
typedef void (*mcp_session_event_callback_t)(const char* session_id, mcp_session_state_t state, void* user_data);

/**
 * @brief Create a new session manager
 *
 * @param default_timeout_seconds Default timeout for new sessions (0 for no timeout)
 * @return mcp_http_session_manager_t* Session manager handle or NULL on failure
 */
mcp_http_session_manager_t* mcp_session_manager_create(uint32_t default_timeout_seconds);

/**
 * @brief Destroy a session manager and all its sessions
 *
 * @param manager Session manager to destroy
 */
void mcp_session_manager_destroy(mcp_http_session_manager_t* manager);

/**
 * @brief Create a new session
 *
 * @param manager Session manager
 * @param session_id_out Buffer to store the generated session ID (must be at least MCP_SESSION_ID_MAX_LENGTH bytes)
 * @param timeout_seconds Session timeout in seconds (0 for default, -1 for no timeout)
 * @return mcp_http_session_t* Session handle or NULL on failure
 */
mcp_http_session_t* mcp_session_manager_create_session(mcp_http_session_manager_t* manager, 
                                                      char* session_id_out, 
                                                      int32_t timeout_seconds);

/**
 * @brief Get an existing session by ID
 *
 * @param manager Session manager
 * @param session_id Session ID to look up
 * @return mcp_http_session_t* Session handle or NULL if not found or expired
 */
mcp_http_session_t* mcp_session_manager_get_session(mcp_http_session_manager_t* manager, const char* session_id);

/**
 * @brief Terminate a session
 *
 * @param manager Session manager
 * @param session_id Session ID to terminate
 * @return bool true if session was found and terminated, false otherwise
 */
bool mcp_session_manager_terminate_session(mcp_http_session_manager_t* manager, const char* session_id);

/**
 * @brief Update session last access time
 *
 * @param session Session to update
 */
void mcp_session_touch(mcp_http_session_t* session);

/**
 * @brief Get session ID
 *
 * @param session Session handle
 * @return const char* Session ID or NULL if invalid session
 */
const char* mcp_session_get_id(mcp_http_session_t* session);

/**
 * @brief Get session state
 *
 * @param session Session handle
 * @return mcp_session_state_t Session state
 */
mcp_session_state_t mcp_session_get_state(mcp_http_session_t* session);

/**
 * @brief Set session user data
 *
 * @param session Session handle
 * @param user_data User data to associate with the session
 */
void mcp_session_set_user_data(mcp_http_session_t* session, void* user_data);

/**
 * @brief Get session user data
 *
 * @param session Session handle
 * @return void* User data associated with the session
 */
void* mcp_session_get_user_data(mcp_http_session_t* session);

/**
 * @brief Set session event callback
 *
 * @param manager Session manager
 * @param callback Callback function to call on session events
 * @param user_data User data to pass to the callback
 */
void mcp_session_manager_set_event_callback(mcp_http_session_manager_t* manager, 
                                           mcp_session_event_callback_t callback, 
                                           void* user_data);

/**
 * @brief Clean up expired sessions
 *
 * This function should be called periodically to remove expired sessions.
 *
 * @param manager Session manager
 * @return size_t Number of sessions that were cleaned up
 */
size_t mcp_session_manager_cleanup_expired(mcp_http_session_manager_t* manager);

/**
 * @brief Get the number of active sessions
 *
 * @param manager Session manager
 * @return size_t Number of active sessions
 */
size_t mcp_session_manager_get_active_count(mcp_http_session_manager_t* manager);

/**
 * @brief Validate a session ID format
 *
 * @param session_id Session ID to validate
 * @return bool true if valid format, false otherwise
 */
bool mcp_session_id_is_valid(const char* session_id);

#ifdef __cplusplus
}
#endif

#endif /* MCP_HTTP_SESSION_MANAGER_H */
