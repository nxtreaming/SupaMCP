#ifndef MCP_MQTT_CLIENT_TRANSPORT_H
#define MCP_MQTT_CLIENT_TRANSPORT_H

#include "mcp_transport.h"
#include "mcp_mqtt_transport.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT client-specific configuration structure.
 * 
 * This structure extends the basic MQTT configuration with
 * client-specific options for connecting to MQTT brokers.
 */
typedef struct mcp_mqtt_client_config {
    mcp_mqtt_config_t base;              /**< Base MQTT configuration */
    
    // Client-specific options
    bool auto_reconnect;                 /**< Whether to automatically reconnect on connection loss */
    uint32_t reconnect_delay_ms;         /**< Delay between reconnection attempts */
    uint32_t max_reconnect_attempts;     /**< Maximum number of reconnection attempts (0 = infinite) */
    double backoff_factor;               /**< Exponential backoff factor for reconnection delays */
    uint32_t max_reconnect_delay_ms;     /**< Maximum delay between reconnection attempts */
    bool randomize_reconnect_delay;      /**< Whether to add random jitter to reconnection delays */
    
    // Message handling options
    uint32_t max_inflight_messages;      /**< Maximum number of in-flight messages for QoS > 0 */
    uint32_t message_retry_interval_ms;  /**< Interval for retrying unacknowledged messages */
    uint32_t max_message_retries;        /**< Maximum number of message retries */
    
    // Session options
    bool persistent_session;             /**< Whether to use persistent sessions */
    const char* session_state_file;      /**< File to store session state (optional) */
    
    // Advanced options
    bool enable_metrics;                 /**< Whether to collect connection metrics */
    uint32_t ping_interval_ms;           /**< Interval for sending PING messages */
    uint32_t ping_timeout_ms;            /**< Timeout for PING responses */
} mcp_mqtt_client_config_t;

/**
 * @brief Default MQTT client configuration values.
 */
#define MCP_MQTT_CLIENT_CONFIG_DEFAULT { \
    .base = MCP_MQTT_CONFIG_DEFAULT, \
    .auto_reconnect = true, \
    .reconnect_delay_ms = 1000, \
    .max_reconnect_attempts = 0, \
    .backoff_factor = 2.0, \
    .max_reconnect_delay_ms = 30000, \
    .randomize_reconnect_delay = true, \
    .max_inflight_messages = 10, \
    .message_retry_interval_ms = 5000, \
    .max_message_retries = 3, \
    .persistent_session = false, \
    .session_state_file = NULL, \
    .enable_metrics = false, \
    .ping_interval_ms = 30000, \
    .ping_timeout_ms = 5000 \
}

/**
 * @brief MQTT client connection state enumeration.
 */
typedef enum {
    MCP_MQTT_CLIENT_DISCONNECTED = 0,   /**< Client is disconnected */
    MCP_MQTT_CLIENT_CONNECTING,         /**< Client is attempting to connect */
    MCP_MQTT_CLIENT_CONNECTED,          /**< Client is connected and ready */
    MCP_MQTT_CLIENT_RECONNECTING,       /**< Client is attempting to reconnect */
    MCP_MQTT_CLIENT_ERROR               /**< Client is in an error state */
} mcp_mqtt_client_state_t;

/**
 * @brief MQTT client connection statistics.
 */
typedef struct mcp_mqtt_client_stats {
    uint64_t messages_sent;              /**< Total number of messages sent */
    uint64_t messages_received;          /**< Total number of messages received */
    uint64_t bytes_sent;                 /**< Total bytes sent */
    uint64_t bytes_received;             /**< Total bytes received */
    uint64_t connection_attempts;        /**< Total connection attempts */
    uint64_t successful_connections;     /**< Number of successful connections */
    uint64_t connection_failures;        /**< Number of connection failures */
    uint64_t reconnection_attempts;      /**< Number of reconnection attempts */
    uint32_t current_inflight_messages;  /**< Current number of in-flight messages */
    uint32_t message_timeouts;           /**< Number of message timeouts */
    uint32_t message_retries;            /**< Number of message retries */
    uint64_t last_connect_time;          /**< Timestamp of last successful connection */
    uint64_t last_disconnect_time;       /**< Timestamp of last disconnection */
    uint32_t uptime_seconds;             /**< Current connection uptime in seconds */
} mcp_mqtt_client_stats_t;

/**
 * @brief Creates an MQTT client transport with extended configuration.
 * 
 * @param config MQTT client configuration structure
 * @return Pointer to the created transport, or NULL on failure
 */
mcp_transport_t* mcp_transport_mqtt_client_create_with_config(const mcp_mqtt_client_config_t* config);

/**
 * @brief Gets the current connection state of the MQTT client.
 * 
 * @param transport The MQTT client transport instance
 * @return The current connection state
 */
mcp_mqtt_client_state_t mcp_mqtt_client_get_state(mcp_transport_t* transport);

/**
 * @brief Gets connection statistics for the MQTT client.
 * 
 * @param transport The MQTT client transport instance
 * @param stats Pointer to structure to fill with statistics
 * @return 0 on success, -1 on error
 */
int mcp_mqtt_client_get_stats(mcp_transport_t* transport, mcp_mqtt_client_stats_t* stats);

/**
 * @brief Resets connection statistics for the MQTT client.
 * 
 * @param transport The MQTT client transport instance
 * @return 0 on success, -1 on error
 */
int mcp_mqtt_client_reset_stats(mcp_transport_t* transport);

/**
 * @brief Forces a reconnection attempt for the MQTT client.
 * 
 * This function will disconnect the client (if connected) and
 * attempt to reconnect immediately.
 * 
 * @param transport The MQTT client transport instance
 * @return 0 on success, -1 on error
 */
int mcp_mqtt_client_force_reconnect(mcp_transport_t* transport);

/**
 * @brief Sets the connection state callback for the MQTT client.
 * 
 * This callback will be invoked whenever the connection state changes.
 * 
 * @param transport The MQTT client transport instance
 * @param callback The callback function to set
 * @param user_data User data to pass to the callback
 * @return 0 on success, -1 on error
 */
typedef void (*mcp_mqtt_client_state_callback_t)(mcp_mqtt_client_state_t state, 
                                                 const char* reason, 
                                                 void* user_data);

int mcp_mqtt_client_set_state_callback(mcp_transport_t* transport,
                                      mcp_mqtt_client_state_callback_t callback,
                                      void* user_data);

/**
 * @brief Enables or disables automatic reconnection for the MQTT client.
 * 
 * @param transport The MQTT client transport instance
 * @param enable Whether to enable automatic reconnection
 * @return 0 on success, -1 on error
 */
int mcp_mqtt_client_set_auto_reconnect(mcp_transport_t* transport, bool enable);

/**
 * @brief Gets the broker information for the MQTT client.
 * 
 * @param transport The MQTT client transport instance
 * @param host Pointer to store the broker hostname (do not free)
 * @param port Pointer to store the broker port
 * @return 0 on success, -1 on error
 */
int mcp_mqtt_client_get_broker_info(mcp_transport_t* transport, 
                                   const char** host, 
                                   uint16_t* port);

#ifdef __cplusplus
}
#endif

#endif // MCP_MQTT_CLIENT_TRANSPORT_H
