#ifndef MQTT_TRANSPORT_INTERNAL_H
#define MQTT_TRANSPORT_INTERNAL_H

#include "mcp_mqtt_transport.h"
#include "transport_internal.h"
#include "libwebsockets.h"
#include "mcp_sync.h"
#include "mcp_thread_pool.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of concurrent MQTT clients for server transport
 */
#define MCP_MQTT_MAX_CLIENTS 1024

/**
 * @brief Maximum MQTT message size
 */
#define MCP_MQTT_MAX_MESSAGE_SIZE (1024 * 1024)  // 1MB

/**
 * @brief Maximum topic length
 */
#define MCP_MQTT_MAX_TOPIC_LENGTH 256

/**
 * @brief Default MQTT topic templates
 */
#define MCP_MQTT_DEFAULT_REQUEST_TOPIC_TEMPLATE "%srequest/%s"
#define MCP_MQTT_DEFAULT_RESPONSE_TOPIC_TEMPLATE "%sresponse/%s"
#define MCP_MQTT_DEFAULT_NOTIFICATION_TOPIC_TEMPLATE "%snotification/%s"

/**
 * @brief MQTT client session information
 */
typedef struct mcp_mqtt_client_session {
    char client_id[64];                  /**< MQTT client ID */
    char request_topic[MCP_MQTT_MAX_TOPIC_LENGTH];   /**< Client's request topic */
    char response_topic[MCP_MQTT_MAX_TOPIC_LENGTH];  /**< Client's response topic */
    char notification_topic[MCP_MQTT_MAX_TOPIC_LENGTH]; /**< Client's notification topic */
    struct lws* wsi;                     /**< libwebsockets instance for this client */
    volatile bool active;                /**< Whether this session is active */
    uint64_t last_activity;              /**< Timestamp of last activity */
    void* user_data;                     /**< User data associated with this session */
} mcp_mqtt_client_session_t;

/**
 * @brief MQTT message queue entry
 */
typedef struct mcp_mqtt_message_queue_entry {
    char* topic;                         /**< Message topic */
    void* payload;                       /**< Message payload */
    size_t payload_len;                  /**< Payload length */
    int qos;                             /**< Quality of Service level */
    bool retain;                         /**< Retain flag */
    uint64_t timestamp;                  /**< Message timestamp */
    uint32_t retry_count;                /**< Number of retry attempts */
    struct mcp_mqtt_message_queue_entry* next; /**< Next message in queue */
} mcp_mqtt_message_queue_entry_t;

/**
 * @brief MQTT transport data structure
 */
typedef struct mcp_mqtt_transport_data {
    // Configuration
    mcp_mqtt_config_t config;            /**< MQTT configuration */
    char* resolved_request_topic;        /**< Resolved request topic */
    char* resolved_response_topic;       /**< Resolved response topic */
    char* resolved_notification_topic;   /**< Resolved notification topic */
    
    // libwebsockets context and connection
    struct lws_context* context;         /**< libwebsockets context */
    struct lws* wsi;                     /**< libwebsockets instance */
    struct lws_protocols* protocols;     /**< MQTT protocol definition */
    
    // Connection state
    volatile int connection_state;       /**< Current connection state */
    volatile bool should_stop;           /**< Stop flag for threads */
    volatile bool is_server;             /**< Whether this is a server transport */
    
    // Threading
    mcp_thread_pool_t* thread_pool;      /**< Thread pool for message processing */
    mcp_thread_t* service_thread;        /**< libwebsockets service thread */
    mcp_thread_t* message_thread;        /**< Message processing thread */
    
    // Synchronization
    mcp_mutex_t* state_mutex;            /**< Mutex for state changes */
    mcp_mutex_t* message_mutex;          /**< Mutex for message queue */
    mcp_cond_t* state_condition;         /**< Condition variable for state changes */
    
    // Message handling
    mcp_mqtt_message_queue_entry_t* message_queue_head; /**< Message queue head */
    mcp_mqtt_message_queue_entry_t* message_queue_tail; /**< Message queue tail */
    volatile int message_queue_size;     /**< Current message queue size */
    uint32_t max_queue_size;             /**< Maximum message queue size */

    // Client sessions (for server transport)
    mcp_mqtt_client_session_t* client_sessions; /**< Array of client sessions */
    uint32_t max_clients;                /**< Maximum number of clients */
    volatile int active_clients;         /**< Number of active clients */
    mcp_mutex_t* clients_mutex;          /**< Mutex for client sessions */

    // Statistics (protected by stats_mutex)
    uint64_t messages_sent;              /**< Total messages sent */
    uint64_t messages_received;          /**< Total messages received */
    uint64_t bytes_sent;                 /**< Total bytes sent */
    uint64_t bytes_received;             /**< Total bytes received */
    uint64_t connection_attempts;        /**< Total connection attempts */
    uint64_t connection_failures;        /**< Total connection failures */
    mcp_mutex_t* stats_mutex;            /**< Mutex for statistics */
    
    // Callbacks
    mcp_transport_message_callback_t message_callback; /**< MCP message callback */
    void* callback_user_data;            /**< User data for callbacks */
    mcp_transport_error_callback_t error_callback; /**< Error callback */
    mcp_mqtt_message_handler_t custom_message_handler; /**< Custom message handler */
    void* custom_handler_user_data;      /**< User data for custom handler */
    
    // Reconnection (for client transport)
    bool auto_reconnect;                 /**< Whether to auto-reconnect */
    uint32_t reconnect_delay_ms;         /**< Reconnection delay */
    uint32_t max_reconnect_attempts;     /**< Maximum reconnection attempts */
    volatile uint32_t reconnect_attempts; /**< Current reconnection attempts */
    uint64_t last_connect_time;          /**< Last successful connection time */
    uint64_t last_disconnect_time;       /**< Last disconnection time */
    
} mcp_mqtt_transport_data_t;

/**
 * @brief MQTT protocol callback data
 */
typedef struct mcp_mqtt_protocol_data {
    mcp_mqtt_transport_data_t* transport_data; /**< Reference to transport data */
    char client_id[64];                  /**< Client ID for this connection */
    bool is_authenticated;               /**< Whether client is authenticated */
    uint64_t connect_time;               /**< Connection timestamp */
} mcp_mqtt_protocol_data_t;

// Internal function declarations

/**
 * @brief Initializes MQTT transport data structure
 */
int mqtt_transport_data_init(mcp_mqtt_transport_data_t* data, const mcp_mqtt_config_t* config, bool is_server);

/**
 * @brief Cleans up MQTT transport data structure
 */
void mqtt_transport_data_cleanup(mcp_mqtt_transport_data_t* data);

/**
 * @brief Resolves topic templates with client ID
 */
int mqtt_resolve_topics(mcp_mqtt_transport_data_t* data, const char* client_id);

/**
 * @brief Creates libwebsockets context for MQTT
 */
struct lws_context* mqtt_create_lws_context(mcp_mqtt_transport_data_t* data);

/**
 * @brief MQTT protocol callback function
 */
int mqtt_protocol_callback(struct lws* wsi, enum lws_callback_reasons reason,
                          void* user, void* in, size_t len);

/**
 * @brief Service thread function for libwebsockets
 */
void* mqtt_service_thread(void* arg);

/**
 * @brief Message processing thread function
 */
void* mqtt_message_thread(void* arg);

/**
 * @brief Enqueues a message for sending
 */
int mqtt_enqueue_message(mcp_mqtt_transport_data_t* data, const char* topic,
                        const void* payload, size_t payload_len, int qos, bool retain);

/**
 * @brief Dequeues and processes messages
 */
int mqtt_process_message_queue(mcp_mqtt_transport_data_t* data);

/**
 * @brief Handles incoming MQTT messages
 */
int mqtt_handle_incoming_message(mcp_mqtt_transport_data_t* data, const char* topic,
                                const void* payload, size_t payload_len);

/**
 * @brief Finds or creates a client session
 */
mcp_mqtt_client_session_t* mqtt_find_or_create_client_session(mcp_mqtt_transport_data_t* data,
                                                             const char* client_id);

/**
 * @brief Removes a client session
 */
void mqtt_remove_client_session(mcp_mqtt_transport_data_t* data, const char* client_id);

/**
 * @brief Validates MQTT configuration
 */
int mqtt_validate_config(const mcp_mqtt_config_t* config);

/**
 * @brief Generates a unique client ID if not provided
 */
char* mqtt_generate_client_id(void);

/**
 * @brief Converts MCP message to MQTT payload
 */
int mqtt_serialize_mcp_message(const void* mcp_data, size_t mcp_len, 
                              void** mqtt_payload, size_t* mqtt_len);

/**
 * @brief Converts MQTT payload to MCP message
 */
int mqtt_deserialize_mcp_message(const void* mqtt_payload, size_t mqtt_len,
                                void** mcp_data, size_t* mcp_len);

#ifdef __cplusplus
}
#endif

#endif // MQTT_TRANSPORT_INTERNAL_H
