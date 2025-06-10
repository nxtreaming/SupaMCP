#include "mcp_mqtt_client_transport.h"
#include "mqtt_client_internal.h"
#include "transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_sys_utils.h"
#include "libwebsockets.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
// Windows doesn't have strndup, so we implement it
static char* strndup(const char* s, size_t n) {
    if (!s) return NULL;

    size_t len = strlen(s);
    if (n < len) len = n;

    char* result = malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, s, len);
    result[len] = '\0';
    return result;
}
#endif

// Forward declarations for transport interface functions
static int mqtt_client_transport_init(mcp_transport_t* transport);
static void mqtt_client_transport_destroy(mcp_transport_t* transport);
static int mqtt_client_transport_start(mcp_transport_t* transport,
                                      mcp_transport_message_callback_t message_callback,
                                      void* user_data,
                                      mcp_transport_error_callback_t error_callback);
static int mqtt_client_transport_stop(mcp_transport_t* transport);
static int mqtt_client_transport_send(mcp_transport_t* transport, const void* data, size_t size);
static int mqtt_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count);
static int mqtt_client_transport_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms);

// libwebsockets MQTT client protocol definition
static int mqtt_client_protocol_callback(struct lws* wsi, enum lws_callback_reasons reason,
                                        void* user, void* in, size_t len);

static struct lws_protocols mqtt_client_protocols[] = {
    {
        .name = "mqtt",
        .callback = mqtt_client_protocol_callback,
        .per_session_data_size = sizeof(mcp_mqtt_protocol_data_t),
        .rx_buffer_size = MCP_MQTT_MAX_MESSAGE_SIZE,
        .id = 0,
        .user = NULL,
        .tx_packet_size = 0
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 } // terminator
};

/**
 * @brief Initializes MQTT client transport data
 */
int mqtt_client_transport_data_init(mcp_mqtt_client_transport_data_t* data, 
                                   const mcp_mqtt_client_config_t* config) {
    if (!data || !config) {
        return -1;
    }
    
    memset(data, 0, sizeof(mcp_mqtt_client_transport_data_t));
    
    // Initialize base transport data
    if (mqtt_transport_data_init(&data->base, &config->base, false) != 0) {
        return -1;
    }
    
    // Copy client configuration
    data->client_config = *config;
    
    // Initialize client-specific synchronization objects
    data->message_tracking.packet_mutex = mcp_mutex_create();
    data->message_tracking.inflight_mutex = mcp_mutex_create();
    data->session.state_mutex = mcp_mutex_create();
    data->session.subscription_mutex = mcp_mutex_create();
    data->monitoring.ping_mutex = mcp_mutex_create();
    data->reconnect_mutex = mcp_mutex_create();
    data->stats_mutex = mcp_mutex_create();
    data->monitoring.ping_condition = mcp_cond_create();
    data->reconnect_condition = mcp_cond_create();

    if (!data->message_tracking.packet_mutex || !data->message_tracking.inflight_mutex ||
        !data->session.state_mutex || !data->session.subscription_mutex ||
        !data->monitoring.ping_mutex || !data->reconnect_mutex || !data->stats_mutex ||
        !data->monitoring.ping_condition || !data->reconnect_condition) {
        mcp_log_error("Failed to create MQTT client synchronization objects");
        // Clean up any created objects
        if (data->message_tracking.packet_mutex) mcp_mutex_destroy(data->message_tracking.packet_mutex);
        if (data->message_tracking.inflight_mutex) mcp_mutex_destroy(data->message_tracking.inflight_mutex);
        if (data->session.state_mutex) mcp_mutex_destroy(data->session.state_mutex);
        if (data->session.subscription_mutex) mcp_mutex_destroy(data->session.subscription_mutex);
        if (data->monitoring.ping_mutex) mcp_mutex_destroy(data->monitoring.ping_mutex);
        if (data->reconnect_mutex) mcp_mutex_destroy(data->reconnect_mutex);
        if (data->stats_mutex) mcp_mutex_destroy(data->stats_mutex);
        if (data->monitoring.ping_condition) mcp_cond_destroy(data->monitoring.ping_condition);
        if (data->reconnect_condition) mcp_cond_destroy(data->reconnect_condition);
        return -1;
    }
    
    // Initialize variables
    data->reconnect_state = MQTT_RECONNECT_IDLE;
    data->message_tracking.inflight_count = 0;
    data->monitoring.ping_thread_active = false;
    data->monitoring.pending_pings = 0;

    // Initialize thread pointers
    data->reconnect_thread = NULL;
    data->monitoring.ping_thread = NULL;
    
    // Set default values
    data->message_tracking.packet_id = 1;
    data->message_tracking.max_inflight = config->max_inflight_messages;
    data->monitoring.ping_interval_ms = config->ping_interval_ms;
    data->monitoring.ping_timeout_ms = config->ping_timeout_ms;
    data->metrics_enabled = config->enable_metrics;
    data->message_retry_interval_ms = config->message_retry_interval_ms;
    data->max_message_retries = config->max_message_retries;
    
    // Initialize statistics
    memset(&data->stats, 0, sizeof(data->stats));
    
    mcp_log_debug("MQTT client transport data initialized");
    
    return 0;
}

/**
 * @brief Cleans up MQTT client transport data
 */
void mqtt_client_transport_data_cleanup(mcp_mqtt_client_transport_data_t* data) {
    if (!data) {
        return;
    }
    
    // Cancel any pending reconnection
    mqtt_client_cancel_reconnect(data);
    
    // Stop ping monitoring
    data->monitoring.ping_thread_active = false;
    if (data->monitoring.ping_thread) {
        mcp_cond_signal(data->monitoring.ping_condition);
        mcp_thread_join(*data->monitoring.ping_thread, NULL);
        free(data->monitoring.ping_thread);
        data->monitoring.ping_thread = NULL;
    }
    
    // Clean up in-flight messages
    mcp_mutex_lock(data->message_tracking.inflight_mutex);
    struct mqtt_inflight_message* msg = data->message_tracking.inflight_messages;
    while (msg) {
        struct mqtt_inflight_message* next = msg->next;
        free(msg->topic);
        free(msg->payload);
        free(msg);
        msg = next;
    }
    data->message_tracking.inflight_messages = NULL;
    mcp_mutex_unlock(data->message_tracking.inflight_mutex);

    // Clean up subscriptions
    mcp_mutex_lock(data->session.subscription_mutex);
    struct mqtt_subscription* sub = data->session.subscriptions;
    while (sub) {
        struct mqtt_subscription* next = sub->next;
        free(sub->topic);
        free(sub);
        sub = next;
    }
    data->session.subscriptions = NULL;
    mcp_mutex_unlock(data->session.subscription_mutex);

    // Clean up session state file
    free(data->session.state_file);

    // Destroy synchronization objects
    if (data->message_tracking.packet_mutex) mcp_mutex_destroy(data->message_tracking.packet_mutex);
    if (data->message_tracking.inflight_mutex) mcp_mutex_destroy(data->message_tracking.inflight_mutex);
    if (data->session.state_mutex) mcp_mutex_destroy(data->session.state_mutex);
    if (data->session.subscription_mutex) mcp_mutex_destroy(data->session.subscription_mutex);
    if (data->monitoring.ping_mutex) mcp_mutex_destroy(data->monitoring.ping_mutex);
    if (data->reconnect_mutex) mcp_mutex_destroy(data->reconnect_mutex);
    if (data->stats_mutex) mcp_mutex_destroy(data->stats_mutex);
    if (data->monitoring.ping_condition) mcp_cond_destroy(data->monitoring.ping_condition);
    if (data->reconnect_condition) mcp_cond_destroy(data->reconnect_condition);
    
    // Clean up base transport data
    mqtt_transport_data_cleanup(&data->base);
    
    mcp_log_debug("MQTT client transport data cleaned up");
}

/**
 * @brief MQTT client protocol callback function
 */
static int mqtt_client_protocol_callback(struct lws* wsi, enum lws_callback_reasons reason,
                                        void* user, void* in, size_t len) {
    mcp_mqtt_protocol_data_t* protocol_data = (mcp_mqtt_protocol_data_t*)user;
    mcp_mqtt_client_transport_data_t* client_data = NULL;
    
    // Get client data from protocol user data
    if (lws_get_protocol(wsi) && lws_get_protocol(wsi)->user) {
        client_data = (mcp_mqtt_client_transport_data_t*)lws_get_protocol(wsi)->user;
    }
    
    switch (reason) {
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            mcp_log_error("MQTT client connection error");
            if (client_data) {
                mqtt_client_handle_state_change(client_data, MCP_MQTT_CLIENT_ERROR, "Connection error");
                if (client_data->client_config.auto_reconnect) {
                    mqtt_client_schedule_reconnect(client_data);
                }
            }
            break;
            
        case LWS_CALLBACK_MQTT_CLIENT_ESTABLISHED:
            mcp_log_info("MQTT client connected");
            if (protocol_data && client_data) {
                protocol_data->transport_data = &client_data->base;
                protocol_data->is_authenticated = false;
                protocol_data->connect_time = mcp_get_time_ms();

                // Generate client ID if not provided
                if (!client_data->base.config.client_id) {
                    char* generated_id = mqtt_generate_client_id();
                    if (generated_id) {
                        client_data->base.config.client_id = generated_id;
                    }
                }

                strncpy(protocol_data->client_id,
                       client_data->base.config.client_id ? client_data->base.config.client_id : "unknown",
                       sizeof(protocol_data->client_id) - 1);

                // Resolve MQTT topics with client ID
                if (mqtt_resolve_topics(&client_data->base, protocol_data->client_id) != 0) {
                    mcp_log_error("Failed to resolve MQTT topics for client: %s", protocol_data->client_id);
                } else {
                    mcp_log_debug("MQTT topics resolved for client: %s", protocol_data->client_id);
                    mcp_log_debug("Request topic: %s", client_data->base.resolved_request_topic);
                    mcp_log_debug("Response topic: %s", client_data->base.resolved_response_topic);
                    mcp_log_debug("Notification topic: %s", client_data->base.resolved_notification_topic);
                }

                // Subscribe to response and notification topics
                if (client_data->base.resolved_response_topic) {
                    lws_mqtt_subscribe_param_t sub = {0};
                    sub.num_topics = 1;
                    sub.topic[0].name = client_data->base.resolved_response_topic;
                    sub.topic[0].qos = client_data->base.config.qos;
                    lws_mqtt_client_send_subcribe(wsi, &sub);
                }

                if (client_data->base.resolved_notification_topic) {
                    lws_mqtt_subscribe_param_t sub = {0};
                    sub.num_topics = 1;
                    sub.topic[0].name = client_data->base.resolved_notification_topic;
                    sub.topic[0].qos = client_data->base.config.qos;
                    lws_mqtt_client_send_subcribe(wsi, &sub);
                }

                mqtt_client_handle_state_change(client_data, MCP_MQTT_CLIENT_CONNECTED, "Connected");
            }
            break;
            
        case LWS_CALLBACK_CLIENT_CLOSED:
            mcp_log_info("MQTT client disconnected");
            if (client_data) {
                mqtt_client_handle_state_change(client_data, MCP_MQTT_CLIENT_DISCONNECTED, "Disconnected");
                if (client_data->client_config.auto_reconnect) {
                    mqtt_client_schedule_reconnect(client_data);
                }
            }
            break;
            
        case LWS_CALLBACK_MQTT_CLIENT_RX:
            if (protocol_data && client_data && in && len > 0) {
                // Handle incoming MQTT message
                lws_mqtt_publish_param_t *pub = (lws_mqtt_publish_param_t *)in;
                mcp_log_debug("Received MQTT message on topic: %.*s, size: %u",
                             pub->topic_len, pub->topic, pub->payload_len);

                char* topic = strndup(pub->topic, pub->topic_len);
                if (topic) {
                    mqtt_handle_incoming_message(&client_data->base, topic, pub->payload, pub->payload_len);
                    free(topic);
                }
                mqtt_client_update_stats(client_data, false, true, pub->payload_len);
            }
            break;
            
        case LWS_CALLBACK_CLIENT_WRITEABLE:
            // Handle outgoing messages
            if (client_data) {
                mqtt_process_message_queue(&client_data->base);
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}

/**
 * @brief Handles connection state changes
 */
void mqtt_client_handle_state_change(mcp_mqtt_client_transport_data_t* data,
                                    mcp_mqtt_client_state_t new_state,
                                    const char* reason) {
    if (!data) {
        return;
    }
    
    mcp_log_debug("MQTT client state change: %d -> %d (%s)",
                 data->base.connection_state, new_state, reason ? reason : "");

    data->base.connection_state = new_state;
    
    // Update statistics
    mcp_mutex_lock(data->stats_mutex);
    if (new_state == MCP_MQTT_CLIENT_CONNECTED) {
        data->stats.successful_connections++;
        data->stats.last_connect_time = mcp_get_time_ms();
    } else if (new_state == MCP_MQTT_CLIENT_DISCONNECTED || new_state == MCP_MQTT_CLIENT_ERROR) {
        data->stats.last_disconnect_time = mcp_get_time_ms();
        if (data->stats.last_connect_time > 0) {
            data->stats.uptime_seconds = (uint32_t)((data->stats.last_disconnect_time - data->stats.last_connect_time) / 1000);
        }
    }
    mcp_mutex_unlock(data->stats_mutex);
    
    // Call state callback if set
    if (data->state_callback) {
        data->state_callback(new_state, reason, data->state_callback_user_data);
    }
}

/**
 * @brief Updates client statistics
 */
void mqtt_client_update_stats(mcp_mqtt_client_transport_data_t* data,
                            bool message_sent, bool message_received,
                            size_t bytes) {
    if (!data || !data->metrics_enabled) {
        return;
    }
    
    mcp_mutex_lock(data->stats_mutex);

    if (message_sent) {
        data->stats.messages_sent++;
        data->stats.bytes_sent += bytes;
    }

    if (message_received) {
        data->stats.messages_received++;
        data->stats.bytes_received += bytes;
    }

    mcp_mutex_unlock(data->stats_mutex);
}

/**
 * @brief Initializes MQTT client transport
 */
static int mqtt_client_transport_init(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;

    // Validate configuration
    if (mqtt_client_validate_config(&data->client_config) != 0) {
        return -1;
    }

    mcp_log_info("MQTT client transport initialized");
    return 0;
}

/**
 * @brief Destroys MQTT client transport
 */
static void mqtt_client_transport_destroy(mcp_transport_t* transport) {
    if (!transport) {
        return;
    }

    if (transport->transport_data) {
        mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
        mqtt_client_transport_data_cleanup(data);
        free(data);
        transport->transport_data = NULL;
    }

    mcp_log_info("MQTT client transport destroyed");
}

/**
 * @brief Starts MQTT client transport
 */
static int mqtt_client_transport_start(mcp_transport_t* transport,
                                      mcp_transport_message_callback_t message_callback,
                                      void* user_data,
                                      mcp_transport_error_callback_t error_callback) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;

    // Store callbacks
    data->base.message_callback = message_callback;
    data->base.callback_user_data = user_data;
    data->base.error_callback = error_callback;

    // Start connection
    if (mqtt_client_start_connection(data) != 0) {
        return -1;
    }

    mcp_log_info("MQTT client transport started");
    return 0;
}

/**
 * @brief Stops MQTT client transport
 */
static int mqtt_client_transport_stop(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;

    // Stop connection
    if (mqtt_client_stop_connection(data) != 0) {
        return -1;
    }

    mcp_log_info("MQTT client transport stopped");
    return 0;
}

/**
 * @brief Sends data through MQTT client transport
 */
static int mqtt_client_transport_send(mcp_transport_t* transport, const void* data, size_t size) {
    if (!transport || !transport->transport_data || !data || size == 0) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* client_data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;

    // Check if connected
    if (client_data->base.connection_state != MCP_MQTT_CLIENT_CONNECTED) {
        mcp_log_warn("MQTT client not connected, cannot send message");
        return -1;
    }

    // Use the resolved request topic for sending MCP messages
    const char* topic = client_data->base.resolved_request_topic;
    if (!topic) {
        mcp_log_error("No request topic configured for MQTT client");
        return -1;
    }

    // Serialize MCP message to MQTT payload
    void* mqtt_payload = NULL;
    size_t mqtt_len = 0;

    if (mqtt_serialize_mcp_message(data, size, &mqtt_payload, &mqtt_len) != 0) {
        mcp_log_error("Failed to serialize MCP message for MQTT");
        return -1;
    }

    // Enqueue message for sending
    int result = mqtt_enqueue_message(&client_data->base, topic, mqtt_payload, mqtt_len,
                                     client_data->base.config.qos, client_data->base.config.retain);

    free(mqtt_payload);

    if (result == 0) {
        mqtt_client_update_stats(client_data, true, false, size);
    }

    return result;
}

/**
 * @brief Sends data from multiple buffers through MQTT client transport
 */
static int mqtt_client_transport_sendv(mcp_transport_t* transport, const mcp_buffer_t* buffers, size_t buffer_count) {
    if (!transport || !buffers || buffer_count == 0) {
        return -1;
    }

    // Calculate total size
    size_t total_size = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        total_size += buffers[i].size;
    }

    if (total_size == 0) {
        return -1;
    }

    // Combine buffers into single buffer
    void* combined_data = malloc(total_size);
    if (!combined_data) {
        mcp_log_error("Failed to allocate combined buffer for MQTT sendv");
        return -1;
    }

    size_t offset = 0;
    for (size_t i = 0; i < buffer_count; i++) {
        memcpy((char*)combined_data + offset, buffers[i].data, buffers[i].size);
        offset += buffers[i].size;
    }

    // Send combined data
    int result = mqtt_client_transport_send(transport, combined_data, total_size);

    free(combined_data);
    return result;
}

/**
 * @brief Receives data synchronously from MQTT client transport
 */
static int mqtt_client_transport_receive(mcp_transport_t* transport, char** data, size_t* size, uint32_t timeout_ms) {
    // MQTT is primarily asynchronous, so synchronous receive is not typically used
    // This is a placeholder implementation
    if (!transport || !data || !size) {
        return -1;
    }

    *data = NULL;
    *size = 0;

    mcp_log_debug("MQTT client synchronous receive not implemented (timeout: %u ms)", timeout_ms);
    return -1; // Not implemented for MQTT
}

/**
 * @brief Placeholder implementations for client-specific functions
 */

int mqtt_client_start_connection(mcp_mqtt_client_transport_data_t* data) {
    if (!data) {
        return -1;
    }

    mcp_log_debug("Starting MQTT client connection to %s:%d",
                 data->base.config.host, data->base.config.port);

    mqtt_client_handle_state_change(data, MCP_MQTT_CLIENT_CONNECTING, "Starting connection");

    // Generate client ID if not provided
    if (!data->base.config.client_id) {
        char* generated_id = mqtt_generate_client_id();
        if (generated_id) {
            data->base.config.client_id = generated_id;
        }
    }

    // Resolve MQTT topics with client ID
    const char* client_id = data->base.config.client_id ? data->base.config.client_id : "unknown";
    if (mqtt_resolve_topics(&data->base, client_id) != 0) {
        mcp_log_error("Failed to resolve MQTT topics for client: %s", client_id);
        return -1;
    } else {
        mcp_log_debug("MQTT topics resolved for client: %s", client_id);
        mcp_log_debug("Request topic: %s", data->base.resolved_request_topic);
        mcp_log_debug("Response topic: %s", data->base.resolved_response_topic);
        mcp_log_debug("Notification topic: %s", data->base.resolved_notification_topic);
    }

    // Create libwebsockets context for MQTT client
    data->base.context = mqtt_create_lws_context(&data->base);
    if (!data->base.context) {
        mcp_log_error("Failed to create libwebsockets context for MQTT client");
        return -1;
    }

    // Set up MQTT connection parameters
    lws_mqtt_client_connect_param_t mqtt_params = {0};
    mqtt_params.client_id = data->base.config.client_id;
    mqtt_params.keep_alive = data->base.config.keep_alive;
    mqtt_params.clean_start = 1;
    mqtt_params.username = data->base.config.username;
    mqtt_params.password = data->base.config.password;

    // Set up client connection info
    struct lws_client_connect_info connect_info = {0};
    connect_info.context = data->base.context;
    connect_info.address = data->base.config.host;
    connect_info.port = data->base.config.port;
    connect_info.path = "/";
    connect_info.host = data->base.config.host;
    connect_info.origin = data->base.config.host;
    connect_info.protocol = "mqtt";
    connect_info.ssl_connection = data->base.config.use_ssl ? LCCSCF_USE_SSL : 0;
    connect_info.userdata = data;
    connect_info.mqtt_cp = &mqtt_params;

    // Attempt connection
    data->base.wsi = lws_client_connect_via_info(&connect_info);
    if (!data->base.wsi) {
        mcp_log_error("Failed to initiate MQTT client connection");
        return -1;
    }

    mcp_log_debug("MQTT client connection initiated");
    return 0;
}

int mqtt_client_stop_connection(mcp_mqtt_client_transport_data_t* data) {
    if (!data) {
        return -1;
    }

    // Signal stop
    data->base.should_stop = true;

    mqtt_client_handle_state_change(data, MCP_MQTT_CLIENT_DISCONNECTED, "Stopped");

    return 0;
}

void* mqtt_client_reconnect_thread(void* arg) {
    // Placeholder for reconnection logic
    return NULL;
}

int mqtt_client_schedule_reconnect(mcp_mqtt_client_transport_data_t* data) {
    // Placeholder for reconnection scheduling
    return 0;
}

void mqtt_client_cancel_reconnect(mcp_mqtt_client_transport_data_t* data) {
    // Placeholder for reconnection cancellation
}

uint32_t mqtt_client_calculate_reconnect_delay(mcp_mqtt_client_transport_data_t* data) {
    // Placeholder for reconnection delay calculation
    return 1000;
}

void* mqtt_client_ping_thread(void* arg) {
    // Placeholder for ping monitoring
    return NULL;
}

int mqtt_client_send_ping(mcp_mqtt_client_transport_data_t* data) {
    // Placeholder for ping sending
    return 0;
}

void mqtt_client_handle_pong(mcp_mqtt_client_transport_data_t* data) {
    // Placeholder for pong handling
}

int mqtt_client_add_inflight_message(mcp_mqtt_client_transport_data_t* data,
                                    uint16_t packet_id, const char* topic,
                                    const void* payload, size_t payload_len,
                                    int qos, bool retain) {
    // Placeholder for in-flight message tracking
    return 0;
}

void mqtt_client_remove_inflight_message(mcp_mqtt_client_transport_data_t* data,
                                        uint16_t packet_id) {
    // Placeholder for in-flight message removal
}

int mqtt_client_retry_inflight_messages(mcp_mqtt_client_transport_data_t* data) {
    // Placeholder for message retry logic
    return 0;
}

uint16_t mqtt_client_next_packet_id(mcp_mqtt_client_transport_data_t* data) {
    if (!data) {
        return 0;
    }

    mcp_mutex_lock(data->message_tracking.packet_mutex);
    uint16_t id = data->message_tracking.packet_id++;
    if (data->message_tracking.packet_id == 0) {
        data->message_tracking.packet_id = 1; // Skip 0
    }
    mcp_mutex_unlock(data->message_tracking.packet_mutex);

    return id;
}

int mqtt_client_add_subscription(mcp_mqtt_client_transport_data_t* data,
                               const char* topic, int qos) {
    // Placeholder for subscription management
    return 0;
}

void mqtt_client_remove_subscription(mcp_mqtt_client_transport_data_t* data,
                                   const char* topic) {
    // Placeholder for subscription removal
}

int mqtt_client_restore_subscriptions(mcp_mqtt_client_transport_data_t* data) {
    // Placeholder for subscription restoration
    return 0;
}

int mqtt_client_save_session_state(mcp_mqtt_client_transport_data_t* data) {
    // Placeholder for session state saving
    return 0;
}

int mqtt_client_load_session_state(mcp_mqtt_client_transport_data_t* data) {
    // Placeholder for session state loading
    return 0;
}

void mqtt_client_reset_stats(mcp_mqtt_client_transport_data_t* data) {
    if (!data) {
        return;
    }

    mcp_mutex_lock(data->stats_mutex);
    memset(&data->stats, 0, sizeof(data->stats));
    mcp_mutex_unlock(data->stats_mutex);
}

int mqtt_client_validate_config(const mcp_mqtt_client_config_t* config) {
    if (!config) {
        return -1;
    }

    // Validate base configuration
    if (mqtt_validate_config(&config->base) != 0) {
        return -1;
    }

    // Validate client-specific configuration
    if (config->backoff_factor <= 0.0) {
        mcp_log_error("MQTT client backoff factor must be > 0");
        return -1;
    }

    if (config->max_inflight_messages == 0) {
        mcp_log_error("MQTT client max in-flight messages must be > 0");
        return -1;
    }

    return 0;
}

/**
 * @brief Creates an MQTT client transport with extended configuration
 */
mcp_transport_t* mcp_transport_mqtt_client_create_with_config(const mcp_mqtt_client_config_t* config) {
    if (!config) {
        mcp_log_error("MQTT client config is required");
        return NULL;
    }

    // Allocate transport structure
    mcp_transport_t* transport = malloc(sizeof(mcp_transport_t));
    if (!transport) {
        mcp_log_error("Failed to allocate MQTT client transport");
        return NULL;
    }

    memset(transport, 0, sizeof(mcp_transport_t));

    // Allocate transport data
    mcp_mqtt_client_transport_data_t* data = malloc(sizeof(mcp_mqtt_client_transport_data_t));
    if (!data) {
        mcp_log_error("Failed to allocate MQTT client transport data");
        free(transport);
        return NULL;
    }

    // Initialize transport data
    if (mqtt_client_transport_data_init(data, config) != 0) {
        mcp_log_error("Failed to initialize MQTT client transport data");
        free(data);
        free(transport);
        return NULL;
    }

    // Set transport properties
    transport->type = MCP_TRANSPORT_TYPE_CLIENT;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_MQTT;
    transport->transport_data = data;

    // Set client interface functions
    transport->client.init = mqtt_client_transport_init;
    transport->client.destroy = mqtt_client_transport_destroy;
    transport->client.start = mqtt_client_transport_start;
    transport->client.stop = mqtt_client_transport_stop;
    transport->client.send = mqtt_client_transport_send;
    transport->client.sendv = mqtt_client_transport_sendv;
    transport->client.receive = mqtt_client_transport_receive;

    mcp_log_info("Created MQTT client transport");
    return transport;
}

/**
 * @brief Gets the current connection state of the MQTT client
 */
mcp_mqtt_client_state_t mcp_mqtt_client_get_state(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return MCP_MQTT_CLIENT_ERROR;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
    return (mcp_mqtt_client_state_t)data->base.connection_state;
}

/**
 * @brief Gets connection statistics for the MQTT client
 */
int mcp_mqtt_client_get_stats(mcp_transport_t* transport, mcp_mqtt_client_stats_t* stats) {
    if (!transport || !transport->transport_data || !stats) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;

    mcp_mutex_lock(data->stats_mutex);
    *stats = data->stats;
    mcp_mutex_unlock(data->stats_mutex);

    return 0;
}

/**
 * @brief Resets connection statistics for the MQTT client
 */
int mcp_mqtt_client_reset_stats(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
    mqtt_client_reset_stats(data);

    return 0;
}

/**
 * @brief Forces a reconnection attempt for the MQTT client
 */
int mcp_mqtt_client_force_reconnect(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;

    // Stop current connection
    mqtt_client_stop_connection(data);

    // Schedule reconnection
    return mqtt_client_schedule_reconnect(data);
}

/**
 * @brief Sets the connection state callback for the MQTT client
 */
int mcp_mqtt_client_set_state_callback(mcp_transport_t* transport,
                                      mcp_mqtt_client_state_callback_t callback,
                                      void* user_data) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
    data->state_callback = callback;
    data->state_callback_user_data = user_data;

    return 0;
}

/**
 * @brief Enables or disables automatic reconnection for the MQTT client
 */
int mcp_mqtt_client_set_auto_reconnect(mcp_transport_t* transport, bool enable) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
    data->client_config.auto_reconnect = enable;

    if (!enable) {
        mqtt_client_cancel_reconnect(data);
    }

    return 0;
}

/**
 * @brief Gets the broker information for the MQTT client
 */
int mcp_mqtt_client_get_broker_info(mcp_transport_t* transport,
                                   const char** host,
                                   uint16_t* port) {
    if (!transport || !transport->transport_data || !host || !port) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
    *host = data->base.config.host;
    *port = data->base.config.port;

    return 0;
}
