#include "internal/json_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mcp_string_utils.h"
#include <inttypes.h>

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


// Rewritten stringify function using dynamic buffer
char* mcp_json_stringify_message(const mcp_message_t* message) {
    if (message == NULL) {
        return NULL;
    }
    PROFILE_START("mcp_json_stringify_message");

    dyn_buf_t db;
    // Estimate initial size (can be tuned)
    if (dyn_buf_init(&db, 256) != 0) {
        mcp_log_error("Failed to initialize dynamic buffer for stringify.");
        PROFILE_END("mcp_json_stringify_message");
        return NULL;
    }

    // Common parts
    dyn_buf_append(&db, "{\"jsonrpc\":\"2.0\","); // Assume success for core parts

    char id_buf[32]; // Buffer for integer ID stringification

    switch (message->type) {
        case MCP_MESSAGE_TYPE_REQUEST:
            // Append ID
            snprintf(id_buf, sizeof(id_buf), "%" PRIu64, message->request.id);
            dyn_buf_append(&db, "\"id\":");
            dyn_buf_append(&db, id_buf);
            dyn_buf_append(&db, ",");

            // Append Method
            dyn_buf_append(&db, "\"method\":");
            dyn_buf_append_json_string(&db, message->request.method ? message->request.method : "");

            // Append Params (if they exist - assume params is already a valid JSON string or NULL)
            if (message->request.params != NULL) {
                dyn_buf_append(&db, ",\"params\":");
                dyn_buf_append(&db, (const char*)message->request.params); // Append raw JSON string
            }
            break;

        case MCP_MESSAGE_TYPE_RESPONSE:
            // Append ID
            snprintf(id_buf, sizeof(id_buf), "%" PRIu64, message->response.id);
            dyn_buf_append(&db, "\"id\":");
            dyn_buf_append(&db, id_buf);

            if (message->response.error_code != MCP_ERROR_NONE) { // Error response
                dyn_buf_append(&db, ",\"error\":{\"code\":");
                snprintf(id_buf, sizeof(id_buf), "%d", message->response.error_code); // Use %d for error code
                dyn_buf_append(&db, id_buf);
                dyn_buf_append(&db, ",\"message\":");
                dyn_buf_append_json_string(&db, message->response.error_message ? message->response.error_message : "");
                dyn_buf_append(&db, "}"); // Close error object
            } else { // Success response
                dyn_buf_append(&db, ",\"result\":");
                if (message->response.result != NULL) {
                    dyn_buf_append(&db, (const char*)message->response.result); // Append raw JSON string
                } else {
                    dyn_buf_append(&db, "null");
                }
            }
            break;

        case MCP_MESSAGE_TYPE_NOTIFICATION:
            // Append Method
            dyn_buf_append(&db, "\"method\":");
            dyn_buf_append_json_string(&db, message->notification.method ? message->notification.method : "");

            // Append Params (if they exist - assume params is already a valid JSON string or NULL)
            if (message->notification.params != NULL) {
                dyn_buf_append(&db, ",\"params\":");
                dyn_buf_append(&db, (const char*)message->notification.params); // Append raw JSON string
            }
            break;

        default:
            mcp_log_error("Invalid message type encountered during stringify.");
            dyn_buf_free(&db); // Clean up buffer
            PROFILE_END("mcp_json_stringify_message");
            return NULL; // Indicate error
    }

    dyn_buf_append(&db, "}"); // Close root object

    // Finalize buffer (transfers ownership of buffer to final_json_string)
    char* final_json_string = dyn_buf_finalize(&db);

    // dyn_buf_free(&db); // Not needed after finalize

    PROFILE_END("mcp_json_stringify_message");
    return final_json_string; // Return the malloc'd final string
}


// --- Creation Functions --- (Keep original versions for now)

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


// --- Batch Parsing Implementation ---

// Internal helper to parse a single JSON object within a batch
static int parse_single_message_from_json(mcp_json_t* json, mcp_message_t* message) {
    // This function assumes 'json' is a valid JSON object node from the arena
    // It reuses the logic from the original mcp_json_parse_message,
    // but operates on an already parsed mcp_json_t* instead of a string.

    if (json == NULL || json->type != MCP_JSON_OBJECT || message == NULL) {
        return -1;
    }

    mcp_json_t* id = mcp_json_object_get_property(json, "id");
    mcp_json_t* method = mcp_json_object_get_property(json, "method");
    mcp_json_t* params = mcp_json_object_get_property(json, "params");
    mcp_json_t* result = mcp_json_object_get_property(json, "result");
    mcp_json_t* error = mcp_json_object_get_property(json, "error");

    int parse_status = -1;
    message->type = MCP_MESSAGE_TYPE_INVALID;

    // --- Request Check ---
    if (method != NULL && method->type == MCP_JSON_STRING) {
        if (id != NULL) { // Request
            if (id->type == MCP_JSON_NUMBER) {
                if (params == NULL || params->type == MCP_JSON_OBJECT || params->type == MCP_JSON_ARRAY) {
                    message->type = MCP_MESSAGE_TYPE_REQUEST;
                    message->request.id = (uint64_t)id->number_value;
                    message->request.method = mcp_strdup(method->string_value);
                    message->request.params = (params != NULL) ? mcp_json_stringify(params) : NULL; // Still uses malloc
                    if (message->request.method != NULL && (params == NULL || message->request.params != NULL)) {
                        parse_status = 0;
                    } else {
                        free(message->request.method); free(message->request.params);
                        message->type = MCP_MESSAGE_TYPE_INVALID;
                    }
                }
            }
        } else { // Notification
            if (params == NULL || params->type == MCP_JSON_OBJECT || params->type == MCP_JSON_ARRAY) {
                message->type = MCP_MESSAGE_TYPE_NOTIFICATION;
                message->notification.method = mcp_strdup(method->string_value);
                message->notification.params = (params != NULL) ? mcp_json_stringify(params) : NULL; // Still uses malloc
                if (message->notification.method != NULL && (params == NULL || message->notification.params != NULL)) {
                    parse_status = 0;
                } else {
                    free(message->notification.method); free(message->notification.params);
                    message->type = MCP_MESSAGE_TYPE_INVALID;
                }
            }
        }
    // --- Response Check ---
    } else if (id != NULL && (result != NULL || error != NULL)) { // Response
        if (id->type == MCP_JSON_NUMBER) {
            message->type = MCP_MESSAGE_TYPE_RESPONSE;
            message->response.id = (uint64_t)id->number_value;
            if (error != NULL && error->type == MCP_JSON_OBJECT) { // Error Response
                if (result != NULL) { message->type = MCP_MESSAGE_TYPE_INVALID; }
                else {
                    mcp_json_t* code = mcp_json_object_get_property(error, "code");
                    mcp_json_t* msg = mcp_json_object_get_property(error, "message");
                    if (code != NULL && code->type == MCP_JSON_NUMBER && msg != NULL && msg->type == MCP_JSON_STRING) {
                        message->response.error_code = (mcp_error_code_t)(int)code->number_value;
                        message->response.error_message = mcp_strdup(msg->string_value);
                        message->response.result = NULL;
                        if (message->response.error_message != NULL) { parse_status = 0; }
                        else { message->type = MCP_MESSAGE_TYPE_INVALID; }
                    } else { message->type = MCP_MESSAGE_TYPE_INVALID; }
                }
            } else if (result != NULL) { // Success Response
                message->response.error_code = MCP_ERROR_NONE;
                message->response.error_message = NULL;
                message->response.result = mcp_json_stringify(result); // Still uses malloc
                if (message->response.result != NULL) { parse_status = 0; }
                else { message->type = MCP_MESSAGE_TYPE_INVALID; }
            } else { message->type = MCP_MESSAGE_TYPE_INVALID; }
        } else { message->type = MCP_MESSAGE_TYPE_INVALID; }
    } else { message->type = MCP_MESSAGE_TYPE_INVALID; }

    // If parsing failed for this specific message, ensure type is invalid
    if (parse_status != 0) {
        message->type = MCP_MESSAGE_TYPE_INVALID;
        // Don't free contents here, let the main loop or free_message_array handle it
    }

    return parse_status; // Return 0 on success for this message, -1 on failure
}


int mcp_json_parse_message_or_batch(const char* json_str, mcp_message_t** messages, size_t* count) {
    if (json_str == NULL || messages == NULL || count == NULL) {
        return -1;
    }
    *messages = NULL;
    *count = 0;

    PROFILE_START("mcp_json_parse_batch");

    // Parse the input string using the thread-local arena
    mcp_json_t* root_json = mcp_json_parse(json_str);
    if (root_json == NULL) {
        mcp_log_error("Batch parse error: Invalid root JSON.");
        PROFILE_END("mcp_json_parse_batch");
        return MCP_ERROR_PARSE_ERROR; // Use defined error code
    }

    int result = 0; // Success by default

    if (root_json->type == MCP_JSON_OBJECT) {
        // --- Single Message ---
        *messages = (mcp_message_t*)malloc(sizeof(mcp_message_t));
        if (*messages == NULL) {
            mcp_log_error("Batch parse error: Failed to allocate memory for single message.");
            result = MCP_ERROR_INTERNAL_ERROR; // Allocation error
        } else {
            // Parse the single object
            if (parse_single_message_from_json(root_json, *messages) == 0) {
                *count = 1;
                result = 0; // Success
            } else {
                // Parsing the single object failed (invalid format)
                mcp_log_error("Batch parse error: Invalid single message format.");
                free(*messages); // Free the allocated message struct
                *messages = NULL;
                result = MCP_ERROR_INVALID_REQUEST; // Invalid message structure
            }
        }
    } else if (root_json->type == MCP_JSON_ARRAY) {
        // --- Batch Message ---
        size_t batch_size = mcp_json_array_get_size(root_json);
        if (batch_size == 0) {
            // Empty batch is technically valid according to JSON-RPC 2.0, but maybe an error for MCP?
            // Let's treat it as an invalid request for now.
            mcp_log_error("Batch parse error: Received empty batch array.");
            result = MCP_ERROR_INVALID_REQUEST;
        } else {
            *messages = (mcp_message_t*)malloc(batch_size * sizeof(mcp_message_t));
            if (*messages == NULL) {
                mcp_log_error("Batch parse error: Failed to allocate memory for message array.");
                result = MCP_ERROR_INTERNAL_ERROR; // Allocation error
            } else {
                *count = 0; // Track successfully parsed messages
                bool parse_error_occurred = false;
                for (size_t i = 0; i < batch_size; ++i) {
                    mcp_json_t* item_json = mcp_json_array_get_item(root_json, (int)i);
                    // Initialize message struct before parsing
                    memset(&(*messages)[i], 0, sizeof(mcp_message_t));
                    (*messages)[i].type = MCP_MESSAGE_TYPE_INVALID; // Default

                    if (item_json != NULL && item_json->type == MCP_JSON_OBJECT) {
                        // Parse the individual message object
                        if (parse_single_message_from_json(item_json, &(*messages)[i]) == 0) {
                            (*count)++; // Increment count only if parsing this item succeeded
                        } else {
                            // Parsing failed for this item, mark as invalid but continue processing batch
                            mcp_log_warn("Batch parse warning: Invalid message format at index %zu.", i);
                            // Ensure contents are released for the failed item
                            mcp_message_release_contents(&(*messages)[i]);
                            (*messages)[i].type = MCP_MESSAGE_TYPE_INVALID; // Ensure type is invalid
                            parse_error_occurred = true; // Flag that at least one item failed
                        }
                    } else {
                        // Item in batch is not a JSON object
                        mcp_log_warn("Batch parse warning: Item at index %zu is not a JSON object.", i);
                        (*messages)[i].type = MCP_MESSAGE_TYPE_INVALID; // Mark as invalid
                        parse_error_occurred = true;
                    }
                }
                 // If any item failed parsing, the overall result might be considered an error,
                 // but we still return the partially parsed array. Let the caller decide.
                 // For now, return success if at least one message was parsed, otherwise error.
                 // result = (*count > 0) ? 0 : MCP_ERROR_INVALID_REQUEST;
                 // Let's return success even if some items failed, caller checks individual types.
                 result = 0;
            }
        }
    } else {
        // Root is not Object or Array
        mcp_log_error("Batch parse error: Root JSON is not an object or array.");
        result = MCP_ERROR_INVALID_REQUEST;
    }

    // Arena cleanup is handled by the caller
    PROFILE_END("mcp_json_parse_batch");
    return result;
}

void mcp_json_free_message_array(mcp_message_t* messages, size_t count) {
    if (messages == NULL) {
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        // Release internal malloc'd strings (method, params, result, error_message)
        mcp_message_release_contents(&messages[i]);
    }
    // Free the array itself
    free(messages);
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
