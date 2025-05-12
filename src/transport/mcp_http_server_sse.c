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

// Store an SSE event for replay on reconnection (using circular buffer)
void store_sse_event(http_transport_data_t* data, const char* event, const char* event_data) {
    if (data == NULL || event_data == NULL) {
        return;
    }

    mcp_mutex_lock(data->event_mutex);

    // Get the current event ID
    int event_id = data->next_event_id++;

    // If the buffer is full, free the oldest event's memory
    if (data->stored_event_count == MAX_SSE_STORED_EVENTS) {
        // Free the oldest event's memory (at head position)
        if (data->stored_events[data->event_head].id)
            free(data->stored_events[data->event_head].id);
        if (data->stored_events[data->event_head].event_type)
            free(data->stored_events[data->event_head].event_type);
        if (data->stored_events[data->event_head].data)
            free(data->stored_events[data->event_head].data);

        // Move head forward (oldest event position)
        data->event_head = (data->event_head + 1) % MAX_SSE_STORED_EVENTS;

        // Count stays the same as we're replacing an event
    } else {
        // Buffer is not full, increment count
        data->stored_event_count++;
    }

    // Add the new event at the tail position
    int index = data->event_tail;

    // Allocate and store the event ID
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%d", event_id);
    data->stored_events[index].id = mcp_strdup(id_str);

    // Store the event type (if provided)
    data->stored_events[index].event_type = event ? mcp_strdup(event) : NULL;

    // Store the event data
    data->stored_events[index].data = mcp_strdup(event_data);

    // Store the timestamp
    data->stored_events[index].timestamp = time(NULL);

    // Move tail forward (position for next event)
    data->event_tail = (data->event_tail + 1) % MAX_SSE_STORED_EVENTS;

    mcp_mutex_unlock(data->event_mutex);
}

// Send a heartbeat to all SSE clients
void send_sse_heartbeat(http_transport_data_t* data) {
    if (!data->send_heartbeats) {
        return;
    }

    time_t now = time(NULL);
    if (now - data->last_heartbeat < data->heartbeat_interval_ms / 1000) {
        return; // Not time for a heartbeat yet
    }

    // Update last heartbeat time
    data->last_heartbeat = now;

    // Send heartbeat to all clients
    mcp_mutex_lock(data->sse_mutex);
    for (int i = 0; i < data->sse_client_count; i++) {
        struct lws* wsi = data->sse_clients[i];

        // Send a comment as a heartbeat (will not trigger an event in the client)
        lws_write_http(wsi, ": heartbeat\n\n", 13);

        // Request a callback when the socket is writable again
        lws_callback_on_writable(wsi);
    }
    mcp_mutex_unlock(data->sse_mutex);
}

// Send SSE event to a specific session or all connected clients
int mcp_http_transport_send_sse(mcp_transport_t* transport, const char* event, const char* data, const char* session_id) {
    if (transport == NULL || transport->transport_data == NULL || data == NULL) {
        return -1;
    }

    http_transport_data_t* transport_data = (http_transport_data_t*)transport->transport_data;

    // Store the event for replay on reconnection
    store_sse_event(transport_data, event, data);

    // Get the current event ID
    int event_id = transport_data->next_event_id - 1; // The ID was already incremented in store_sse_event
    char id_str[32];
    snprintf(id_str, sizeof(id_str), "%d", event_id);

    // Log the number of connected SSE clients
    mcp_log_debug("Sending SSE event to %d connected clients, session_id: %s",
                transport_data->sse_client_count, session_id ? session_id : "NULL");

    // Send to all SSE clients
    mcp_mutex_lock(transport_data->sse_mutex);

    // If no clients are connected, log a warning
    if (transport_data->sse_client_count == 0) {
        mcp_log_warn("No SSE clients connected, event will not be delivered");
    }

    int matched_clients = 0;
    for (int i = 0; i < transport_data->sse_client_count; i++) {
        struct lws* wsi = transport_data->sse_clients[i];

        // Get session data to check for event filter and session ID
        http_session_data_t* session = (http_session_data_t*)lws_wsi_user(wsi);

        // Skip this client if it has a filter and the event doesn't match
        if (session && session->event_filter && event &&
            strcmp(session->event_filter, event) != 0) {
            mcp_log_debug("Skipping SSE client due to event filter mismatch - filter: %s, event: %s",
                         session->event_filter, event);
            continue;
        }

        // If a specific session ID was requested
        if (session_id != NULL) {
            // If session is NULL, skip
            if (session == NULL) {
                mcp_log_debug("Skipping SSE client - client has no session data but requested session_id: %s", session_id);
                continue;
            }

            // If session_id is NULL, skip
            if (session->session_id == NULL) {
                mcp_log_debug("Skipping SSE client - client session_id is NULL but requested session_id: %s", session_id);

                // Dump all session data for debugging
                mcp_log_debug("Client session data: is_sse_client=%d, last_event_id=%d, event_filter=%s",
                             session->is_sse_client,
                             session->last_event_id,
                             session->event_filter ? session->event_filter : "NULL");
                continue;
            }

            // If session_id doesn't match, skip
            if (strcmp(session->session_id, session_id) != 0) {
                mcp_log_debug("Skipping SSE client - session_id mismatch - requested: %s, client: %s",
                             session_id, session->session_id);

                // Try case-insensitive comparison
                if (strcasecmp(session->session_id, session_id) == 0) {
                    mcp_log_debug("Session IDs match with case-insensitive comparison");
                    // Continue with this client despite the case difference
                } else {
                    continue;
                }
            }

            // If we get here, the session_id matched
            mcp_log_info("Found matching SSE client with session_id: %s", session_id);
        }

        matched_clients++;

        if (event != NULL) {
            // Write in multiple pieces to avoid any heap allocation
            // 1. Write "event: "
            lws_write_http(wsi, "event: ", 7);

            // 2. Write the event name
            lws_write_http(wsi, event, strlen(event));

            // 3. Write "\nid: "
            lws_write_http(wsi, "\nid: ", 5);

            // 4. Write the event ID
            lws_write_http(wsi, id_str, strlen(id_str));

            // 5. Write "\ndata: "
            lws_write_http(wsi, "\ndata: ", 7);

            // 6. Write the data
            lws_write_http(wsi, data, strlen(data));

            // 7. Write final "\n\n"
            lws_write_http(wsi, "\n\n", 2);
        } else {
            // No event specified, simpler format
            // 1. Write "id: "
            lws_write_http(wsi, "id: ", 4);

            // 2. Write the event ID
            lws_write_http(wsi, id_str, strlen(id_str));

            // 3. Write "\ndata: "
            lws_write_http(wsi, "\ndata: ", 7);

            // 4. Write the data
            lws_write_http(wsi, data, strlen(data));

            // 5. Write final "\n\n"
            lws_write_http(wsi, "\n\n", 2);
        }

        // Update the client's last event ID
        if (session) {
            session->last_event_id = event_id;
        }

        // Request a callback when the socket is writable again
        // This ensures that libwebsockets will flush the data
        lws_callback_on_writable(wsi);
    }
    mcp_mutex_unlock(transport_data->sse_mutex);

    // Log the number of matched clients
    if (session_id != NULL) {
        if (matched_clients > 0) {
            mcp_log_info("Successfully sent SSE event to %d client(s) with session_id: %s",
                        matched_clients, session_id);
        } else {
            mcp_log_warn("No SSE clients matched the requested session_id: %s", session_id);
        }
    } else {
        mcp_log_info("Successfully sent SSE event to %d client(s) (broadcast)", matched_clients);
    }

    return 0;
}
