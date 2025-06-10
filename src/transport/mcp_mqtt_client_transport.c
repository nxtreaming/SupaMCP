#include "mcp_mqtt_client_transport.h"
#include "mqtt_client_internal.h"
#include "internal/mqtt_session_persistence.h"
#include "transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_sys_utils.h"
#include "mcp_string_utils.h"
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

    // Initialize session persistence
    if (config->session_storage_path) {
        data->session_storage_path = mcp_strdup(config->session_storage_path);
        if (mqtt_session_persistence_init(data->session_storage_path) != 0) {
            mcp_log_warn("Failed to initialize session persistence");
            free(data->session_storage_path);
            data->session_storage_path = NULL;
        }
    }
    data->session_persist = config->persistent_session;
    
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
    data->session_cleanup_condition = mcp_cond_create();
    data->session_cleanup_mutex = mcp_mutex_create();

    if (!data->message_tracking.packet_mutex || !data->message_tracking.inflight_mutex ||
        !data->session.state_mutex || !data->session.subscription_mutex ||
        !data->monitoring.ping_mutex || !data->reconnect_mutex || !data->stats_mutex ||
        !data->monitoring.ping_condition || !data->reconnect_condition ||
        !data->session_cleanup_condition || !data->session_cleanup_mutex) {
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
        if (data->session_cleanup_condition) mcp_cond_destroy(data->session_cleanup_condition);
        if (data->session_cleanup_mutex) mcp_mutex_destroy(data->session_cleanup_mutex);
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
    data->session_cleanup_thread = NULL;
    data->session_cleanup_active = false;
    data->session_cleanup_interval_ms = 3600000; // 1 hour default
    
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

    // Stop session cleanup
    data->session_cleanup_active = false;
    if (data->session_cleanup_thread) {
        mcp_cond_signal(data->session_cleanup_condition);
        mcp_thread_join(*data->session_cleanup_thread, NULL);
        free(data->session_cleanup_thread);
        data->session_cleanup_thread = NULL;
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
    if (data->session_cleanup_condition) mcp_cond_destroy(data->session_cleanup_condition);
    if (data->session_cleanup_mutex) mcp_mutex_destroy(data->session_cleanup_mutex);
    
    // Clean up base transport data
    mqtt_transport_data_cleanup(&data->base);

    // Clean up session persistence
    if (data->session_storage_path) {
        mqtt_session_persistence_cleanup();
        free(data->session_storage_path);
        data->session_storage_path = NULL;
    }
    
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

                // Subscribe to appropriate topics based on transport type
                mcp_transport_t* transport = (mcp_transport_t*)protocol_data->transport_data;
                bool is_server = (transport && transport->type == MCP_TRANSPORT_TYPE_SERVER);

                if (is_server) {
                    // Server: Subscribe to all stored subscriptions (including request wildcard)
                    mqtt_client_restore_subscriptions(client_data);
                } else {
                    // Client: Subscribe to response and notification topics
                    if (client_data->base.resolved_response_topic) {
                        lws_mqtt_topic_elem_t topic_elem = {0};
                        topic_elem.name = client_data->base.resolved_response_topic;
                        topic_elem.qos = (lws_mqtt_qos_levels_t)client_data->base.config.qos;

                        lws_mqtt_subscribe_param_t sub = {0};
                        sub.num_topics = 1;
                        sub.topic = &topic_elem;
                        lws_mqtt_client_send_subcribe(wsi, &sub);
                    }

                    if (client_data->base.resolved_notification_topic) {
                        lws_mqtt_topic_elem_t topic_elem = {0};
                        topic_elem.name = client_data->base.resolved_notification_topic;
                        topic_elem.qos = (lws_mqtt_qos_levels_t)client_data->base.config.qos;

                        lws_mqtt_subscribe_param_t sub = {0};
                        sub.num_topics = 1;
                        sub.topic = &topic_elem;
                        lws_mqtt_client_send_subcribe(wsi, &sub);
                    }
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
                } else {
                    mcp_log_error("Failed to allocate memory for MQTT topic (length: %u)", pub->topic_len);
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

    // Determine if this is a server transport (based on transport type)
    bool is_server = (transport->type == MCP_TRANSPORT_TYPE_SERVER);

    // For server transports, set up subscription to request topics
    if (is_server) {
        // Subscribe to all client request topics using wildcard
        const char* prefix = data->base.config.topic_prefix ? data->base.config.topic_prefix : "mcp/";
        char request_wildcard_topic[256];
        snprintf(request_wildcard_topic, sizeof(request_wildcard_topic), "%srequest/+", prefix);

        // Add subscription for server to receive all client requests
        mqtt_client_add_subscription(data, request_wildcard_topic, data->base.config.qos);

        mcp_log_info("MQTT server will subscribe to request topic: %s", request_wildcard_topic);
    }

    // Start session cleanup thread if persistence is enabled
    if (data->session_persist && data->session_storage_path) {
        data->session_cleanup_active = true;
        data->session_cleanup_thread = malloc(sizeof(mcp_thread_t));
        if (data->session_cleanup_thread) {
            if (mcp_thread_create(data->session_cleanup_thread, mqtt_client_session_cleanup_thread, data) != 0) {
                mcp_log_warn("Failed to create session cleanup thread");
                free(data->session_cleanup_thread);
                data->session_cleanup_thread = NULL;
                data->session_cleanup_active = false;
            } else {
                mcp_log_debug("Session cleanup thread started");
            }
        }
    }

    // Start connection
    if (mqtt_client_start_connection(data) != 0) {
        return -1;
    }

    mcp_log_info("MQTT client transport started (server mode: %s)", is_server ? "yes" : "no");
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
            mcp_log_debug("Generated MQTT client ID: %s", generated_id);
        } else {
            mcp_log_error("Failed to generate MQTT client ID");
            return -1;
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

    // Load session if persistence is enabled
    if (data->session_persist && data->session_storage_path) {
        mqtt_session_data_t session_data = {0};
        if (mqtt_session_load(data->base.config.client_id, &session_data) == 0) {
            mcp_log_info("Loaded persistent session for client: %s", data->base.config.client_id);

            // Restore subscriptions
            int subscription_count = 0;
            struct mqtt_subscription* sub = session_data.subscriptions;
            while (sub) {
                struct mqtt_subscription* next = sub->next;
                if (mqtt_client_add_subscription(data, sub->topic, sub->qos) == 0) {
                    subscription_count++;
                } else {
                    mcp_log_warn("Failed to restore subscription for topic: %s", sub->topic);
                }
                free(sub->topic);
                free(sub);
                sub = next;
            }
            mcp_log_debug("Restored %d subscriptions from persistent session", subscription_count);

            // Restore in-flight messages
            int inflight_count = 0;
            struct mqtt_inflight_message* inflight = session_data.inflight_messages;
            while (inflight) {
                struct mqtt_inflight_message* next = inflight->next;
                if (mqtt_client_add_inflight_message(data, inflight->packet_id, inflight->topic,
                                                   inflight->payload, inflight->payload_len,
                                                   inflight->qos, inflight->retain) == 0) {
                    inflight_count++;
                } else {
                    mcp_log_warn("Failed to restore in-flight message for topic: %s", inflight->topic);
                }
                free(inflight->topic);
                free(inflight->payload);
                free(inflight);
                inflight = next;
            }
            mcp_log_debug("Restored %d in-flight messages from persistent session", inflight_count);

            // Restore last packet ID if needed
            if (session_data.last_packet_id > 0) {
                data->message_tracking.packet_id = (uint16_t)session_data.last_packet_id;
                mcp_log_debug("Restored last packet ID: %u", session_data.last_packet_id);
            }
        } else {
            mcp_log_debug("No existing session found for client: %s", data->base.config.client_id);
        }
    }

    mcp_log_debug("MQTT client connection initiated");
    return 0;
}

static int mqtt_client_save_session(mcp_mqtt_client_transport_data_t* data) {
    if (!data->session_persist || !data->session_storage_path) {
        return 0;  // Session persistence not enabled
    }

    mqtt_session_data_t session_data = {0};
    session_data.client_id = (char*)data->base.config.client_id;
    session_data.session_created_time = mcp_get_time_ms();
    session_data.session_last_access_time = mcp_get_time_ms();
    session_data.session_expiry_interval = data->client_config.session_expiry_interval;
    session_data.file_format_version = 1;

    // Copy subscriptions
    mcp_mutex_lock(data->session.subscription_mutex);
    struct mqtt_subscription* sub = data->session.subscriptions;
    while (sub) {
        struct mqtt_subscription* new_sub = (struct mqtt_subscription*)malloc(sizeof(struct mqtt_subscription));
        if (new_sub) {
            new_sub->topic = mcp_strdup(sub->topic);
            if (!new_sub->topic) {
                // mcp_strdup failed, clean up and continue
                free(new_sub);
                mcp_log_warn("Failed to duplicate subscription topic, skipping");
                sub = sub->next;
                continue;
            }
            new_sub->qos = sub->qos;
            new_sub->active = sub->active;
            new_sub->next = session_data.subscriptions;
            session_data.subscriptions = new_sub;
        } else {
            mcp_log_warn("Failed to allocate subscription, skipping");
        }
        sub = sub->next;
    }
    mcp_mutex_unlock(data->session.subscription_mutex);

    // Copy in-flight messages
    mcp_mutex_lock(data->message_tracking.inflight_mutex);
    struct mqtt_inflight_message* inflight = data->message_tracking.inflight_messages;
    while (inflight) {
        struct mqtt_inflight_message* new_inflight = (struct mqtt_inflight_message*)malloc(sizeof(struct mqtt_inflight_message));
        if (new_inflight) {
            new_inflight->packet_id = inflight->packet_id;
            new_inflight->topic = mcp_strdup(inflight->topic);
            new_inflight->payload = malloc(inflight->payload_len);

            // Check if both allocations succeeded
            if (!new_inflight->topic || !new_inflight->payload) {
                // Clean up partial allocation
                free(new_inflight->topic);
                free(new_inflight->payload);
                free(new_inflight);
                mcp_log_warn("Failed to allocate in-flight message data, skipping");
                inflight = inflight->next;
                continue;
            }

            memcpy(new_inflight->payload, inflight->payload, inflight->payload_len);
            new_inflight->payload_len = inflight->payload_len;
            new_inflight->qos = inflight->qos;
            new_inflight->retain = inflight->retain;
            new_inflight->send_time = inflight->send_time;
            new_inflight->retry_count = inflight->retry_count;
            new_inflight->next = session_data.inflight_messages;
            session_data.inflight_messages = new_inflight;
        } else {
            mcp_log_warn("Failed to allocate in-flight message, skipping");
        }
        inflight = inflight->next;
    }
    mcp_mutex_unlock(data->message_tracking.inflight_mutex);

    // Save last packet ID
    session_data.last_packet_id = data->message_tracking.packet_id;

    // Save session
    int result = mqtt_session_save(data->base.config.client_id, &session_data);
    if (result == 0) {
        mcp_log_debug("Successfully saved session for client: %s", data->base.config.client_id);
    } else {
        mcp_log_error("Failed to save session for client: %s", data->base.config.client_id);
    }

    // Cleanup
    sub = session_data.subscriptions;
    while (sub) {
        struct mqtt_subscription* next = sub->next;
        free(sub->topic);
        free(sub);
        sub = next;
    }

    inflight = session_data.inflight_messages;
    while (inflight) {
        struct mqtt_inflight_message* next = inflight->next;
        free(inflight->topic);
        free(inflight->payload);
        free(inflight);
        inflight = next;
    }

    if (result == 0) {
        mcp_log_debug("Saved session for client: %s", data->base.config.client_id);
    } else {
        mcp_log_warn("Failed to save session for client: %s", data->base.config.client_id);
    }

    return result;
}

int mqtt_client_stop_connection(mcp_mqtt_client_transport_data_t* data) {
    if (!data) {
        return -1;
    }

    if (data->session_persist) {
        mqtt_client_save_session(data);
    }

    // Signal stop
    data->base.should_stop = true;

    mqtt_client_handle_state_change(data, MCP_MQTT_CLIENT_DISCONNECTED, "Stopped");

    return 0;
}

void* mqtt_client_reconnect_thread(void* arg) {
    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)arg;
    if (!data) {
        return NULL;
    }

    mcp_log_debug("MQTT client reconnect thread started");

    while (!data->base.should_stop && data->reconnect_state != MQTT_RECONNECT_IDLE) {
        // Wait for reconnect delay
        uint32_t delay_ms = mqtt_client_calculate_reconnect_delay(data);
        mcp_sleep_ms(delay_ms);

        if (data->base.should_stop) {
            break;
        }

        // Attempt reconnection
        mcp_log_info("Attempting MQTT client reconnection...");
        if (mqtt_client_start_connection(data) == 0) {
            mcp_log_info("MQTT client reconnected successfully");
            data->reconnect_state = MQTT_RECONNECT_IDLE;

            // Restore subscriptions
            mqtt_client_restore_subscriptions(data);
            break;
        } else {
            mcp_log_warn("MQTT client reconnection failed, will retry");
            data->base.connection_failures++;
        }
    }

    mcp_log_debug("MQTT client reconnect thread ended");
    return NULL;
}

int mqtt_client_schedule_reconnect(mcp_mqtt_client_transport_data_t* data) {
    if (!data || data->reconnect_state != MQTT_RECONNECT_IDLE) {
        return -1;
    }

    data->reconnect_state = MQTT_RECONNECT_SCHEDULED;

    // Create reconnect thread if not already running
    if (!data->reconnect_thread) {
        data->reconnect_thread = malloc(sizeof(mcp_thread_t));
        if (!data->reconnect_thread) {
            mcp_log_error("Failed to allocate memory for reconnect thread");
            return -1;
        }

        if (mcp_thread_create(data->reconnect_thread, mqtt_client_reconnect_thread, data) != 0) {
            mcp_log_error("Failed to create MQTT client reconnect thread");
            free(data->reconnect_thread);
            data->reconnect_thread = NULL;
            data->reconnect_state = MQTT_RECONNECT_IDLE;
            return -1;
        }
    }

    mcp_log_debug("MQTT client reconnection scheduled");
    return 0;
}

void mqtt_client_cancel_reconnect(mcp_mqtt_client_transport_data_t* data) {
    if (!data) {
        return;
    }

    data->reconnect_state = MQTT_RECONNECT_IDLE;

    if (data->reconnect_thread) {
        // Signal the thread to stop and wait for it
        mcp_cond_signal(data->reconnect_condition);
        mcp_thread_join(*data->reconnect_thread, NULL);
        free(data->reconnect_thread);
        data->reconnect_thread = NULL;
    }

    mcp_log_debug("MQTT client reconnection cancelled");
}

uint32_t mqtt_client_calculate_reconnect_delay(mcp_mqtt_client_transport_data_t* data) {
    if (!data) {
        return 1000;
    }

    // Exponential backoff with jitter
    uint32_t base_delay = 1000; // 1 second
    uint32_t max_delay = 60000;  // 60 seconds

    // Calculate exponential backoff: base_delay * 2^failures
    uint32_t delay = base_delay;
    for (uint32_t i = 0; i < data->base.connection_failures && delay < max_delay; i++) {
        delay *= 2;
        if (delay > max_delay) {
            delay = max_delay;
            break;
        }
    }

    // Add jitter (±25%) using standard rand()
    uint32_t jitter = delay / 4;
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)mcp_get_time_ms());
        seeded = true;
    }
    uint32_t random_offset = (rand() % (jitter * 2)) - jitter;
    delay += random_offset;

    mcp_log_debug("MQTT reconnect delay calculated: %u ms (failures: %u)",
                  delay, data->base.connection_failures);

    return delay;
}

void* mqtt_client_ping_thread(void* arg) {
    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)arg;
    if (!data) {
        return NULL;
    }

    mcp_log_debug("MQTT client ping thread started");

    while (data->monitoring.ping_thread_active && !data->base.should_stop) {
        // Wait for ping interval
        mcp_mutex_lock(data->monitoring.ping_mutex);
        mcp_cond_timedwait(data->monitoring.ping_condition, data->monitoring.ping_mutex,
                          data->monitoring.ping_interval_ms);
        mcp_mutex_unlock(data->monitoring.ping_mutex);

        if (!data->monitoring.ping_thread_active || data->base.should_stop) {
            break;
        }

        // Send ping if connected
        if (data->base.connection_state == MCP_MQTT_CLIENT_CONNECTED) {
            if (mqtt_client_send_ping(data) != 0) {
                mcp_log_warn("Failed to send MQTT ping");
            }
        }
    }

    mcp_log_debug("MQTT client ping thread ended");
    return NULL;
}

void* mqtt_client_session_cleanup_thread(void* arg) {
    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)arg;
    if (!data) {
        return NULL;
    }

    mcp_log_debug("MQTT client session cleanup thread started");

    while (data->session_cleanup_active && !data->base.should_stop) {
        // Wait for cleanup interval
        mcp_mutex_lock(data->session_cleanup_mutex);
        mcp_cond_timedwait(data->session_cleanup_condition, data->session_cleanup_mutex,
                          data->session_cleanup_interval_ms);
        mcp_mutex_unlock(data->session_cleanup_mutex);

        if (!data->session_cleanup_active || data->base.should_stop) {
            break;
        }

        // Perform session cleanup
        if (data->session_storage_path) {
            int cleaned = mqtt_session_cleanup_expired();
            if (cleaned > 0) {
                mcp_log_info("Cleaned %d expired MQTT sessions", cleaned);
            }
        }
    }

    mcp_log_debug("MQTT client session cleanup thread ended");
    return NULL;
}

int mqtt_client_send_ping(mcp_mqtt_client_transport_data_t* data) {
    if (!data || !data->base.wsi) {
        return -1;
    }

    // MQTT ping is typically handled automatically by libwebsockets
    // For now, we'll simulate ping functionality by checking connection status
    // In a full implementation, this could use lws_callback_on_writable to trigger a ping

    // Check if connection is still alive by attempting to write
    if (lws_callback_on_writable(data->base.wsi) < 0) {
        mcp_log_error("Failed to schedule MQTT ping write");
        return -1;
    }

    data->monitoring.pending_pings++;
    mcp_log_debug("MQTT ping scheduled (pending: %d)", data->monitoring.pending_pings);

    return 0;
}

void mqtt_client_handle_pong(mcp_mqtt_client_transport_data_t* data) {
    if (!data) {
        return;
    }

    if (data->monitoring.pending_pings > 0) {
        data->monitoring.pending_pings--;
    }

    mcp_log_debug("MQTT pong received (pending: %d)", data->monitoring.pending_pings);

    // Reset connection failure count on successful pong
    data->base.connection_failures = 0;
}

int mqtt_client_add_inflight_message(mcp_mqtt_client_transport_data_t* data,
                                    uint16_t packet_id, const char* topic,
                                    const void* payload, size_t payload_len,
                                    int qos, bool retain) {
    if (!data || !topic || !payload) {
        return -1;
    }

    // Check if we've reached the maximum in-flight messages
    mcp_mutex_lock(data->message_tracking.inflight_mutex);
    if ((uint32_t)data->message_tracking.inflight_count >= data->message_tracking.max_inflight) {
        mcp_mutex_unlock(data->message_tracking.inflight_mutex);
        mcp_log_warn("Maximum in-flight messages reached (%u)", data->message_tracking.max_inflight);
        return -1;
    }

    // Create new in-flight message
    struct mqtt_inflight_message* msg = malloc(sizeof(struct mqtt_inflight_message));
    if (!msg) {
        mcp_mutex_unlock(data->message_tracking.inflight_mutex);
        mcp_log_error("Failed to allocate in-flight message");
        return -1;
    }

    msg->packet_id = packet_id;
    msg->topic = mcp_strdup(topic);
    msg->payload = malloc(payload_len);
    if (!msg->topic || !msg->payload) {
        free(msg->topic);
        free(msg->payload);
        free(msg);
        mcp_mutex_unlock(data->message_tracking.inflight_mutex);
        mcp_log_error("Failed to allocate message data");
        return -1;
    }

    memcpy(msg->payload, payload, payload_len);
    msg->payload_len = payload_len;
    msg->qos = qos;
    msg->retain = retain;
    msg->send_time = mcp_get_time_ms();
    msg->retry_count = 0;

    // Add to linked list
    msg->next = data->message_tracking.inflight_messages;
    data->message_tracking.inflight_messages = msg;
    data->message_tracking.inflight_count++;

    mcp_mutex_unlock(data->message_tracking.inflight_mutex);

    mcp_log_debug("Added in-flight message: packet_id=%u, topic=%s", packet_id, topic);
    return 0;
}

void mqtt_client_remove_inflight_message(mcp_mqtt_client_transport_data_t* data,
                                        uint16_t packet_id) {
    if (!data) {
        return;
    }

    mcp_mutex_lock(data->message_tracking.inflight_mutex);

    struct mqtt_inflight_message* prev = NULL;
    struct mqtt_inflight_message* current = data->message_tracking.inflight_messages;

    while (current) {
        if (current->packet_id == packet_id) {
            // Remove from linked list
            if (prev) {
                prev->next = current->next;
            } else {
                data->message_tracking.inflight_messages = current->next;
            }

            // Free memory
            free(current->topic);
            free(current->payload);
            free(current);

            data->message_tracking.inflight_count--;

            mcp_log_debug("Removed in-flight message: packet_id=%u", packet_id);
            break;
        }

        prev = current;
        current = current->next;
    }

    mcp_mutex_unlock(data->message_tracking.inflight_mutex);
}

int mqtt_client_retry_inflight_messages(mcp_mqtt_client_transport_data_t* data) {
    if (!data) {
        return -1;
    }

    uint64_t current_time = mcp_get_time_ms();
    int retried_count = 0;

    mcp_mutex_lock(data->message_tracking.inflight_mutex);

    struct mqtt_inflight_message* msg = data->message_tracking.inflight_messages;
    while (msg) {
        // Check if message needs retry
        uint64_t elapsed = current_time - msg->send_time;
        if (elapsed > data->message_retry_interval_ms && msg->retry_count < data->max_message_retries) {
            // Retry the message
            if (data->base.wsi) {
                lws_mqtt_publish_param_t pub = {0};
                pub.topic = msg->topic;
                pub.topic_len = (uint16_t)strlen(msg->topic);
                pub.payload = msg->payload;
                pub.payload_len = (uint32_t)msg->payload_len;
                pub.qos = (lws_mqtt_qos_levels_t)msg->qos;
                pub.retain = msg->retain;

                if (lws_mqtt_client_send_publish(data->base.wsi, &pub, msg->payload, (uint32_t)msg->payload_len, 1) >= 0) {
                    msg->retry_count++;
                    msg->send_time = current_time;
                    retried_count++;
                    mcp_log_debug("Retried in-flight message: packet_id=%u, retry=%u",
                                  msg->packet_id, msg->retry_count);
                }
            }
        }

        msg = msg->next;
    }

    mcp_mutex_unlock(data->message_tracking.inflight_mutex);

    if (retried_count > 0) {
        mcp_log_debug("Retried %d in-flight messages", retried_count);
    }

    return retried_count;
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
    if (!data || !topic) {
        return -1;
    }

    mcp_mutex_lock(data->session.subscription_mutex);

    // Check if subscription already exists
    struct mqtt_subscription* existing = data->session.subscriptions;
    while (existing) {
        if (strcmp(existing->topic, topic) == 0) {
            // Update QoS if different
            if (existing->qos != qos) {
                existing->qos = qos;
                mcp_log_debug("Updated subscription QoS: topic=%s, qos=%d", topic, qos);
            }
            mcp_mutex_unlock(data->session.subscription_mutex);
            return 0;
        }
        existing = existing->next;
    }

    // Create new subscription
    struct mqtt_subscription* sub = malloc(sizeof(struct mqtt_subscription));
    if (!sub) {
        mcp_mutex_unlock(data->session.subscription_mutex);
        mcp_log_error("Failed to allocate subscription");
        return -1;
    }

    sub->topic = mcp_strdup(topic);
    if (!sub->topic) {
        free(sub);
        mcp_mutex_unlock(data->session.subscription_mutex);
        mcp_log_error("Failed to allocate subscription topic");
        return -1;
    }

    sub->qos = qos;
    sub->next = data->session.subscriptions;
    data->session.subscriptions = sub;

    mcp_mutex_unlock(data->session.subscription_mutex);

    mcp_log_debug("Added subscription: topic=%s, qos=%d", topic, qos);
    return 0;
}

void mqtt_client_remove_subscription(mcp_mqtt_client_transport_data_t* data,
                                   const char* topic) {
    if (!data || !topic) {
        return;
    }

    mcp_mutex_lock(data->session.subscription_mutex);

    struct mqtt_subscription* prev = NULL;
    struct mqtt_subscription* current = data->session.subscriptions;

    while (current) {
        if (strcmp(current->topic, topic) == 0) {
            // Remove from linked list
            if (prev) {
                prev->next = current->next;
            } else {
                data->session.subscriptions = current->next;
            }

            // Free memory
            free(current->topic);
            free(current);

            mcp_log_debug("Removed subscription: topic=%s", topic);
            break;
        }

        prev = current;
        current = current->next;
    }

    mcp_mutex_unlock(data->session.subscription_mutex);
}

int mqtt_client_restore_subscriptions(mcp_mqtt_client_transport_data_t* data) {
    if (!data || !data->base.wsi) {
        return -1;
    }

    int restored_count = 0;

    mcp_mutex_lock(data->session.subscription_mutex);

    struct mqtt_subscription* sub = data->session.subscriptions;
    while (sub) {
        // Restore subscription using libwebsockets MQTT API
        lws_mqtt_topic_elem_t topic_elem = {0};
        topic_elem.name = sub->topic;
        topic_elem.qos = (lws_mqtt_qos_levels_t)sub->qos;

        lws_mqtt_subscribe_param_t subscribe_param = {0};
        subscribe_param.num_topics = 1;
        subscribe_param.topic = &topic_elem;

        if (lws_mqtt_client_send_subcribe(data->base.wsi, &subscribe_param) >= 0) {
            restored_count++;
            mcp_log_debug("Restored subscription: topic=%s, qos=%d", sub->topic, sub->qos);
        } else {
            mcp_log_warn("Failed to restore subscription: topic=%s", sub->topic);
        }

        sub = sub->next;
    }

    mcp_mutex_unlock(data->session.subscription_mutex);

    mcp_log_info("Restored %d MQTT subscriptions", restored_count);
    return restored_count;
}

int mqtt_client_save_session_state(mcp_mqtt_client_transport_data_t* data) {
    if (!data) {
        return -1;
    }

    // Use the main session save function
    return mqtt_client_save_session(data);
}

int mqtt_client_load_session_state(mcp_mqtt_client_transport_data_t* data) {
    if (!data || !data->session_persist || !data->session_storage_path) {
        return -1;
    }

    mqtt_session_data_t session_data = {0};
    if (mqtt_session_load(data->base.config.client_id, &session_data) != 0) {
        mcp_log_debug("No session state to load for client: %s", data->base.config.client_id);
        return -1;
    }

    mcp_log_info("Loading session state for client: %s", data->base.config.client_id);

    // Restore subscriptions
    mcp_mutex_lock(data->session.subscription_mutex);
    struct mqtt_subscription* sub = session_data.subscriptions;
    while (sub) {
        struct mqtt_subscription* next = sub->next;

        // Add to client's subscription list
        struct mqtt_subscription* client_sub = malloc(sizeof(struct mqtt_subscription));
        if (client_sub) {
            client_sub->topic = mcp_strdup(sub->topic);
            client_sub->qos = sub->qos;
            client_sub->active = sub->active;
            client_sub->next = data->session.subscriptions;
            data->session.subscriptions = client_sub;
        }

        free(sub->topic);
        free(sub);
        sub = next;
    }
    mcp_mutex_unlock(data->session.subscription_mutex);

    // Restore in-flight messages
    mcp_mutex_lock(data->message_tracking.inflight_mutex);
    struct mqtt_inflight_message* inflight = session_data.inflight_messages;
    while (inflight) {
        struct mqtt_inflight_message* next = inflight->next;

        // Add to client's in-flight list
        struct mqtt_inflight_message* client_inflight = malloc(sizeof(struct mqtt_inflight_message));
        if (client_inflight) {
            client_inflight->packet_id = inflight->packet_id;
            client_inflight->topic = mcp_strdup(inflight->topic);
            client_inflight->payload = malloc(inflight->payload_len);
            if (client_inflight->payload) {
                memcpy(client_inflight->payload, inflight->payload, inflight->payload_len);
            }
            client_inflight->payload_len = inflight->payload_len;
            client_inflight->qos = inflight->qos;
            client_inflight->retain = inflight->retain;
            client_inflight->send_time = inflight->send_time;
            client_inflight->retry_count = inflight->retry_count;
            client_inflight->next = data->message_tracking.inflight_messages;
            data->message_tracking.inflight_messages = client_inflight;
            data->message_tracking.inflight_count++;
        }

        free(inflight->topic);
        free(inflight->payload);
        free(inflight);
        inflight = next;
    }
    mcp_mutex_unlock(data->message_tracking.inflight_mutex);

    // Restore last packet ID
    if (session_data.last_packet_id > 0) {
        data->message_tracking.packet_id = (uint16_t)session_data.last_packet_id;
    }

    mcp_log_info("Loaded session state for client: %s", data->base.config.client_id);
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
 
    // Validate session persistence settings
    if (config->persistent_session && (!config->session_storage_path ||
                                       strlen(config->session_storage_path) == 0)) {
        mcp_log_error("Session storage path must be provided when session persistence is enabled");
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

/**
 * @brief Saves the current session state for the MQTT client
 */
int mcp_mqtt_client_save_session(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
    return mqtt_client_save_session(data);
}

/**
 * @brief Loads session state for the MQTT client
 */
int mcp_mqtt_client_load_session(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
    return mqtt_client_load_session_state(data);
}

/**
 * @brief Deletes the session state for the MQTT client
 */
int mcp_mqtt_client_delete_session(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;

    if (!data->base.config.client_id) {
        return -1;
    }

    return mqtt_session_delete(data->base.config.client_id);
}

/**
 * @brief Checks if a session exists for the MQTT client
 */
bool mcp_mqtt_client_session_exists(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return false;
    }

    mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;

    if (!data->base.config.client_id) {
        return false;
    }

    return mqtt_session_exists(data->base.config.client_id);
}

/**
 * @brief Triggers cleanup of expired sessions
 */
int mcp_mqtt_client_cleanup_expired_sessions(void) {
    return mqtt_session_cleanup_expired();
}
