#ifndef MQTT_SESSION_PERSISTENCE_H
#define MQTT_SESSION_PERSISTENCE_H

#include "mcp_mqtt_client_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

// Session data structure
typedef struct {
    char* client_id;
    struct mqtt_subscription* subscriptions;
    struct mqtt_inflight_message* inflight_messages;
    uint32_t last_packet_id;
    uint64_t session_created_time;       /**< When session was created */
    uint64_t session_last_access_time;   /**< Last time session was accessed */
    uint32_t session_expiry_interval;    /**< Session expiry interval in seconds */
    uint16_t file_format_version;        /**< File format version for compatibility */
} mqtt_session_data_t;

// Initialize session persistence
int mqtt_session_persistence_init(const char* storage_path);

// Save session data
int mqtt_session_save(const char* client_id, const mqtt_session_data_t* session);

// Load session data
int mqtt_session_load(const char* client_id, mqtt_session_data_t* session);

// Delete session data
int mqtt_session_delete(const char* client_id);

// Check if session exists
bool mqtt_session_exists(const char* client_id);

// Check if session has expired
bool mqtt_session_is_expired(const char* client_id);

// Clean up expired sessions
int mqtt_session_cleanup_expired(void);

// Get session info without loading full data
int mqtt_session_get_info(const char* client_id, uint64_t* created_time,
                         uint64_t* last_access_time, uint32_t* expiry_interval);

// Update session access time
int mqtt_session_update_access_time(const char* client_id);

// Cleanup session persistence
void mqtt_session_persistence_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_SESSION_PERSISTENCE_H