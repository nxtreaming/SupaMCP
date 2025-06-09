#include "mcp_mqtt_transport.h"
#include "mcp_mqtt_client_transport.h"
#include "mqtt_transport_internal.h"
#include "transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_sys_utils.h"
#include "mcp_string_utils.h"
#include "libwebsockets.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// Forward declarations for transport interface functions
static int mqtt_server_transport_init(mcp_transport_t* transport);
static void mqtt_server_transport_destroy(mcp_transport_t* transport);
static int mqtt_server_transport_start(mcp_transport_t* transport,
                                      mcp_transport_message_callback_t message_callback,
                                      void* user_data,
                                      mcp_transport_error_callback_t error_callback);
static int mqtt_server_transport_stop(mcp_transport_t* transport);

// libwebsockets MQTT protocol definition
static int mqtt_server_protocol_callback(struct lws* wsi, enum lws_callback_reasons reason,
                                        void* user, void* in, size_t len);

static struct lws_protocols mqtt_server_protocols[] = {
    {
        .name = "mqtt",
        .callback = mqtt_server_protocol_callback,
        .per_session_data_size = sizeof(mcp_mqtt_protocol_data_t),
        .rx_buffer_size = MCP_MQTT_MAX_MESSAGE_SIZE,
        .id = 0,
        .user = NULL,
        .tx_packet_size = 0
    },
    { NULL, NULL, 0, 0, 0, NULL, 0 } // terminator
};

/**
 * @brief Initializes MQTT transport data structure
 */
int mqtt_transport_data_init(mcp_mqtt_transport_data_t* data, const mcp_mqtt_config_t* config, bool is_server) {
    if (!data || !config) {
        return -1;
    }
    
    memset(data, 0, sizeof(mcp_mqtt_transport_data_t));
    
    // Copy configuration
    data->config = *config;
    
    // Duplicate string fields
    if (config->host) {
        data->config.host = mcp_strdup(config->host);
    }
    if (config->client_id) {
        data->config.client_id = mcp_strdup(config->client_id);
    }
    if (config->username) {
        data->config.username = mcp_strdup(config->username);
    }
    if (config->password) {
        data->config.password = mcp_strdup(config->password);
    }
    if (config->topic_prefix) {
        data->config.topic_prefix = mcp_strdup(config->topic_prefix);
    }
    
    // Initialize variables
    data->connection_state = 0;
    data->should_stop = false;
    data->is_server = is_server;
    data->message_queue_size = 0;
    data->active_clients = 0;
    data->messages_sent = 0;
    data->messages_received = 0;
    data->bytes_sent = 0;
    data->bytes_received = 0;
    data->connection_attempts = 0;
    data->connection_failures = 0;

    // Initialize thread pointers
    data->service_thread = NULL;
    data->message_thread = NULL;
    
    // Initialize synchronization objects
    data->state_mutex = mcp_mutex_create();
    data->message_mutex = mcp_mutex_create();
    data->clients_mutex = mcp_mutex_create();
    data->stats_mutex = mcp_mutex_create();
    data->state_condition = mcp_cond_create();

    if (!data->state_mutex || !data->message_mutex || !data->clients_mutex ||
        !data->stats_mutex || !data->state_condition) {
        mcp_log_error("Failed to create MQTT transport synchronization objects");
        // Clean up any created objects
        if (data->state_mutex) mcp_mutex_destroy(data->state_mutex);
        if (data->message_mutex) mcp_mutex_destroy(data->message_mutex);
        if (data->clients_mutex) mcp_mutex_destroy(data->clients_mutex);
        if (data->stats_mutex) mcp_mutex_destroy(data->stats_mutex);
        if (data->state_condition) mcp_cond_destroy(data->state_condition);
        return -1;
    }
    
    // Set default values
    data->max_queue_size = 1000;
    data->max_clients = is_server ? MCP_MQTT_MAX_CLIENTS : 1;
    
    // Allocate client sessions array for server
    if (is_server) {
        data->client_sessions = calloc(data->max_clients, sizeof(mcp_mqtt_client_session_t));
        if (!data->client_sessions) {
            mcp_log_error("Failed to allocate client sessions array");
            return -1;
        }
    }
    
    mcp_log_debug("MQTT transport data initialized (server: %s)", is_server ? "yes" : "no");
    
    return 0;
}

/**
 * @brief Cleans up MQTT transport data structure
 */
void mqtt_transport_data_cleanup(mcp_mqtt_transport_data_t* data) {
    if (!data) {
        return;
    }
    
    // Signal stop
    data->should_stop = true;
    
    // Clean up libwebsockets context
    if (data->context) {
        lws_context_destroy(data->context);
        data->context = NULL;
    }
    
    // Clean up message queue
    mcp_mqtt_message_queue_entry_t* entry = data->message_queue_head;
    while (entry) {
        mcp_mqtt_message_queue_entry_t* next = entry->next;
        free(entry->topic);
        free(entry->payload);
        free(entry);
        entry = next;
    }
    
    // Clean up client sessions
    if (data->client_sessions) {
        free(data->client_sessions);
        data->client_sessions = NULL;
    }
    
    // Clean up resolved topics
    free((void*)data->resolved_request_topic);
    free((void*)data->resolved_response_topic);
    free((void*)data->resolved_notification_topic);
    
    // Clean up configuration strings
    free((void*)data->config.host);
    free((void*)data->config.client_id);
    free((void*)data->config.username);
    free((void*)data->config.password);
    free((void*)data->config.topic_prefix);
    
    // Destroy synchronization objects
    if (data->state_mutex) mcp_mutex_destroy(data->state_mutex);
    if (data->message_mutex) mcp_mutex_destroy(data->message_mutex);
    if (data->clients_mutex) mcp_mutex_destroy(data->clients_mutex);
    if (data->stats_mutex) mcp_mutex_destroy(data->stats_mutex);
    if (data->state_condition) mcp_cond_destroy(data->state_condition);
    
    mcp_log_debug("MQTT transport data cleaned up");
}

/**
 * @brief Creates libwebsockets context for MQTT
 */
struct lws_context* mqtt_create_lws_context(mcp_mqtt_transport_data_t* data) {
    if (!data) {
        return NULL;
    }
    
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    
    info.port = data->config.port;
    info.iface = data->config.host;
    info.protocols = mqtt_server_protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
    info.user = data;
    
    if (data->config.use_ssl) {
        info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
        if (data->config.cert_path) {
            info.ssl_cert_filepath = data->config.cert_path;
        }
        if (data->config.key_path) {
            info.ssl_private_key_filepath = data->config.key_path;
        }
        if (data->config.ca_cert_path) {
            info.ssl_ca_filepath = data->config.ca_cert_path;
        }
    }
    
    // Set protocol user data to point to our transport data
    mqtt_server_protocols[0].user = data;
    
    struct lws_context* context = lws_create_context(&info);
    if (!context) {
        mcp_log_error("Failed to create libwebsockets context for MQTT");
        return NULL;
    }
    
    mcp_log_info("Created MQTT libwebsockets context on %s:%d", 
                data->config.host, data->config.port);
    
    return context;
}

/**
 * @brief MQTT protocol callback function
 */
static int mqtt_server_protocol_callback(struct lws* wsi, enum lws_callback_reasons reason,
                                        void* user, void* in, size_t len) {
    mcp_mqtt_protocol_data_t* protocol_data = (mcp_mqtt_protocol_data_t*)user;
    mcp_mqtt_transport_data_t* transport_data = NULL;
    
    // Get transport data from protocol user data
    if (lws_get_protocol(wsi) && lws_get_protocol(wsi)->user) {
        transport_data = (mcp_mqtt_transport_data_t*)lws_get_protocol(wsi)->user;
    }
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            mcp_log_debug("MQTT client connected");
            if (protocol_data && transport_data) {
                protocol_data->transport_data = transport_data;
                protocol_data->is_authenticated = false;
                protocol_data->connect_time = mcp_get_time_ms();
                snprintf(protocol_data->client_id, sizeof(protocol_data->client_id), 
                        "client_%p", (void*)wsi);
            }
            break;
            
        case LWS_CALLBACK_CLOSED:
            mcp_log_debug("MQTT client disconnected");
            if (protocol_data && transport_data) {
                mqtt_remove_client_session(transport_data, protocol_data->client_id);
            }
            break;
            
        case LWS_CALLBACK_RECEIVE:
            if (protocol_data && transport_data && in && len > 0) {
                // Handle incoming MQTT message
                // This is a placeholder - actual MQTT message parsing would be more complex
                mcp_log_debug("Received MQTT data: %zu bytes", len);
                mqtt_handle_incoming_message(transport_data, "unknown", in, len);
            }
            break;
            
        case LWS_CALLBACK_SERVER_WRITEABLE:
            // Handle outgoing messages
            if (transport_data) {
                mqtt_process_message_queue(transport_data);
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}

/**
 * @brief Service thread function for libwebsockets
 */
void* mqtt_service_thread(void* arg) {
    mcp_mqtt_transport_data_t* data = (mcp_mqtt_transport_data_t*)arg;
    if (!data) {
        return NULL;
    }
    
    mcp_log_debug("MQTT service thread started");
    
    while (!data->should_stop) {
        if (data->context) {
            lws_service(data->context, 50); // 50ms timeout
        } else {
            mcp_sleep_ms(50);
        }
    }
    
    mcp_log_debug("MQTT service thread stopped");
    return NULL;
}

/**
 * @brief Finds or creates a client session
 */
mcp_mqtt_client_session_t* mqtt_find_or_create_client_session(mcp_mqtt_transport_data_t* data,
                                                             const char* client_id) {
    if (!data || !client_id || !data->client_sessions) {
        return NULL;
    }
    
    mcp_mutex_lock(data->clients_mutex);

    // First, try to find existing session
    for (uint32_t i = 0; i < data->max_clients; i++) {
        if (data->client_sessions[i].active &&
            strcmp(data->client_sessions[i].client_id, client_id) == 0) {
            mcp_mutex_unlock(data->clients_mutex);
            return &data->client_sessions[i];
        }
    }

    // Find empty slot for new session
    for (uint32_t i = 0; i < data->max_clients; i++) {
        if (!data->client_sessions[i].active) {
            mcp_mqtt_client_session_t* session = &data->client_sessions[i];
            
            strncpy(session->client_id, client_id, sizeof(session->client_id) - 1);
            session->client_id[sizeof(session->client_id) - 1] = '\0';
            
            // Resolve topics for this client
            mqtt_resolve_topics(data, client_id);
            if (data->resolved_request_topic) {
                strncpy(session->request_topic, data->resolved_request_topic, 
                       sizeof(session->request_topic) - 1);
            }
            if (data->resolved_response_topic) {
                strncpy(session->response_topic, data->resolved_response_topic, 
                       sizeof(session->response_topic) - 1);
            }
            if (data->resolved_notification_topic) {
                strncpy(session->notification_topic, data->resolved_notification_topic, 
                       sizeof(session->notification_topic) - 1);
            }
            
            session->last_activity = mcp_get_time_ms();
            session->active = true;
            data->active_clients++;

            mcp_mutex_unlock(data->clients_mutex);
            mcp_log_debug("Created new MQTT client session: %s", client_id);
            return session;
        }
    }

    mcp_mutex_unlock(data->clients_mutex);
    mcp_log_warn("No available slots for new MQTT client session: %s", client_id);
    return NULL;
}

/**
 * @brief Removes a client session
 */
void mqtt_remove_client_session(mcp_mqtt_transport_data_t* data, const char* client_id) {
    if (!data || !client_id || !data->client_sessions) {
        return;
    }
    
    mcp_mutex_lock(data->clients_mutex);

    for (uint32_t i = 0; i < data->max_clients; i++) {
        if (data->client_sessions[i].active &&
            strcmp(data->client_sessions[i].client_id, client_id) == 0) {
            data->client_sessions[i].active = false;
            memset(&data->client_sessions[i], 0, sizeof(mcp_mqtt_client_session_t));
            data->active_clients--;
            mcp_log_debug("Removed MQTT client session: %s", client_id);
            break;
        }
    }

    mcp_mutex_unlock(data->clients_mutex);
}

/**
 * @brief Initializes MQTT server transport
 */
static int mqtt_server_transport_init(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_transport_data_t* data = (mcp_mqtt_transport_data_t*)transport->transport_data;

    // Validate configuration
    if (mqtt_validate_config(&data->config) != 0) {
        return -1;
    }

    // Create libwebsockets context
    data->context = mqtt_create_lws_context(data);
    if (!data->context) {
        return -1;
    }

    mcp_log_info("MQTT server transport initialized");
    return 0;
}

/**
 * @brief Destroys MQTT server transport
 */
static void mqtt_server_transport_destroy(mcp_transport_t* transport) {
    if (!transport) {
        return;
    }

    if (transport->transport_data) {
        mcp_mqtt_transport_data_t* data = (mcp_mqtt_transport_data_t*)transport->transport_data;
        mqtt_transport_data_cleanup(data);
        free(data);
        transport->transport_data = NULL;
    }

    mcp_log_info("MQTT server transport destroyed");
}

/**
 * @brief Starts MQTT server transport
 */
static int mqtt_server_transport_start(mcp_transport_t* transport,
                                      mcp_transport_message_callback_t message_callback,
                                      void* user_data,
                                      mcp_transport_error_callback_t error_callback) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_transport_data_t* data = (mcp_mqtt_transport_data_t*)transport->transport_data;

    // Store callbacks
    data->message_callback = message_callback;
    data->callback_user_data = user_data;
    data->error_callback = error_callback;

    // Start service thread
    data->service_thread = malloc(sizeof(mcp_thread_t));
    if (!data->service_thread) {
        mcp_log_error("Failed to allocate memory for MQTT service thread");
        return -1;
    }

    if (mcp_thread_create(data->service_thread, mqtt_service_thread, data) != 0) {
        mcp_log_error("Failed to create MQTT service thread");
        free(data->service_thread);
        data->service_thread = NULL;
        return -1;
    }

    mcp_log_info("MQTT server transport started on %s:%d",
                data->config.host, data->config.port);
    return 0;
}

/**
 * @brief Stops MQTT server transport
 */
static int mqtt_server_transport_stop(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_transport_data_t* data = (mcp_mqtt_transport_data_t*)transport->transport_data;

    // Signal stop
    data->should_stop = true;

    // Wait for service thread to finish
    if (data->service_thread) {
        mcp_thread_join(*data->service_thread, NULL);
        free(data->service_thread);
        data->service_thread = NULL;
    }

    mcp_log_info("MQTT server transport stopped");
    return 0;
}

/**
 * @brief Creates an MQTT server transport (broker functionality)
 */
mcp_transport_t* mcp_transport_mqtt_create(const mcp_mqtt_config_t* config) {
    if (!config) {
        mcp_log_error("MQTT config is required");
        return NULL;
    }

    // Allocate transport structure
    mcp_transport_t* transport = malloc(sizeof(mcp_transport_t));
    if (!transport) {
        mcp_log_error("Failed to allocate MQTT transport");
        return NULL;
    }

    memset(transport, 0, sizeof(mcp_transport_t));

    // Allocate transport data
    mcp_mqtt_transport_data_t* data = malloc(sizeof(mcp_mqtt_transport_data_t));
    if (!data) {
        mcp_log_error("Failed to allocate MQTT transport data");
        free(transport);
        return NULL;
    }

    // Initialize transport data
    if (mqtt_transport_data_init(data, config, true) != 0) {
        mcp_log_error("Failed to initialize MQTT transport data");
        free(data);
        free(transport);
        return NULL;
    }

    // Set transport properties
    transport->type = MCP_TRANSPORT_TYPE_SERVER;
    transport->protocol_type = MCP_TRANSPORT_PROTOCOL_MQTT;
    transport->transport_data = data;

    // Set server interface functions
    transport->server.init = mqtt_server_transport_init;
    transport->server.destroy = mqtt_server_transport_destroy;
    transport->server.start = mqtt_server_transport_start;
    transport->server.stop = mqtt_server_transport_stop;

    mcp_log_info("Created MQTT server transport");
    return transport;
}

/**
 * @brief Creates an MQTT client transport
 */
mcp_transport_t* mcp_transport_mqtt_client_create(const mcp_mqtt_config_t* config) {
    if (!config) {
        mcp_log_error("MQTT config is required");
        return NULL;
    }

    // Create client configuration with default values
    mcp_mqtt_client_config_t client_config = MCP_MQTT_CLIENT_CONFIG_DEFAULT;
    client_config.base = *config;

    // Use the extended client creation function
    return mcp_transport_mqtt_client_create_with_config(&client_config);
}

/**
 * @brief Gets the current connection status of an MQTT transport
 */
int mcp_mqtt_transport_is_connected(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_transport_data_t* data = (mcp_mqtt_transport_data_t*)transport->transport_data;
    return data->connection_state > 0 ? 1 : 0;
}

/**
 * @brief Gets the client ID used by the MQTT transport
 */
const char* mcp_mqtt_transport_get_client_id(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return NULL;
    }

    mcp_mqtt_transport_data_t* data = (mcp_mqtt_transport_data_t*)transport->transport_data;
    return data->config.client_id;
}

/**
 * @brief Sets a custom message handler for MQTT transport
 */
int mcp_mqtt_transport_set_message_handler(mcp_transport_t* transport,
                                          mcp_mqtt_message_handler_t handler,
                                          void* user_data) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    mcp_mqtt_transport_data_t* data = (mcp_mqtt_transport_data_t*)transport->transport_data;
    data->custom_message_handler = handler;
    data->custom_handler_user_data = user_data;

    return 0;
}

/**
 * @brief Publishes a message to a specific MQTT topic
 */
int mcp_mqtt_transport_publish(mcp_transport_t* transport,
                              const char* topic,
                              const void* payload,
                              size_t payload_len,
                              int qos,
                              bool retain) {
    if (!transport || !transport->transport_data || !topic || !payload) {
        return -1;
    }

    mcp_mqtt_transport_data_t* data = (mcp_mqtt_transport_data_t*)transport->transport_data;
    return mqtt_enqueue_message(data, topic, payload, payload_len, qos, retain);
}

/**
 * @brief Subscribes to an MQTT topic
 */
int mcp_mqtt_transport_subscribe(mcp_transport_t* transport,
                                const char* topic,
                                int qos) {
    if (!transport || !transport->transport_data || !topic) {
        return -1;
    }

    // This is a placeholder - actual subscription would use libwebsockets MQTT API
    mcp_log_debug("MQTT subscribe to topic: %s (QoS: %d)", topic, qos);
    return 0;
}

/**
 * @brief Unsubscribes from an MQTT topic
 */
int mcp_mqtt_transport_unsubscribe(mcp_transport_t* transport,
                                  const char* topic) {
    if (!transport || !transport->transport_data || !topic) {
        return -1;
    }

    // This is a placeholder - actual unsubscription would use libwebsockets MQTT API
    mcp_log_debug("MQTT unsubscribe from topic: %s", topic);
    return 0;
}
