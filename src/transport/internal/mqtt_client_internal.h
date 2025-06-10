#ifndef MQTT_CLIENT_INTERNAL_H
#define MQTT_CLIENT_INTERNAL_H

#include "mcp_mqtt_client_transport.h"
#include "mqtt_transport_internal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT client-specific transport data
 */
typedef struct mcp_mqtt_client_transport_data {
    mcp_mqtt_transport_data_t base;      /**< Base MQTT transport data */
    
    // Client-specific configuration
    mcp_mqtt_client_config_t client_config; /**< Client configuration */
    
    // Reconnection state
    volatile int reconnect_state;        /**< Current reconnection state */
    mcp_thread_t* reconnect_thread;      /**< Reconnection thread */
    mcp_cond_t* reconnect_condition;     /**< Condition for reconnection events */
    mcp_mutex_t* reconnect_mutex;        /**< Mutex for reconnection state */
    
    // Message tracking for QoS > 0
    struct {
        uint16_t packet_id;              /**< Next packet ID to use */
        mcp_mutex_t* packet_mutex;       /**< Mutex for packet ID generation */
        
        // In-flight message tracking
        struct mqtt_inflight_message {
            uint16_t packet_id;          /**< Packet ID */
            char* topic;                 /**< Message topic */
            void* payload;               /**< Message payload */
            size_t payload_len;          /**< Payload length */
            int qos;                     /**< Quality of Service */
            bool retain;                 /**< Retain flag */
            uint64_t send_time;          /**< Time message was sent */
            uint32_t retry_count;        /**< Number of retries */
            struct mqtt_inflight_message* next; /**< Next in list */
        }* inflight_messages;            /**< List of in-flight messages */
        
        mcp_mutex_t* inflight_mutex;     /**< Mutex for in-flight messages */
        volatile int inflight_count;     /**< Number of in-flight messages */
        uint32_t max_inflight;           /**< Maximum in-flight messages */
    } message_tracking;
    
    // Session state
    struct {
        bool persistent;                 /**< Whether session is persistent */
        char* state_file;                /**< File to store session state */
        mcp_mutex_t* state_mutex;        /**< Mutex for session state */
        
        // Subscription state
        struct mqtt_subscription {
            char* topic;                 /**< Subscribed topic */
            int qos;                     /**< Subscription QoS */
            bool active;                 /**< Whether subscription is active */
            struct mqtt_subscription* next; /**< Next subscription */
        }* subscriptions;                /**< List of subscriptions */
        
        mcp_mutex_t* subscription_mutex; /**< Mutex for subscriptions */
    } session;
    
    // Connection monitoring
    struct {
        mcp_thread_t* ping_thread;       /**< Ping monitoring thread */
        volatile bool ping_thread_active; /**< Whether ping thread is running */
        uint64_t last_ping_time;         /**< Last ping sent time */
        uint64_t last_pong_time;         /**< Last pong received time */
        volatile int pending_pings;      /**< Number of pending pings */
        uint32_t ping_interval_ms;       /**< Ping interval */
        uint32_t ping_timeout_ms;        /**< Ping timeout */
        mcp_cond_t* ping_condition;      /**< Condition for ping events */
        mcp_mutex_t* ping_mutex;         /**< Mutex for ping state */
    } monitoring;
    
    // Statistics (extends base statistics)
    mcp_mqtt_client_stats_t stats;       /**< Client-specific statistics */
    mcp_mutex_t* stats_mutex;            /**< Mutex for statistics */
    
    // Callbacks
    mcp_mqtt_client_state_callback_t state_callback; /**< State change callback */
    void* state_callback_user_data;     /**< User data for state callback */
    
    // Advanced features
    bool metrics_enabled;                /**< Whether metrics collection is enabled */
    uint32_t message_retry_interval_ms;  /**< Message retry interval */
    uint32_t max_message_retries;        /**< Maximum message retries */

    char* session_storage_path;         // Path for session storage
    bool session_persist;               // Whether to persist sessions

    // Session cleanup
    mcp_thread_t* session_cleanup_thread; /**< Session cleanup thread */
    volatile bool session_cleanup_active; /**< Whether cleanup thread is running */
    mcp_cond_t* session_cleanup_condition; /**< Condition for cleanup events */
    mcp_mutex_t* session_cleanup_mutex;   /**< Mutex for cleanup state */
    uint32_t session_cleanup_interval_ms; /**< Cleanup interval in milliseconds */
} mcp_mqtt_client_transport_data_t;

/**
 * @brief MQTT client reconnection states
 */
typedef enum {
    MQTT_RECONNECT_IDLE = 0,            /**< Not reconnecting */
    MQTT_RECONNECT_SCHEDULED,           /**< Reconnection scheduled */
    MQTT_RECONNECT_IN_PROGRESS,         /**< Reconnection in progress */
    MQTT_RECONNECT_FAILED,              /**< Reconnection failed */
    MQTT_RECONNECT_CANCELLED            /**< Reconnection cancelled */
} mqtt_reconnect_state_t;

// Internal function declarations

/**
 * @brief Initializes MQTT client transport data
 */
int mqtt_client_transport_data_init(mcp_mqtt_client_transport_data_t* data, 
                                   const mcp_mqtt_client_config_t* config);

/**
 * @brief Cleans up MQTT client transport data
 */
void mqtt_client_transport_data_cleanup(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Starts the MQTT client connection
 */
int mqtt_client_start_connection(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Stops the MQTT client connection
 */
int mqtt_client_stop_connection(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Handles connection state changes
 */
void mqtt_client_handle_state_change(mcp_mqtt_client_transport_data_t* data,
                                    mcp_mqtt_client_state_t new_state,
                                    const char* reason);

/**
 * @brief Reconnection thread function
 */
void* mqtt_client_reconnect_thread(void* arg);

/**
 * @brief Schedules a reconnection attempt
 */
int mqtt_client_schedule_reconnect(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Cancels any pending reconnection
 */
void mqtt_client_cancel_reconnect(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Calculates reconnection delay with backoff
 */
uint32_t mqtt_client_calculate_reconnect_delay(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Ping monitoring thread function
 */
void* mqtt_client_ping_thread(void* arg);

/**
 * @brief Session cleanup thread function
 */
void* mqtt_client_session_cleanup_thread(void* arg);

/**
 * @brief Sends a ping message
 */
int mqtt_client_send_ping(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Handles ping response
 */
void mqtt_client_handle_pong(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Adds a message to in-flight tracking
 */
int mqtt_client_add_inflight_message(mcp_mqtt_client_transport_data_t* data,
                                    uint16_t packet_id, const char* topic,
                                    const void* payload, size_t payload_len,
                                    int qos, bool retain);

/**
 * @brief Removes a message from in-flight tracking
 */
void mqtt_client_remove_inflight_message(mcp_mqtt_client_transport_data_t* data,
                                        uint16_t packet_id);

/**
 * @brief Retries timed-out in-flight messages
 */
int mqtt_client_retry_inflight_messages(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Generates next packet ID
 */
uint16_t mqtt_client_next_packet_id(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Adds a subscription to the session
 */
int mqtt_client_add_subscription(mcp_mqtt_client_transport_data_t* data,
                               const char* topic, int qos);

/**
 * @brief Removes a subscription from the session
 */
void mqtt_client_remove_subscription(mcp_mqtt_client_transport_data_t* data,
                                   const char* topic);

/**
 * @brief Restores subscriptions after reconnection
 */
int mqtt_client_restore_subscriptions(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Saves session state to file
 */
int mqtt_client_save_session_state(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Loads session state from file
 */
int mqtt_client_load_session_state(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Updates client statistics
 */
void mqtt_client_update_stats(mcp_mqtt_client_transport_data_t* data,
                            bool message_sent, bool message_received,
                            size_t bytes);

/**
 * @brief Resets client statistics
 */
void mqtt_client_reset_stats(mcp_mqtt_client_transport_data_t* data);

/**
 * @brief Validates client configuration
 */
int mqtt_client_validate_config(const mcp_mqtt_client_config_t* config);

#ifdef __cplusplus
}
#endif

#endif // MQTT_CLIENT_INTERNAL_H
