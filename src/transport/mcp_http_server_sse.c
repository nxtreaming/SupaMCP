#ifdef _WIN32
#include "win_socket_compat.h"
#endif

#include "internal/http_transport_internal.h"
#include "mcp_http_sse_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Buffer size constants
#define SSE_ID_BUFFER_SIZE 32         /**< Maximum size for event ID strings */

/**
 * @brief Free all memory associated with a stored SSE event
 *
 * This function safely frees all dynamically allocated memory in an SSE event
 * structure and resets its fields to prevent use-after-free issues.
 *
 * @param event Pointer to the event structure to free
 */
static void free_stored_event(sse_event_t* event) {
    if (event == NULL) {
        return;
    }

    // Use sse_event_clear since the event is part of a fixed-size array
    sse_event_clear(event);
}

/**
 * @brief Allocate and store event fields in the circular buffer
 *
 * This function allocates memory for and stores an SSE event in the circular buffer.
 * It handles all necessary memory management and error conditions.
 *
 * @param index Index in the circular buffer
 * @param data Transport data
 * @param event_id Event ID string
 * @param event_type Event type (can be NULL)
 * @param event_data Event data
 * @return bool true if successful, false on parameter or memory allocation failure
 */
static bool allocate_event_fields(int index, http_transport_data_t* data,
                                 const char* event_id, const char* event_type,
                                 const char* event_data) {
    if (data == NULL || event_id == NULL || event_data == NULL ||
        index < 0 || index >= MAX_SSE_STORED_EVENTS) {
        mcp_log_error("Invalid parameters for allocate_event_fields");
        return false;
    }

    data->stored_events[index].id = NULL;
    data->stored_events[index].event = NULL;
    data->stored_events[index].data = NULL;
    data->stored_events[index].timestamp = 0;

    data->stored_events[index].id = mcp_strdup(event_id);
    if (data->stored_events[index].id == NULL) {
        mcp_log_error("Failed to allocate memory for event ID");
        goto cleanup_error;
    }

    // Store the event type (if provided)
    if (event_type != NULL) {
        data->stored_events[index].event = mcp_strdup(event_type);
        if (data->stored_events[index].event == NULL) {
            mcp_log_error("Failed to allocate memory for event type");
            goto cleanup_error;
        }
    }

    // Store the event data
    data->stored_events[index].data = mcp_strdup(event_data);
    if (data->stored_events[index].data == NULL) {
        mcp_log_error("Failed to allocate memory for event data");
        goto cleanup_error;
    }

    // Set the timestamp
    data->stored_events[index].timestamp = time(NULL);
    
    return true;

cleanup_error:
    free_stored_event(&data->stored_events[index]);
    return false;
}

/**
 * @brief Store an SSE event for replay on reconnection (using circular buffer)
 *
 * This function stores an event in a circular buffer for replay to clients that
 * reconnect with a Last-Event-ID. It manages the circular buffer, handling
 * overflow by replacing the oldest event when the buffer is full.
 *
 * @param data Transport data containing the event circular buffer
 * @param event Event type (can be NULL for default events)
 * @param event_data Event data payload (must not be NULL)
 */
void store_sse_event(http_transport_data_t* data, const char* event, const char* event_data) {
    if (data == NULL || event_data == NULL) {
        mcp_log_error("Invalid parameters for store_sse_event");
        return;
    }

    if (!is_valid_sse_text(event_data)) {
        mcp_log_error("Invalid characters in SSE event data");
        return;
    }

    if (!is_valid_sse_text(event)) {
        mcp_log_error("Invalid characters in SSE event type");
        return;
    }

    mcp_mutex_lock(data->event_mutex);

    int event_id = data->next_event_id++;

    // Format the event ID as a string
    char id_str[SSE_ID_BUFFER_SIZE];
    int id_len = snprintf(id_str, sizeof(id_str), "%d", event_id);
    if (id_len < 0 || id_len >= (int)sizeof(id_str)) {
        mcp_log_error("Event ID buffer overflow: ID %d exceeds buffer size", event_id);
        mcp_mutex_unlock(data->event_mutex);
        return;
    }

    // Handle circular buffer management
    if (data->stored_event_count == MAX_SSE_STORED_EVENTS) {
        // Buffer is full - replace oldest event
        mcp_log_debug("Circular buffer full (%d events), replacing oldest event at position %d",
                     MAX_SSE_STORED_EVENTS, data->event_head);

        // Free the oldest event's memory (at head position)
        free_stored_event(&data->stored_events[data->event_head]);

        // Move head forward (oldest event position)
        data->event_head = (data->event_head + 1) % MAX_SSE_STORED_EVENTS;

        // Count stays the same as we're replacing an event
    } else {
        // Buffer has space - increment count
        data->stored_event_count++;
        mcp_log_debug("Adding event to circular buffer, new count: %d/%d",
                     data->stored_event_count, MAX_SSE_STORED_EVENTS);
    }

    // Add the new event at the tail position
    int index = data->event_tail;

    // Allocate and store the event fields
    if (!allocate_event_fields(index, data, id_str, event, event_data)) {
        mcp_log_error("Failed to allocate event fields, event not stored");
        mcp_mutex_unlock(data->event_mutex);
        return;
    }

    // Move tail forward (position for next event)
    data->event_tail = (data->event_tail + 1) % MAX_SSE_STORED_EVENTS;

    mcp_log_debug("Stored SSE event: id=%s, type=%s, data_length=%zu",
                 id_str, event ? event : "(default)", strlen(event_data));

    mcp_mutex_unlock(data->event_mutex);
}

/**
 * @brief Send a heartbeat to all SSE clients
 *
 * This function sends a heartbeat comment to all connected SSE clients
 * to keep the connections alive. Heartbeats are sent at the configured interval
 * and help prevent connection timeouts, especially with proxies and firewalls.
 *
 * @param data Transport data containing SSE client information
 */
void send_sse_heartbeat(http_transport_data_t* data) {
    if (data == NULL) {
        mcp_log_error("Invalid parameters for send_sse_heartbeat");
        return;
    }

    // Check if heartbeats are enabled in configuration
    if (!data->send_heartbeats) {
        return;
    }

    int heartbeat_interval_sec = data->heartbeat_interval_ms / 1000;
    // Ensure minimum interval of 1 second
    if (heartbeat_interval_sec <= 0) {
        heartbeat_interval_sec = 1;
    }

    // Check if it's time for a heartbeat based on configured interval
    time_t now = time(NULL);
    // Skip if not enough time has passed since last heartbeat
    if (now - data->last_heartbeat < heartbeat_interval_sec) {
        return;
    }

    // Update heartbeat tracking information
    data->last_heartbeat = now;
    data->last_heartbeat_time = now;
    data->heartbeat_counter++;

    // Lock the SSE mutex to safely access the client list
    mcp_mutex_lock(data->sse_mutex);

    // Skip if no clients are connected
    if (data->sse_client_count <= 0) {
        mcp_log_debug("No SSE clients connected, skipping heartbeat");
        mcp_mutex_unlock(data->sse_mutex);
        return;
    }

    mcp_log_debug("Sending heartbeat #%llu to %d SSE clients (last heartbeat: %lld seconds ago)",
                 (unsigned long long)data->heartbeat_counter + 1,
                 data->sse_client_count,
                 (long long)(now - data->last_heartbeat_time));

    // Send heartbeat to all connected clients
    int success_count = 0;
    for (int i = 0; i < data->sse_client_count; i++) {
        struct lws* wsi = data->sse_clients[i];
        if (wsi == NULL) {
            continue;
        }

        // Construct heartbeat message with counter
        char heartbeat_msg[64];
        int msg_len = snprintf(heartbeat_msg, sizeof(heartbeat_msg),
                              ": heartbeat %llu\n\n",
                              (unsigned long long)data->heartbeat_counter);

        // Validate the constructed message (should always be valid, but check anyway)
        if (msg_len < 0 || msg_len >= (int)sizeof(heartbeat_msg) || !is_valid_sse_text(heartbeat_msg)) {
            mcp_log_error("Failed to construct valid heartbeat message");
            continue;
        }

        // Send a comment as a heartbeat (using SSE comment format)
        // This will not trigger an event in the client but keeps the connection alive
        int result = lws_write_http(wsi, heartbeat_msg, msg_len);
        if (result < 0) {
            mcp_log_warn("Failed to send heartbeat to SSE client %d", i);
        } else {
            success_count++;

            // Request a callback when the socket is writable again
            // This ensures libwebsockets will flush the data properly
            lws_callback_on_writable(wsi);
        }
    }

    mcp_log_debug("Heartbeat #%llu sent successfully to %d/%d SSE clients",
                 (unsigned long long)data->heartbeat_counter,
                 success_count, data->sse_client_count);

    mcp_mutex_unlock(data->sse_mutex);
}

/**
 * @brief Check if a client session matches the requested session ID
 *
 * This function determines if an SSE client session matches a specified session ID.
 * It supports both exact and case-insensitive matching, and handles NULL values
 * appropriately.
 *
 * If no session_id is specified (broadcast), only clients without a session_id will
 * receive the message. Clients with a session_id will only receive messages specifically
 * targeted to their session_id.
 *
 * @param session Session data (can be NULL)
 * @param session_id Requested session ID (can be NULL for broadcast)
 * @return bool true if the session matches the criteria, false otherwise
 */
static bool session_matches_id(http_session_data_t* session, const char* session_id) {
    // If no specific session ID requested, this is a broadcast
    // Only match clients WITHOUT a session ID
    if (session_id == NULL) {
        // If client has no session data or no session ID, it should receive broadcasts
        if (session == NULL || session->session_id == NULL) {
            return true;
        }

        // Validate client session ID
        if (!is_valid_sse_text(session->session_id)) {
            mcp_log_warn("Client has invalid characters in session ID");
            return false;
        }

        // Client has a session ID, so it should NOT receive broadcasts
        mcp_log_debug("Client has session_id (%s) and will not receive broadcast messages",
                     session->session_id);
        return false;
    }

    // If client has no session data, it can't match a specific session ID
    if (session == NULL) {
        mcp_log_debug("Client has no session data but filter requires session_id: %s", session_id);
        return false;
    }

    // If client session has no ID, it can't match a specific session ID
    if (session->session_id == NULL) {
        mcp_log_debug("Client session has NULL session_id but filter requires: %s", session_id);
        return false;
    }

    if (!is_valid_sse_text(session->session_id)) {
        mcp_log_warn("Client has invalid characters in session ID");
        return false;
    }

    // First try exact match (faster)
    if (strcmp(session->session_id, session_id) == 0) {
        return true;
    }

    // Then try case-insensitive match (for robustness)
    if (strcasecmp(session->session_id, session_id) == 0) {
        mcp_log_debug("Session IDs match with case-insensitive comparison: %s", session_id);
        return true;
    }

    mcp_log_debug("Session ID mismatch - requested: %s, client has: %s",
                 session_id, session->session_id);
    return false;
}

/**
 * @brief Check if a client session matches the event filter
 *
 * This function determines if an event should be sent to a client based on
 * the client's event filter settings. Clients can subscribe to specific event
 * types or receive all events.
 *
 * @param session Session data (can be NULL)
 * @param event Event type (can be NULL for default events)
 * @return bool true if the event should be sent to this client, false otherwise
 */
static bool session_matches_filter(http_session_data_t* session, const char* event) {
    // If client has no session data or no event filter, send all events
    if (session == NULL || session->event_filter == NULL) {
        return true;
    }

    // Validate client event filter
    if (!is_valid_sse_text(session->event_filter)) {
        mcp_log_warn("Client has invalid characters in event filter");
        return false;
    }

    // If this is a default event (no type specified), send to all clients
    if (event == NULL) {
        return true;
    }

    // Check if the event type matches the client's filter
    if (strcmp(session->event_filter, event) != 0) {
        // For detailed logging in debug mode
        mcp_log_debug("Event type mismatch - client filter: %s, event type: %s",
                     session->event_filter, event);
        return false;
    }

    // Event type matches client's filter
    return true;
}

/**
 * @brief Write an SSE field with proper error handling
 *
 * @param wsi WebSocket instance
 * @param field Field name (e.g., "event: ", "id: ")
 * @param value Field value (can be NULL)
 * @return bool true if all writes succeeded, false otherwise
 */
static bool write_sse_field(struct lws* wsi, const char* field, const char* value) {
    if (!wsi || !field) {
        return false;
    }

    if (!is_valid_sse_text(value)) {
        mcp_log_error("Invalid characters in SSE field value");
        return false;
    }

    if (lws_write_http(wsi, field, strlen(field)) < 0) {
        return false;
    }

    if (value && lws_write_http(wsi, value, strlen(value)) < 0) {
        return false;
    }

    return lws_write_http(wsi, "\n", 1) >= 0;
}

/**
 * @brief Send an SSE event to a client
 *
 * This function sends a properly formatted Server-Sent Event (SSE) to a client.
 * It follows the SSE protocol format, writing the event in chunks to avoid any
 * heap allocations. The function handles both named events and default events.
 *
 * SSE Format:
 * - Named event: "event: [event_type]\nid: [id]\ndata: [data]\n\n"
 * - Default event: "id: [id]\ndata: [data]\n\n"
 *
 * @param wsi WebSocket instance representing the client connection
 * @param event Event type (can be NULL for default events)
 * @param data Event data payload
 * @param id_str Event ID string
 * @return bool true if the event was sent successfully, false otherwise
 */
static bool send_sse_event_to_client(struct lws* wsi, const char* event,
                                     const char* data, const char* id_str) {
    if (wsi == NULL || data == NULL || id_str == NULL) {
        mcp_log_error("Invalid parameters for send_sse_event_to_client");
        return false;
    }

    // Validate event data and ID (these should already be validated, but double-check)
    if (!is_valid_sse_text(data) || !is_valid_sse_text(id_str)) {
        mcp_log_error("Invalid characters in SSE event data or ID");
        return false;
    }

    // Validate event type if provided
    if (!is_valid_sse_text(event)) {
        mcp_log_error("Invalid characters in SSE event type");
        return false;
    }

    int result = 0;

    // Different format based on whether an event type is specified
    if (event != NULL) {
        // Named event format
        // Write in multiple pieces to avoid any heap allocation

        // Step 1: Write event field ("event: [event_type]\n")
        result = write_sse_field(wsi, SSE_FIELD_EVENT, event);
        if (!result) {
            mcp_log_error("Failed to write event field");
            return false;
        }

        // Step 2: Write ID field ("id: [id]\n")
        result = write_sse_field(wsi, SSE_FIELD_ID, id_str);
        if (!result) {
            mcp_log_error("Failed to write id field");
            return false;
        }

        // Step 3: Write data field ("data: [data]\n\n")
        result = write_sse_field(wsi, SSE_FIELD_DATA, data);
        if (!result) {
            mcp_log_error("Failed to write data field");
            return false;
        }

        // End with double newline to complete the event
        result = lws_write_http(wsi, "\n", 1);
        if (result < 0) {
            mcp_log_error("Failed to write final newlines");
            return false;
        }
    } else {
        // Default event format (no event type)

        // Step 1: Write ID field ("id: [id]\n")
        result = write_sse_field(wsi, SSE_FIELD_ID, id_str);
        if (!result) {
            mcp_log_error("Failed to write id field");
            return false;
        }

        // Step 2: Write data field ("data: [data]\n\n")
        result = write_sse_field(wsi, SSE_FIELD_DATA, data);
        if (!result) {
            mcp_log_error("Failed to write data field");
            return false;
        }

        // End with double newline to complete the event
        result = lws_write_http(wsi, "\n", 1);
        if (result < 0) {
            mcp_log_error("Failed to write final newlines");
            return false;
        }
    }

    // Request a callback when the socket is writable again
    // This ensures that libwebsockets will flush the data
    lws_callback_on_writable(wsi);

    return true;
}

/**
 * @brief Send SSE event to a specific session or all connected clients
 *
 * This function sends an SSE event to one or more connected clients,
 * optionally filtering by session ID. It handles storing the event for
 * replay on reconnection, client filtering, and delivery to matching clients.
 *
 * @param transport Transport instance
 * @param event Event type (can be NULL for default events)
 * @param data Event data payload (must not be NULL)
 * @param session_id Session ID to filter by (can be NULL for broadcast to all clients)
 * @return int 0 on success, -1 on failure
 */
int mcp_http_transport_send_sse(mcp_transport_t* transport, const char* event,
                                const char* data, const char* session_id) {
    if (transport == NULL || transport->transport_data == NULL || data == NULL) {
        mcp_log_error("Invalid parameters for mcp_http_transport_send_sse");
        return -1;
    }

    if (!is_valid_sse_text(data)) {
        mcp_log_error("Invalid characters in SSE event data");
        return -1;
    }

    if (!is_valid_sse_text(event)) {
        mcp_log_error("Invalid characters in SSE event type");
        return -1;
    }

    if (!is_valid_sse_text(session_id)) {
        mcp_log_error("Invalid characters in SSE session ID");
        return -1;
    }

    http_transport_data_t* transport_data = (http_transport_data_t*)transport->transport_data;

    // Store the event for replay on reconnection (for clients that join later)
    store_sse_event(transport_data, event, data);

    // Get the current event ID (already incremented in store_sse_event)
    int event_id = transport_data->next_event_id - 1;

    // Format the event ID as a string for SSE protocol
    char id_str[SSE_ID_BUFFER_SIZE];
    int id_len = snprintf(id_str, sizeof(id_str), "%d", event_id);
    if (id_len < 0 || id_len >= (int)sizeof(id_str)) {
        mcp_log_error("Event ID buffer overflow: ID %d exceeds buffer size", event_id);
        return -1;
    }

    // Log the event details for debugging
    mcp_log_debug("Sending SSE event: id=%s, type=%s, data_length=%zu, target=%s",
                 id_str, event ? event : "(default)", strlen(data),
                 session_id ? session_id : "broadcast");

    // Lock the SSE mutex to safely access the client list
    mcp_mutex_lock(transport_data->sse_mutex);

    // Early return if no clients are connected
    if (transport_data->sse_client_count <= 0) {
        mcp_log_warn("No SSE clients connected, event will be stored but not delivered");
        mcp_mutex_unlock(transport_data->sse_mutex);
        return 0; // Not an error condition, just no clients to deliver to
    }

    // Track delivery statistics
    int matched_clients = 0;
    int success_count = 0;

    // Iterate through all connected clients
    for (int i = 0; i < transport_data->sse_client_count; i++) {
        struct lws* wsi = transport_data->sse_clients[i];
        if (wsi == NULL) {
            continue;
        }

        // Get session data to check for event filter and session ID
        http_session_data_t* session = (http_session_data_t*)lws_wsi_user(wsi);

        // Apply filtering logic
        // 1. Check if client is subscribed to this event type
        if (!session_matches_filter(session, event)) {
            continue;
        }

        // 2. Check if client matches the requested session ID
        if (!session_matches_id(session, session_id)) {
            continue;
        }

        // Client matches all criteria - count it
        matched_clients++;

        // Send the event to this client
        if (send_sse_event_to_client(wsi, event, data, id_str)) {
            // Success - update counters and client state
            success_count++;

            // Update the client's last event ID for reconnection support
            if (session != NULL) {
                session->last_event_id = event_id;
            }
        } else {
            mcp_log_error("Failed to send SSE event to client %d", i);
            // Continue with other clients even if this one failed
        }
    }

    // Release the mutex as soon as possible
    mcp_mutex_unlock(transport_data->sse_mutex);

    // Log appropriate summary based on delivery mode and results
    if (session_id != NULL) {
        // Targeted delivery to specific session
        if (matched_clients > 0) {
            mcp_log_info("Successfully sent SSE event to %d/%d client(s) with session_id: %s",
                        success_count, matched_clients, session_id);
        } else {
            mcp_log_warn("No SSE clients matched the requested session_id: %s", session_id);
        }
    } else {
        // Broadcast delivery (only to clients without session_id)
        mcp_log_info("Successfully sent SSE event to %d/%d client(s) (broadcast to clients without session_id)",
                    success_count, matched_clients);
    }

    return 0;
}
