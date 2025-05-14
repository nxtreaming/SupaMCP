#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif

#include <win_socket_compat.h>

// On Windows, strcasecmp is _stricmp
#define strcasecmp _stricmp
#endif

#include "internal/http_transport_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// SSE event fields
#define SSE_FIELD_EVENT "event: "
#define SSE_FIELD_ID "id: "
#define SSE_FIELD_DATA "data: "
#define SSE_FIELD_HEARTBEAT ": heartbeat\n\n"

// Buffer sizes
#define SSE_ID_BUFFER_SIZE 32

// Forward declarations
static void free_stored_event(sse_event_t* event);
static bool allocate_event_fields(int index, http_transport_data_t* data,
                                 const char* event_id, const char* event_type,
                                 const char* event_data);

/**
 * @brief Free all memory associated with a stored SSE event
 *
 * @param event The event to free
 */
static void free_stored_event(sse_event_t* event) {
    if (event == NULL) {
        return;
    }

    if (event->id != NULL) {
        free(event->id);
        event->id = NULL;
    }

    if (event->event_type != NULL) {
        free(event->event_type);
        event->event_type = NULL;
    }

    if (event->data != NULL) {
        free(event->data);
        event->data = NULL;
    }

    // Reset timestamp
    event->timestamp = 0;
}

/**
 * @brief Allocate and store event fields in the circular buffer
 *
 * @param index Index in the circular buffer
 * @param data Transport data
 * @param event_id Event ID string
 * @param event_type Event type (can be NULL)
 * @param event_data Event data
 * @return bool true if successful, false on memory allocation failure
 */
static bool allocate_event_fields(int index, http_transport_data_t* data,
                                 const char* event_id, const char* event_type,
                                 const char* event_data) {
    if (data == NULL || event_id == NULL || event_data == NULL ||
        index < 0 || index >= MAX_SSE_STORED_EVENTS) {
        return false;
    }

    // Store the event ID
    data->stored_events[index].id = mcp_strdup(event_id);
    if (data->stored_events[index].id == NULL) {
        mcp_log_error("Failed to allocate memory for event ID");
        return false;
    }

    // Store the event type (if provided)
    if (event_type != NULL) {
        data->stored_events[index].event_type = mcp_strdup(event_type);
        if (data->stored_events[index].event_type == NULL) {
            mcp_log_error("Failed to allocate memory for event type");
            free(data->stored_events[index].id);
            data->stored_events[index].id = NULL;
            return false;
        }
    } else {
        data->stored_events[index].event_type = NULL;
    }

    // Store the event data
    data->stored_events[index].data = mcp_strdup(event_data);
    if (data->stored_events[index].data == NULL) {
        mcp_log_error("Failed to allocate memory for event data");
        free(data->stored_events[index].id);
        data->stored_events[index].id = NULL;
        if (data->stored_events[index].event_type != NULL) {
            free(data->stored_events[index].event_type);
            data->stored_events[index].event_type = NULL;
        }
        return false;
    }

    // Store the timestamp
    data->stored_events[index].timestamp = time(NULL);

    return true;
}

/**
 * @brief Store an SSE event for replay on reconnection (using circular buffer)
 *
 * This function stores an event in a circular buffer for replay to clients that
 * reconnect with a Last-Event-ID.
 *
 * @param data Transport data
 * @param event Event type (can be NULL)
 * @param event_data Event data
 */
void store_sse_event(http_transport_data_t* data, const char* event, const char* event_data) {
    // Validate input parameters
    if (data == NULL || event_data == NULL) {
        mcp_log_error("Invalid parameters for store_sse_event");
        return;
    }

    // Lock the event mutex to safely modify the circular buffer
    mcp_mutex_lock(data->event_mutex);

    // Get the current event ID
    int event_id = data->next_event_id++;

    // Format the event ID as a string
    char id_str[SSE_ID_BUFFER_SIZE];
    int id_len = snprintf(id_str, sizeof(id_str), "%d", event_id);
    if (id_len < 0 || id_len >= (int)sizeof(id_str)) {
        mcp_log_error("Event ID buffer overflow");
        mcp_mutex_unlock(data->event_mutex);
        return;
    }

    // If the buffer is full, free the oldest event's memory
    if (data->stored_event_count == MAX_SSE_STORED_EVENTS) {
        // Free the oldest event's memory (at head position)
        free_stored_event(&data->stored_events[data->event_head]);

        // Move head forward (oldest event position)
        data->event_head = (data->event_head + 1) % MAX_SSE_STORED_EVENTS;

        // Count stays the same as we're replacing an event
        mcp_log_debug("Circular buffer full, replacing oldest event at position %d", data->event_head);
    } else {
        // Buffer is not full, increment count
        data->stored_event_count++;
        mcp_log_debug("Adding event to circular buffer, count: %d", data->stored_event_count);
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
                 id_str, event ? event : "NULL", strlen(event_data));

    mcp_mutex_unlock(data->event_mutex);
}

/**
 * @brief Send a heartbeat to all SSE clients
 *
 * This function sends a heartbeat comment to all connected SSE clients
 * to keep the connections alive.
 *
 * @param data Transport data
 */
void send_sse_heartbeat(http_transport_data_t* data) {
    // Validate input parameters
    if (data == NULL) {
        mcp_log_error("Invalid parameters for send_sse_heartbeat");
        return;
    }

    // Check if heartbeats are enabled
    if (!data->send_heartbeats) {
        return;
    }

    // Check if it's time for a heartbeat
    time_t now = time(NULL);
    int heartbeat_interval_sec = data->heartbeat_interval_ms / 1000;
    if (heartbeat_interval_sec <= 0) {
        heartbeat_interval_sec = 1; // Minimum 1 second interval
    }

    if (now - data->last_heartbeat < heartbeat_interval_sec) {
        return; // Not time for a heartbeat yet
    }

    // Update last heartbeat time
    data->last_heartbeat = now;

    // Lock the SSE mutex to safely access the client list
    mcp_mutex_lock(data->sse_mutex);

    // Check if there are any connected clients
    if (data->sse_client_count <= 0) {
        mcp_log_debug("No SSE clients connected, skipping heartbeat");
        mcp_mutex_unlock(data->sse_mutex);
        return;
    }

    mcp_log_debug("Sending heartbeat to %d SSE clients", data->sse_client_count);

    // Send heartbeat to all clients
    int success_count = 0;
    for (int i = 0; i < data->sse_client_count; i++) {
        struct lws* wsi = data->sse_clients[i];
        if (wsi == NULL) {
            continue;
        }

        // Send a comment as a heartbeat (will not trigger an event in the client)
        int result = lws_write_http(wsi, SSE_FIELD_HEARTBEAT, strlen(SSE_FIELD_HEARTBEAT));
        if (result < 0) {
            mcp_log_warn("Failed to send heartbeat to SSE client %d", i);
        } else {
            success_count++;

            // Request a callback when the socket is writable again
            lws_callback_on_writable(wsi);
        }
    }

    mcp_log_debug("Heartbeat sent successfully to %d/%d SSE clients",
                 success_count, data->sse_client_count);

    mcp_mutex_unlock(data->sse_mutex);
}

/**
 * @brief Check if a client session matches the requested session ID
 *
 * @param session Session data
 * @param session_id Requested session ID
 * @return bool true if the session matches, false otherwise
 */
static bool session_matches_id(http_session_data_t* session, const char* session_id) {
    if (session_id == NULL) {
        return true; // No specific session ID requested, match all
    }

    // If session is NULL, no match
    if (session == NULL) {
        mcp_log_debug("Client has no session data but requested session_id: %s", session_id);
        return false;
    }

    // If session_id is NULL, no match
    if (session->session_id == NULL) {
        mcp_log_debug("Client session_id is NULL but requested session_id: %s", session_id);
        return false;
    }

    // Try exact match first
    if (strcmp(session->session_id, session_id) == 0) {
        return true;
    }

    // Try case-insensitive match
    if (strcasecmp(session->session_id, session_id) == 0) {
        mcp_log_debug("Session IDs match with case-insensitive comparison");
        return true;
    }

    // No match
    mcp_log_debug("Session ID mismatch - requested: %s, client: %s",
                 session_id, session->session_id);
    return false;
}

/**
 * @brief Check if a client session matches the event filter
 *
 * @param session Session data
 * @param event Event type
 * @return bool true if the session matches the filter, false otherwise
 */
static bool session_matches_filter(http_session_data_t* session, const char* event) {
    // If no session or no filter, match all
    if (session == NULL || session->event_filter == NULL) {
        return true;
    }

    // If no event specified, match all
    if (event == NULL) {
        return true;
    }

    // Check if the event matches the filter
    if (strcmp(session->event_filter, event) != 0) {
        mcp_log_debug("Event filter mismatch - filter: %s, event: %s",
                     session->event_filter, event);
        return false;
    }

    return true;
}

/**
 * @brief Send an SSE event to a client
 *
 * @param wsi WebSocket instance
 * @param event Event type (can be NULL)
 * @param data Event data
 * @param id_str Event ID string
 * @return bool true if the event was sent successfully, false otherwise
 */
static bool send_sse_event_to_client(struct lws* wsi, const char* event,
                                    const char* data, const char* id_str) {
    if (wsi == NULL || data == NULL || id_str == NULL) {
        return false;
    }

    int result = 0;

    if (event != NULL) {
        // Write in multiple pieces to avoid any heap allocation
        // 1. Write "event: "
        result = lws_write_http(wsi, SSE_FIELD_EVENT, strlen(SSE_FIELD_EVENT));
        if (result < 0) return false;

        // 2. Write the event name
        result = lws_write_http(wsi, event, strlen(event));
        if (result < 0) return false;

        // 3. Write "\nid: "
        result = lws_write_http(wsi, "\n", 1);
        if (result < 0) return false;

        result = lws_write_http(wsi, SSE_FIELD_ID, strlen(SSE_FIELD_ID));
        if (result < 0) return false;

        // 4. Write the event ID
        result = lws_write_http(wsi, id_str, strlen(id_str));
        if (result < 0) return false;

        // 5. Write "\ndata: "
        result = lws_write_http(wsi, "\n", 1);
        if (result < 0) return false;

        result = lws_write_http(wsi, SSE_FIELD_DATA, strlen(SSE_FIELD_DATA));
        if (result < 0) return false;

        // 6. Write the data
        result = lws_write_http(wsi, data, strlen(data));
        if (result < 0) return false;

        // 7. Write final "\n\n"
        result = lws_write_http(wsi, "\n\n", 2);
        if (result < 0) return false;
    } else {
        // No event specified, simpler format
        // 1. Write "id: "
        result = lws_write_http(wsi, SSE_FIELD_ID, strlen(SSE_FIELD_ID));
        if (result < 0) return false;

        // 2. Write the event ID
        result = lws_write_http(wsi, id_str, strlen(id_str));
        if (result < 0) return false;

        // 3. Write "\ndata: "
        result = lws_write_http(wsi, "\n", 1);
        if (result < 0) return false;

        result = lws_write_http(wsi, SSE_FIELD_DATA, strlen(SSE_FIELD_DATA));
        if (result < 0) return false;

        // 4. Write the data
        result = lws_write_http(wsi, data, strlen(data));
        if (result < 0) return false;

        // 5. Write final "\n\n"
        result = lws_write_http(wsi, "\n\n", 2);
        if (result < 0) return false;
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
 * optionally filtering by session ID.
 *
 * @param transport Transport instance
 * @param event Event type (can be NULL)
 * @param data Event data
 * @param session_id Session ID to filter by (can be NULL for broadcast)
 * @return int 0 on success, -1 on failure
 */
int mcp_http_transport_send_sse(mcp_transport_t* transport, const char* event,
                               const char* data, const char* session_id) {
    // Validate input parameters
    if (transport == NULL || transport->transport_data == NULL || data == NULL) {
        mcp_log_error("Invalid parameters for mcp_http_transport_send_sse");
        return -1;
    }

    http_transport_data_t* transport_data = (http_transport_data_t*)transport->transport_data;

    // Store the event for replay on reconnection
    store_sse_event(transport_data, event, data);

    // Get the current event ID
    int event_id = transport_data->next_event_id - 1; // The ID was already incremented in store_sse_event
    char id_str[SSE_ID_BUFFER_SIZE];
    int id_len = snprintf(id_str, sizeof(id_str), "%d", event_id);
    if (id_len < 0 || id_len >= (int)sizeof(id_str)) {
        mcp_log_error("Event ID buffer overflow");
        return -1;
    }

    // Log the event details
    mcp_log_debug("Sending SSE event: id=%s, type=%s, data_length=%zu, session_id=%s",
                 id_str, event ? event : "NULL", strlen(data),
                 session_id ? session_id : "NULL");

    // Lock the SSE mutex to safely access the client list
    mcp_mutex_lock(transport_data->sse_mutex);

    // Check if there are any connected clients
    if (transport_data->sse_client_count <= 0) {
        mcp_log_warn("No SSE clients connected, event will not be delivered");
        mcp_mutex_unlock(transport_data->sse_mutex);
        return 0;
    }

    // Send to matching SSE clients
    int matched_clients = 0;
    int success_count = 0;

    for (int i = 0; i < transport_data->sse_client_count; i++) {
        struct lws* wsi = transport_data->sse_clients[i];
        if (wsi == NULL) {
            continue;
        }

        // Get session data to check for event filter and session ID
        http_session_data_t* session = (http_session_data_t*)lws_wsi_user(wsi);

        // Skip this client if it doesn't match the filter
        if (!session_matches_filter(session, event)) {
            continue;
        }

        // Skip this client if it doesn't match the session ID
        if (!session_matches_id(session, session_id)) {
            continue;
        }

        // If we get here, the client matches all criteria
        matched_clients++;

        // Send the event to this client
        if (send_sse_event_to_client(wsi, event, data, id_str)) {
            success_count++;

            // Update the client's last event ID
            if (session != NULL) {
                session->last_event_id = event_id;
            }
        } else {
            mcp_log_error("Failed to send SSE event to client %d", i);
        }
    }

    mcp_mutex_unlock(transport_data->sse_mutex);

    // Log the results
    if (session_id != NULL) {
        if (matched_clients > 0) {
            mcp_log_info("Successfully sent SSE event to %d/%d client(s) with session_id: %s",
                        success_count, matched_clients, session_id);
        } else {
            mcp_log_warn("No SSE clients matched the requested session_id: %s", session_id);
        }
    } else {
        mcp_log_info("Successfully sent SSE event to %d/%d client(s) (broadcast)",
                    success_count, matched_clients);
    }

    return 0;
}
