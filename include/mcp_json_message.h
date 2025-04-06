#ifndef MCP_JSON_MESSAGE_H
#define MCP_JSON_MESSAGE_H

#include "mcp_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parses a JSON string representing an MCP message (request, response, or notification).
 *
 * Uses the calling thread's thread-local arena for parsing the JSON structure.
 * Note that top-level strings like `method` within the output `message` struct are still `malloc`-ed.
 * @param json_str The null-terminated JSON string representing the message.
 * @param[out] message Pointer to an `mcp_message_t` structure where the parsed message will be stored.
 *                     The caller typically allocates this structure on the stack.
 * @return 0 on success, non-zero on error (e.g., parse error, invalid message structure).
 * @note On success, the `message` structure will contain pointers to dynamically allocated data
 *       (strings like method, error_message, result/params). The caller MUST call `mcp_message_release_contents(message)`
 *       to free this internal data before the `message` structure goes out of scope or is reused. The parsed JSON tree itself lives in the thread-local arena. */
int mcp_json_parse_message(const char* json_str, mcp_message_t* message);

/**
 * @brief Creates a JSON-RPC request string.
 *
 * @param method The method name.
 * @param params The parameters as a valid JSON string (object or array), or NULL for no params.
 * @param id The request ID.
 * @return A newly allocated JSON-RPC request string, or NULL on error. The caller must free the returned string.
 */
char* mcp_json_create_request(const char* method, const char* params, uint64_t id);

/**
 * @brief Creates a JSON-RPC success response string.
 *
 * @param id The ID of the request being responded to.
 * @param result The result as a valid JSON string, or NULL to represent JSON null.
 * @return A newly allocated JSON-RPC response string, or NULL on error. The caller must free the returned string.
 */
char* mcp_json_create_response(uint64_t id, const char* result);

/**
 * @brief Creates a JSON-RPC error response string.
 *
 * @param id The ID of the request being responded to (or 0 if ID is unknown/parse error).
 * @param error_code The JSON-RPC error code.
 * @param error_message The error message string (can be NULL).
 * @return A newly allocated JSON-RPC error response string, or NULL on error. The caller must free the returned string.
 */
char* mcp_json_create_error_response(uint64_t id, int error_code, const char* error_message);

/**
 * @brief Converts an MCP message structure into a JSON string representation.
 *
 * @param message Pointer to the `mcp_message_t` structure to stringify.
 * @return A newly allocated null-terminated JSON string, or NULL on error (e.g., allocation failure).
 * @note The caller is responsible for freeing the returned string using `free()`.
 * @note This function uses the thread-local arena for temporary JSON nodes during stringification,
 *       but the final returned string is allocated using `malloc`. */
char* mcp_json_stringify_message(const mcp_message_t* message);

#ifdef __cplusplus
}
#endif

#endif /* MCP_JSON_MESSAGE_H */
