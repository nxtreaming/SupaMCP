#include "internal/json_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// --- MCP Message Parsing/Stringification ---
// These functions now use the thread-local arena implicitly for parsing/creating
// the main JSON structure, but still use malloc/mcp_strdup for strings within
// the mcp_message_t struct and for the stringified result/params.

int mcp_json_parse_message(const char* json_str, mcp_message_t* message) {
    if (json_str == NULL || message == NULL) {
        return -1;
    }
    PROFILE_START("mcp_json_parse_message");
    // Parse using the thread-local arena
    mcp_json_t* json = mcp_json_parse(json_str);
    if (json == NULL) {
        PROFILE_END("mcp_json_parse_message"); // End profile on error
        // Thread-local arena cleanup is handled by the caller
        return -1;
    }
    if (json->type != MCP_JSON_OBJECT) {
        // Parsed something, but not an object. Arena contains the invalid node.
        mcp_log_error("MCP message parse error: Root element is not a JSON object.");
        // Let caller handle arena cleanup.
        return -1;
    }

    mcp_json_t* id = mcp_json_object_get_property(json, "id");
    mcp_json_t* method = mcp_json_object_get_property(json, "method");
    mcp_json_t* params = mcp_json_object_get_property(json, "params");
    mcp_json_t* result = mcp_json_object_get_property(json, "result");
    mcp_json_t* error = mcp_json_object_get_property(json, "error");

    int parse_status = -1; // Default to error
    message->type = MCP_MESSAGE_TYPE_INVALID; // Default type


    // --- Request Check ---
    if (method != NULL && method->type == MCP_JSON_STRING) {
        if (id != NULL) { // Must have ID for request
            // Check ID type (integer or string allowed by spec, but we use uint64_t)
            if (id->type == MCP_JSON_NUMBER) { // || id->type == MCP_JSON_STRING) { // Allow string ID? No, use number for now.
                // Check params type (object or array allowed, or omitted)
                if (params == NULL || params->type == MCP_JSON_OBJECT || params->type == MCP_JSON_ARRAY) {
                    message->type = MCP_MESSAGE_TYPE_REQUEST;
                    message->request.id = (uint64_t)id->number_value; // TODO: Handle potential precision loss or non-integer?
                    message->request.method = mcp_strdup(method->string_value); // Use helper
                    message->request.params = (params != NULL) ? mcp_json_stringify(params) : NULL; // Stringify uses malloc

                    if (message->request.method != NULL && (params == NULL || message->request.params != NULL)) {
                        parse_status = 0; // Success
                    } else {
                        // Allocation failure during stringify/mcp_strdup
                        free(message->request.method); // Method might be allocated
                        free(message->request.params); // Params might be allocated
                        message->type = MCP_MESSAGE_TYPE_INVALID; // Reset type
                        // Parsed JSON tree is in thread-local arena, caller handles cleanup.
                    }
                }
            }

        } else { // Notification (method present, id absent)
             // Check params type (object or array allowed, or omitted)
            if (params == NULL || params->type == MCP_JSON_OBJECT || params->type == MCP_JSON_ARRAY) {
                message->type = MCP_MESSAGE_TYPE_NOTIFICATION;
                message->notification.method = mcp_strdup(method->string_value); // Use helper
                message->notification.params = (params != NULL) ? mcp_json_stringify(params) : NULL; // Stringify uses malloc

                if (message->notification.method != NULL && (params == NULL || message->notification.params != NULL)) {
                    parse_status = 0; // Success
                } else {
                    // Allocation failure
                    free(message->notification.method);
                    free(message->notification.params);
                    message->type = MCP_MESSAGE_TYPE_INVALID;
                    // Parsed JSON tree is in thread-local arena, caller handles cleanup.
                }
            }
        }

    // --- Response Check ---
    } else if (id != NULL && (result != NULL || error != NULL)) { // Response
        // Check ID type
        if (id->type == MCP_JSON_NUMBER) { // || id->type == MCP_JSON_STRING) { // Allow string ID? No.
            message->type = MCP_MESSAGE_TYPE_RESPONSE;
            message->response.id = (uint64_t)id->number_value; // TODO: Handle potential precision loss?

            if (error != NULL && error->type == MCP_JSON_OBJECT) { // Error Response
                 if (result != NULL) {
                     // Error: Both result and error members MUST NOT exist
                     message->type = MCP_MESSAGE_TYPE_INVALID;
                 } else {
                    mcp_json_t* code = mcp_json_object_get_property(error, "code");
                    mcp_json_t* msg = mcp_json_object_get_property(error, "message");
                    // Code MUST be an integer
                    if (code != NULL && code->type == MCP_JSON_NUMBER) { // TODO: Check if integer?
                        // Message MUST be a string
                        if (msg != NULL && msg->type == MCP_JSON_STRING) {
                            message->response.error_code = (mcp_error_code_t)(int)code->number_value;
                            message->response.error_message = mcp_strdup(msg->string_value); // Use helper
                            message->response.result = NULL;
                            if (message->response.error_message != NULL) {
                                parse_status = 0; // Success
                            } else {
                                // mcp_strdup failed
                                message->type = MCP_MESSAGE_TYPE_INVALID;
                                // Parsed JSON tree is in thread-local arena, caller handles cleanup.
                            }
                         } else { // Invalid error message type
                             mcp_log_error("MCP message parse error: Response 'error.message' is not a string.");
                              message->type = MCP_MESSAGE_TYPE_INVALID;
                         }
                     } else { // Invalid error code type
                         mcp_log_error("MCP message parse error: Response 'error.code' is not a number.");
                         message->type = MCP_MESSAGE_TYPE_INVALID;
                     }
                }

            } else if (result != NULL) { // Success Response
                // Success Response (result can be any JSON type)
                message->response.error_code = MCP_ERROR_NONE;
                message->response.error_message = NULL;
                message->response.result = mcp_json_stringify(result); // Uses malloc
                if (message->response.result != NULL) {
                    parse_status = 0; // Success
                 } else {
                     // stringify failed
                     mcp_log_error("Failed to stringify response result.");
                     message->type = MCP_MESSAGE_TYPE_INVALID;
                     // Parsed JSON tree is in thread-local arena, caller handles cleanup.
                }
             } else {
                  // Invalid response: Must have 'result' or 'error'
                  mcp_log_error("MCP message parse error: Response must have 'result' or 'error'.");
                  message->type = MCP_MESSAGE_TYPE_INVALID;
             }

         } else { // Invalid ID type
              mcp_log_error("MCP message parse error: Response 'id' is not a number.");
              message->type = MCP_MESSAGE_TYPE_INVALID;
         }

     // --- Invalid Message Type ---
     } else { // Not a request, notification, or response
         mcp_log_error("MCP message parse error: Message is not a valid request, response, or notification.");
         message->type = MCP_MESSAGE_TYPE_INVALID;
     }

    // Parsed JSON tree ('json') lives in the thread-local arena.
    // The caller is responsible for calling mcp_arena_reset_current_thread()
    // or mcp_arena_destroy_current_thread() eventually.
    PROFILE_END("mcp_json_parse_message");

    return parse_status;
}

// Stringify message uses the _create and _parse functions which now use the
// thread-local arena implicitly for temporary node creation during stringification.
// The final output string is still allocated with malloc.
char* mcp_json_stringify_message(const mcp_message_t* message) {
    if (message == NULL) {
        return NULL;
    }
    PROFILE_START("mcp_json_stringify_message");
    // Create the root JSON object using the thread-local arena
     mcp_json_t* json = mcp_json_object_create();
     if (json == NULL) {
         PROFILE_END("mcp_json_stringify_message"); // End profile on error
         mcp_log_error("Failed to create root JSON object for stringify.");
         return NULL;
     }

    char* final_json_string = NULL; // Store final malloc'd string here

    switch (message->type) {
        case MCP_MESSAGE_TYPE_REQUEST: {
            // Create nodes using thread-local arena
            mcp_json_t* id_node = mcp_json_number_create((double)message->request.id);
            mcp_json_t* method_node = mcp_json_string_create(message->request.method);
            // Parse params string (if exists) using thread-local arena
            mcp_json_t* params_node = (message->request.params != NULL) ? mcp_json_parse(message->request.params) : NULL;

            if (id_node && method_node && (message->request.params == NULL || params_node)) {
                mcp_json_object_set_property(json, "id", id_node); // table_set uses malloc for entry/key
                mcp_json_object_set_property(json, "method", method_node); // table_set uses malloc for entry/key
                if (params_node) {
                    mcp_json_object_set_property(json, "params", params_node); // table_set uses malloc for entry/key
                }

                 final_json_string = mcp_json_stringify(json); // Stringify uses malloc for output buffer
             } else {
                 mcp_log_error("Failed to create/parse nodes for stringifying request.");
                 // Nodes are in thread-local arena, no manual destroy needed here.
             }
            break;
        }

        case MCP_MESSAGE_TYPE_RESPONSE: {
            // Create nodes using thread-local arena
            mcp_json_t* id_node = mcp_json_number_create((double)message->response.id);
            if (!id_node) break; // Failed to create ID node
            mcp_json_object_set_property(json, "id", id_node); // table_set uses malloc

            if (message->response.error_code != MCP_ERROR_NONE) { // Error response
                mcp_json_t* error_obj = mcp_json_object_create(); // Uses arena
                mcp_json_t* code_node = mcp_json_number_create((double)message->response.error_code); // Uses arena
                // Use error_message directly if it's a const literal, otherwise create string node
                mcp_json_t* msg_node = (message->response.error_message != NULL) ? mcp_json_string_create(message->response.error_message) : NULL; // Uses arena

                if (error_obj && code_node && (message->response.error_message == NULL || msg_node)) {
                    mcp_json_object_set_property(error_obj, "code", code_node); // table_set uses malloc
                    if (msg_node) {
                        mcp_json_object_set_property(error_obj, "message", msg_node); // table_set uses malloc
                    }

                    mcp_json_object_set_property(json, "error", error_obj); // table_set uses malloc
                     final_json_string = mcp_json_stringify(json); // Stringify uses malloc
                 } else {
                      mcp_log_error("Failed to create nodes for stringifying error response.");
                     // Nodes are in thread-local arena.
                 }

            } else if (message->response.result != NULL) { // Success response with result
                // Parse result string using thread-local arena
                mcp_json_t* result_node = mcp_json_parse(message->response.result);
                if (result_node) {
                    mcp_json_object_set_property(json, "result", result_node); // table_set uses malloc
                     final_json_string = mcp_json_stringify(json); // Stringify uses malloc
                 } else {
                      mcp_log_error("Failed to parse result string for stringifying response.");
                 }

            } else { // Success response with null result
                mcp_json_t* result_node = mcp_json_null_create(); // Uses arena
                 if (result_node) {
                    mcp_json_object_set_property(json, "result", result_node); // table_set uses malloc
                     final_json_string = mcp_json_stringify(json); // Stringify uses malloc
                  } else {
                       mcp_log_error("Failed to create null result node for stringifying response.");
                 }
             }
            break;
        }

        case MCP_MESSAGE_TYPE_NOTIFICATION: {
             // Create nodes using thread-local arena
             mcp_json_t* method_node = mcp_json_string_create(message->notification.method);
             // Parse params string (if exists) using thread-local arena
             mcp_json_t* params_node = (message->notification.params != NULL) ? mcp_json_parse(message->notification.params) : NULL;

             if (method_node && (message->notification.params == NULL || params_node)) {
                 mcp_json_object_set_property(json, "method", method_node); // table_set uses malloc
                 if (params_node) {
                     mcp_json_object_set_property(json, "params", params_node); // table_set uses malloc
                 }

                  final_json_string = mcp_json_stringify(json); // Stringify uses malloc
              } else {
                   mcp_log_error("Failed to create/parse nodes for stringifying notification.");
                  // Nodes are in thread-local arena.
              }
             break;
        }
        default:
            // Should not happen
            break;
    }

    // Temporary JSON structure 'json' and its sub-nodes were allocated in the
    // thread-local arena. They will be cleaned up when the caller resets or
    // destroys the thread's arena. We don't call mcp_json_destroy on 'json' here.
    PROFILE_END("mcp_json_stringify_message");
    return final_json_string; // Return the malloc'd final string
}


// --- Creation Functions ---

// Note: These functions now use the thread-local arena for temporary JSON nodes
// during creation, but the final returned string is allocated with malloc.

char* mcp_json_create_request(const char* method, const char* params, uint64_t id) {
    if (method == NULL) {
        return NULL;
    }
    PROFILE_START("mcp_json_create_request");

    mcp_json_t* request = mcp_json_object_create(); // Uses arena
    if (request == NULL) {
        PROFILE_END("mcp_json_create_request");
        return NULL;
    }

    mcp_json_t* jsonrpc = mcp_json_string_create("2.0"); // Uses arena
    mcp_json_t* method_node = mcp_json_string_create(method); // Uses arena
    mcp_json_t* id_node = mcp_json_number_create((double)id); // Uses arena
    mcp_json_t* params_node = NULL;

    if (params != NULL) {
        params_node = mcp_json_parse(params); // Uses arena
        if (params_node == NULL) {
            // Invalid params JSON, treat as if params were omitted or handle error?
            // For now, let's omit the params field if parsing fails.
            mcp_log_warn("Invalid JSON provided for request params: %s", params);
            // Arena nodes for failed parse are handled by arena cleanup
        }
    }

    char* final_json_string = NULL;

    // Check if all necessary nodes were created successfully
    if (jsonrpc && method_node && id_node && (params == NULL || params_node != NULL)) {
        mcp_json_object_set_property(request, "jsonrpc", jsonrpc);
        mcp_json_object_set_property(request, "method", method_node);
        mcp_json_object_set_property(request, "id", id_node);
        if (params_node) {
            mcp_json_object_set_property(request, "params", params_node);
        }
        final_json_string = mcp_json_stringify(request); // Uses malloc
    } else {
         mcp_log_error("Failed to create nodes for JSON-RPC request.");
         // Nodes are in arena, no need to destroy individually here.
         // Destroy the partially built request object in the arena if creation failed
         // Note: mcp_json_destroy handles NULL safely
         mcp_json_destroy(request); // This will free associated arena nodes if needed by impl
    }

    // The 'request' object and its direct children (jsonrpc, method_node, id_node, params_node if parsed)
    // were allocated in the thread-local arena. They will be cleaned up later by the arena reset/destroy.
    PROFILE_END("mcp_json_create_request");
    return final_json_string; // Return malloc'd string or NULL
}


char* mcp_json_create_response(uint64_t id, const char* result) {
    PROFILE_START("mcp_json_create_response");

    mcp_json_t* response = mcp_json_object_create(); // Uses arena
    if (response == NULL) {
        PROFILE_END("mcp_json_create_response");
        return NULL;
    }

    mcp_json_t* jsonrpc = mcp_json_string_create("2.0"); // Uses arena
    mcp_json_t* id_node = mcp_json_number_create((double)id); // Uses arena
    mcp_json_t* result_node = NULL;

    if (result != NULL) {
        result_node = mcp_json_parse(result); // Uses arena
        if (result_node == NULL) {
            mcp_log_warn("Invalid JSON provided for response result, defaulting to null: %s", result);
            result_node = mcp_json_null_create(); // Fallback to null if parse fails (uses arena)
        }
    } else {
        result_node = mcp_json_null_create(); // Create JSON null if result string is NULL (uses arena)
    }

    char* final_json_string = NULL;

    if (jsonrpc && id_node && result_node) {
        mcp_json_object_set_property(response, "jsonrpc", jsonrpc);
        mcp_json_object_set_property(response, "id", id_node);
        mcp_json_object_set_property(response, "result", result_node);
        final_json_string = mcp_json_stringify(response); // Uses malloc
    } else {
         mcp_log_error("Failed to create nodes for JSON-RPC response.");
         mcp_json_destroy(response); // Clean up arena nodes if creation failed
    }

    PROFILE_END("mcp_json_create_response");
    return final_json_string;
}


char* mcp_json_create_error_response(uint64_t id, int error_code, const char* error_message) {
     PROFILE_START("mcp_json_create_error_response");

     mcp_json_t* response = mcp_json_object_create(); // Uses arena
     if (response == NULL) {
         PROFILE_END("mcp_json_create_error_response");
         return NULL;
     }

     mcp_json_t* jsonrpc = mcp_json_string_create("2.0"); // Uses arena
     mcp_json_t* id_node = mcp_json_number_create((double)id); // Uses arena
     mcp_json_t* error_obj = mcp_json_object_create(); // Uses arena
     mcp_json_t* code_node = mcp_json_number_create((double)error_code); // Uses arena
     // Use empty string if message is NULL
     mcp_json_t* msg_node = mcp_json_string_create(error_message ? error_message : ""); // Uses arena

     char* final_json_string = NULL;

     if (jsonrpc && id_node && error_obj && code_node && msg_node) {
         mcp_json_object_set_property(error_obj, "code", code_node);
         mcp_json_object_set_property(error_obj, "message", msg_node);
         mcp_json_object_set_property(response, "jsonrpc", jsonrpc);
         mcp_json_object_set_property(response, "id", id_node);
         mcp_json_object_set_property(response, "error", error_obj);
         final_json_string = mcp_json_stringify(response); // Uses malloc
     } else {
          mcp_log_error("Failed to create nodes for JSON-RPC error response.");
          mcp_json_destroy(response); // Clean up arena nodes if creation failed
     }

     PROFILE_END("mcp_json_create_error_response");
     return final_json_string;
}
