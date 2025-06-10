#include "mcp_mqtt_transport.h"
#include "mcp_mqtt_client_transport.h"
#include "internal/transport_internal.h"
#include "internal/mqtt_client_internal.h"
#include "internal/mqtt_transport_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_sys_utils.h"
#include "mcp_string_utils.h"
#include "libwebsockets.h"
#include <stdlib.h>
#include <string.h>

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
    if (config->request_topic) {
        data->config.request_topic = mcp_strdup(config->request_topic);
    }
    if (config->response_topic) {
        data->config.response_topic = mcp_strdup(config->response_topic);
    }
    if (config->notification_topic) {
        data->config.notification_topic = mcp_strdup(config->notification_topic);
    }
    if (config->cert_path) {
        data->config.cert_path = mcp_strdup(config->cert_path);
    }
    if (config->key_path) {
        data->config.key_path = mcp_strdup(config->key_path);
    }
    if (config->ca_cert_path) {
        data->config.ca_cert_path = mcp_strdup(config->ca_cert_path);
    }
    if (config->will_topic) {
        data->config.will_topic = mcp_strdup(config->will_topic);
    }
    if (config->will_message) {
        data->config.will_message = mcp_strdup(config->will_message);
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

    // Allocate client sessions array for server (not used for client)
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

    // Free string fields
    if (data->config.host) {
        free((void*)data->config.host);
        data->config.host = NULL;
    }
    if (data->config.client_id) {
        free((void*)data->config.client_id);
        data->config.client_id = NULL;
    }
    if (data->config.username) {
        free((void*)data->config.username);
        data->config.username = NULL;
    }
    if (data->config.password) {
        free((void*)data->config.password);
        data->config.password = NULL;
    }
    if (data->config.topic_prefix) {
        free((void*)data->config.topic_prefix);
        data->config.topic_prefix = NULL;
    }
    if (data->config.request_topic) {
        free((void*)data->config.request_topic);
        data->config.request_topic = NULL;
    }
    if (data->config.response_topic) {
        free((void*)data->config.response_topic);
        data->config.response_topic = NULL;
    }
    if (data->config.notification_topic) {
        free((void*)data->config.notification_topic);
        data->config.notification_topic = NULL;
    }
    if (data->config.cert_path) {
        free((void*)data->config.cert_path);
        data->config.cert_path = NULL;
    }
    if (data->config.key_path) {
        free((void*)data->config.key_path);
        data->config.key_path = NULL;
    }
    if (data->config.ca_cert_path) {
        free((void*)data->config.ca_cert_path);
        data->config.ca_cert_path = NULL;
    }
    if (data->config.will_topic) {
        free((void*)data->config.will_topic);
        data->config.will_topic = NULL;
    }
    if (data->config.will_message) {
        free((void*)data->config.will_message);
        data->config.will_message = NULL;
    }

    // Free resolved topics
    if (data->resolved_request_topic) {
        free(data->resolved_request_topic);
        data->resolved_request_topic = NULL;
    }
    if (data->resolved_response_topic) {
        free(data->resolved_response_topic);
        data->resolved_response_topic = NULL;
    }
    if (data->resolved_notification_topic) {
        free(data->resolved_notification_topic);
        data->resolved_notification_topic = NULL;
    }

    // Destroy libwebsockets context
    if (data->context) {
        lws_context_destroy(data->context);
        data->context = NULL;
    }

    // Free client sessions array
    if (data->client_sessions) {
        free(data->client_sessions);
        data->client_sessions = NULL;
    }

    // Destroy synchronization objects
    if (data->state_mutex) {
        mcp_mutex_destroy(data->state_mutex);
        data->state_mutex = NULL;
    }
    if (data->message_mutex) {
        mcp_mutex_destroy(data->message_mutex);
        data->message_mutex = NULL;
    }
    if (data->clients_mutex) {
        mcp_mutex_destroy(data->clients_mutex);
        data->clients_mutex = NULL;
    }
    if (data->stats_mutex) {
        mcp_mutex_destroy(data->stats_mutex);
        data->stats_mutex = NULL;
    }
    if (data->state_condition) {
        mcp_cond_destroy(data->state_condition);
        data->state_condition = NULL;
    }

    mcp_log_debug("MQTT transport data cleaned up");
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
 * @brief Creates libwebsockets context for MQTT
 */
struct lws_context* mqtt_create_lws_context(mcp_mqtt_transport_data_t* data) {
    if (!data) {
        return NULL;
    }

    struct lws_context_creation_info info = {0};

    // Set basic context options
    info.port = CONTEXT_PORT_NO_LISTEN;  // Client mode
    info.protocols = data->protocols;
    info.user = data;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    // Configure SSL if enabled
    if (data->config.use_ssl) {
        info.options |= LWS_SERVER_OPTION_REQUIRE_VALID_OPENSSL_CLIENT_CERT;

        if (data->config.cert_path) {
            info.client_ssl_cert_filepath = data->config.cert_path;
        }
        if (data->config.key_path) {
            info.client_ssl_private_key_filepath = data->config.key_path;
        }
        if (data->config.ca_cert_path) {
            info.client_ssl_ca_filepath = data->config.ca_cert_path;
        }

        if (!data->config.verify_ssl) {
            info.options |= LWS_SERVER_OPTION_PEER_CERT_NOT_REQUIRED;
        }
    }

    // Set timeouts
    info.ka_time = data->config.keep_alive;
    info.ka_probes = 3;
    info.ka_interval = 5;

    // Create context
    struct lws_context* context = lws_create_context(&info);
    if (!context) {
        mcp_log_error("Failed to create libwebsockets context for MQTT");
        return NULL;
    }

    mcp_log_debug("Created libwebsockets context for MQTT");
    return context;
}

/**
 * @brief Gets the current connection status of an MQTT transport
 */
int mcp_mqtt_transport_is_connected(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return -1;
    }

    // For MQTT transport, check the connection state
    if (transport->protocol_type == MCP_TRANSPORT_PROTOCOL_MQTT) {
        mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
        return (data->base.connection_state == MCP_MQTT_CLIENT_CONNECTED) ? 1 : 0;
    }

    return 0;
}

/**
 * @brief Gets the client ID used by the MQTT transport
 */
const char* mcp_mqtt_transport_get_client_id(mcp_transport_t* transport) {
    if (!transport || !transport->transport_data) {
        return NULL;
    }

    // For MQTT transport, return the client ID from configuration
    if (transport->protocol_type == MCP_TRANSPORT_PROTOCOL_MQTT) {
        mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
        return data->base.config.client_id;
    }

    return NULL;
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

    // For MQTT transport, store the custom handler
    if (transport->protocol_type == MCP_TRANSPORT_PROTOCOL_MQTT) {
        mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
        data->base.custom_message_handler = handler;
        data->base.custom_handler_user_data = user_data;
        return 0;
    }

    return -1;
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

    // For MQTT transport, delegate to the client transport's publish function
    if (transport->protocol_type == MCP_TRANSPORT_PROTOCOL_MQTT) {
        mcp_mqtt_client_transport_data_t* data = (mcp_mqtt_client_transport_data_t*)transport->transport_data;
        return mqtt_enqueue_message(&data->base, topic, payload, payload_len, qos, retain);
    }

    return -1;
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

    // For MQTT transport, delegate to the client transport's subscribe function
    if (transport->protocol_type == MCP_TRANSPORT_PROTOCOL_MQTT) {
        // TODO: Implement actual MQTT subscription using libwebsockets MQTT API
        mcp_log_debug("MQTT subscribe to topic: %s (QoS: %d)", topic, qos);
        return 0;
    }

    return -1;
}

/**
 * @brief Unsubscribes from an MQTT topic
 */
int mcp_mqtt_transport_unsubscribe(mcp_transport_t* transport,
                                  const char* topic) {
    if (!transport || !transport->transport_data || !topic) {
        return -1;
    }

    // For MQTT transport, delegate to the client transport's unsubscribe function
    if (transport->protocol_type == MCP_TRANSPORT_PROTOCOL_MQTT) {
        // TODO: Implement actual MQTT unsubscription using libwebsockets MQTT API
        mcp_log_debug("MQTT unsubscribe from topic: %s", topic);
        return 0;
    }

    return -1;
}
