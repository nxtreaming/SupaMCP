#ifndef MCP_MQTT_TRANSPORT_H
#define MCP_MQTT_TRANSPORT_H

#include "mcp_transport.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT transport configuration structure.
 * 
 * This structure contains all configuration options for MQTT transport,
 * including broker connection settings, authentication, SSL/TLS options,
 * and MCP-specific topic configuration.
 */
typedef struct mcp_mqtt_config {
    const char* host;                    /**< MQTT broker hostname or IP address */
    uint16_t port;                       /**< MQTT broker port (default: 1883 for non-SSL, 8883 for SSL) */
    const char* client_id;               /**< MQTT client ID (auto-generated if NULL) */
    const char* username;                /**< MQTT username (optional) */
    const char* password;                /**< MQTT password (optional) */
    const char* topic_prefix;            /**< Topic prefix for MCP messages (default: "mcp/") */
    const char* request_topic;           /**< Topic for MCP requests (default: "{prefix}request") */
    const char* response_topic;          /**< Topic for MCP responses (default: "{prefix}response") */
    const char* notification_topic;      /**< Topic for MCP notifications (default: "{prefix}notification") */
    uint16_t keep_alive;                 /**< MQTT keep-alive interval in seconds (default: 60) */
    bool clean_session;                  /**< MQTT clean session flag */
    bool use_ssl;                        /**< Whether to use SSL/TLS */
    const char* cert_path;               /**< Path to SSL certificate (optional) */
    const char* key_path;                /**< Path to SSL private key (optional) */
    const char* ca_cert_path;            /**< Path to CA certificate for verification (optional) */
    bool verify_ssl;                     /**< Whether to verify SSL certificates */
    uint32_t connect_timeout_ms;         /**< Connection timeout in milliseconds */
    uint32_t message_timeout_ms;         /**< Message timeout in milliseconds */
    int qos;                             /**< MQTT Quality of Service level (0, 1, or 2) */
    bool retain;                         /**< Whether to retain messages */
    const char* will_topic;              /**< Last Will and Testament topic (optional) */
    const char* will_message;            /**< Last Will and Testament message (optional) */
    int will_qos;                        /**< Last Will and Testament QoS level */
    bool will_retain;                    /**< Whether to retain Last Will and Testament message */
} mcp_mqtt_config_t;

/**
 * @brief Default MQTT configuration values.
 */
#define MCP_MQTT_CONFIG_DEFAULT { \
    .host = "localhost", \
    .port = 1883, \
    .client_id = NULL, \
    .username = NULL, \
    .password = NULL, \
    .topic_prefix = "mcp/", \
    .request_topic = NULL, \
    .response_topic = NULL, \
    .notification_topic = NULL, \
    .keep_alive = 60, \
    .clean_session = true, \
    .use_ssl = false, \
    .cert_path = NULL, \
    .key_path = NULL, \
    .ca_cert_path = NULL, \
    .verify_ssl = true, \
    .connect_timeout_ms = 30000, \
    .message_timeout_ms = 10000, \
    .qos = 1, \
    .retain = false, \
    .will_topic = NULL, \
    .will_message = NULL, \
    .will_qos = 0, \
    .will_retain = false \
}

// MQTT server transport (broker functionality) has been removed.
// Use external MQTT broker with MQTT client transport instead.

/**
 * @brief Creates an MQTT client transport.
 * 
 * This function creates an MQTT transport that connects to an MQTT broker
 * and communicates using MCP protocol over MQTT topics.
 * 
 * @param config MQTT configuration structure
 * @return Pointer to the created transport, or NULL on failure
 */
mcp_transport_t* mcp_transport_mqtt_client_create(const mcp_mqtt_config_t* config);

/**
 * @brief Gets the current connection status of an MQTT transport.
 * 
 * @param transport The MQTT transport instance
 * @return 1 if connected, 0 if disconnected, -1 on error
 */
int mcp_mqtt_transport_is_connected(mcp_transport_t* transport);

/**
 * @brief Gets the client ID used by the MQTT transport.
 * 
 * @param transport The MQTT transport instance
 * @return The client ID string, or NULL on error
 */
const char* mcp_mqtt_transport_get_client_id(mcp_transport_t* transport);

/**
 * @brief Sets a custom message handler for MQTT transport.
 * 
 * This allows handling of non-MCP MQTT messages that may be received
 * on subscribed topics.
 * 
 * @param transport The MQTT transport instance
 * @param handler Custom message handler function
 * @param user_data User data to pass to the handler
 * @return 0 on success, -1 on error
 */
typedef void (*mcp_mqtt_message_handler_t)(const char* topic, const void* payload, 
                                          size_t payload_len, void* user_data);

int mcp_mqtt_transport_set_message_handler(mcp_transport_t* transport,
                                          mcp_mqtt_message_handler_t handler,
                                          void* user_data);

/**
 * @brief Publishes a message to a specific MQTT topic.
 * 
 * This function allows publishing arbitrary messages to MQTT topics,
 * separate from the MCP protocol messages.
 * 
 * @param transport The MQTT transport instance
 * @param topic The MQTT topic to publish to
 * @param payload The message payload
 * @param payload_len The length of the payload
 * @param qos The Quality of Service level (0, 1, or 2)
 * @param retain Whether to retain the message
 * @return 0 on success, -1 on error
 */
int mcp_mqtt_transport_publish(mcp_transport_t* transport,
                              const char* topic,
                              const void* payload,
                              size_t payload_len,
                              int qos,
                              bool retain);

/**
 * @brief Subscribes to an MQTT topic.
 * 
 * @param transport The MQTT transport instance
 * @param topic The MQTT topic to subscribe to
 * @param qos The Quality of Service level for the subscription
 * @return 0 on success, -1 on error
 */
int mcp_mqtt_transport_subscribe(mcp_transport_t* transport,
                                const char* topic,
                                int qos);

/**
 * @brief Unsubscribes from an MQTT topic.
 * 
 * @param transport The MQTT transport instance
 * @param topic The MQTT topic to unsubscribe from
 * @return 0 on success, -1 on error
 */
int mcp_mqtt_transport_unsubscribe(mcp_transport_t* transport,
                                  const char* topic);

#ifdef __cplusplus
}
#endif

#endif // MCP_MQTT_TRANSPORT_H
