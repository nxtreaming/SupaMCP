#include "mqtt_transport_internal.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_sys_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

// Simple random number generation for client ID and security tokens
static uint32_t mqtt_get_random(void) {
    static bool seeded = false;
    if (!seeded) {
#ifdef _WIN32
        srand((unsigned int)(GetTickCount() ^ GetCurrentProcessId()));
#else
        srand((unsigned int)(time(NULL) ^ getpid()));
#endif
        seeded = true;
    }
    return (uint32_t)rand();
}

// Generate secure random token for topic protection
static void mqtt_generate_security_token(char* token, size_t token_size) {
    const char* chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    size_t chars_len = strlen(chars);

    for (size_t i = 0; i < token_size - 1; i++) {
        token[i] = chars[mqtt_get_random() % chars_len];
    }
    token[token_size - 1] = '\0';
}

/**
 * @brief Validates MQTT configuration
 */
int mqtt_validate_config(const mcp_mqtt_config_t* config) {
    if (!config) {
        mcp_log_error("MQTT config is NULL");
        return -1;
    }
    
    if (!config->host || strlen(config->host) == 0) {
        mcp_log_error("MQTT host is required");
        return -1;
    }
    
    if (config->port == 0) {
        mcp_log_error("MQTT port must be specified");
        return -1;
    }
    
    if (config->qos < 0 || config->qos > 2) {
        mcp_log_error("MQTT QoS must be 0, 1, or 2");
        return -1;
    }
    
    if (config->will_qos < 0 || config->will_qos > 2) {
        mcp_log_error("MQTT Will QoS must be 0, 1, or 2");
        return -1;
    }
    
    if (config->keep_alive > 65535) {
        mcp_log_error("MQTT keep-alive must be <= 65535 seconds");
        return -1;
    }
    
    return 0;
}

/**
 * @brief Generates a unique client ID if not provided
 */
char* mqtt_generate_client_id(void) {
    char* client_id = malloc(32);
    if (!client_id) {
        mcp_log_error("Failed to allocate memory for client ID");
        return NULL;
    }
    
    // Generate client ID with timestamp and random component
    uint64_t timestamp = mcp_get_time_ms();
    uint32_t random = mqtt_get_random();
    
    snprintf(client_id, 32, "mcp_%llu_%u", 
             (unsigned long long)timestamp, random);
    
    return client_id;
}

/**
 * @brief Resolves topic templates with client ID
 */
int mqtt_resolve_topics(mcp_mqtt_transport_data_t* data, const char* client_id) {
    if (!data || !client_id) {
        return -1;
    }

    const char* prefix = data->config.topic_prefix ? data->config.topic_prefix : "mcp/";

    // Generate security token for topic protection
    char security_token[17]; // 16 chars + null terminator
    mqtt_generate_security_token(security_token, sizeof(security_token));

    // Create secure client identifier
    char secure_client_id[256];
    snprintf(secure_client_id, sizeof(secure_client_id), "%s_%s", client_id, security_token);

    // Resolve request topic with security token
    if (data->config.request_topic) {
        data->resolved_request_topic = mcp_strdup(data->config.request_topic);
    } else {
        size_t len = strlen(prefix) + strlen(secure_client_id) + 16;
        data->resolved_request_topic = malloc(len);
        if (data->resolved_request_topic) {
            snprintf(data->resolved_request_topic, len,
                    MCP_MQTT_DEFAULT_REQUEST_TOPIC_TEMPLATE, prefix, secure_client_id);
        }
    }

    // Resolve response topic with security token
    if (data->config.response_topic) {
        data->resolved_response_topic = mcp_strdup(data->config.response_topic);
    } else {
        size_t len = strlen(prefix) + strlen(secure_client_id) + 16;
        data->resolved_response_topic = malloc(len);
        if (data->resolved_response_topic) {
            snprintf(data->resolved_response_topic, len,
                    MCP_MQTT_DEFAULT_RESPONSE_TOPIC_TEMPLATE, prefix, secure_client_id);
        }
    }

    // Resolve notification topic with security token
    if (data->config.notification_topic) {
        data->resolved_notification_topic = mcp_strdup(data->config.notification_topic);
    } else {
        size_t len = strlen(prefix) + strlen(secure_client_id) + 16;
        data->resolved_notification_topic = malloc(len);
        if (data->resolved_notification_topic) {
            snprintf(data->resolved_notification_topic, len,
                    MCP_MQTT_DEFAULT_NOTIFICATION_TOPIC_TEMPLATE, prefix, secure_client_id);
        }
    }
    
    if (!data->resolved_request_topic || !data->resolved_response_topic || 
        !data->resolved_notification_topic) {
        mcp_log_error("Failed to resolve MQTT topics");
        return -1;
    }
    
    mcp_log_debug("MQTT topics resolved with security token - Request: %s, Response: %s, Notification: %s",
                 data->resolved_request_topic, data->resolved_response_topic,
                 data->resolved_notification_topic);
    
    return 0;
}

/**
 * @brief Converts MCP message to MQTT payload
 */
int mqtt_serialize_mcp_message(const void* mcp_data, size_t mcp_len, 
                              void** mqtt_payload, size_t* mqtt_len) {
    if (!mcp_data || mcp_len == 0 || !mqtt_payload || !mqtt_len) {
        return -1;
    }
    
    // For now, we'll use the MCP message as-is
    // In the future, we might add MQTT-specific framing or compression
    *mqtt_payload = malloc(mcp_len);
    if (!*mqtt_payload) {
        mcp_log_error("Failed to allocate MQTT payload");
        return -1;
    }
    
    memcpy(*mqtt_payload, mcp_data, mcp_len);
    *mqtt_len = mcp_len;
    
    return 0;
}

/**
 * @brief Converts MQTT payload to MCP message
 */
int mqtt_deserialize_mcp_message(const void* mqtt_payload, size_t mqtt_len,
                                void** mcp_data, size_t* mcp_len) {
    if (!mqtt_payload || mqtt_len == 0 || !mcp_data || !mcp_len) {
        return -1;
    }
    
    // For now, we'll use the MQTT payload as-is
    // In the future, we might add MQTT-specific deframing or decompression
    *mcp_data = malloc(mqtt_len);
    if (!*mcp_data) {
        mcp_log_error("Failed to allocate MCP data");
        return -1;
    }
    
    memcpy(*mcp_data, mqtt_payload, mqtt_len);
    *mcp_len = mqtt_len;
    
    return 0;
}

/**
 * @brief Enqueues a message for sending
 */
int mqtt_enqueue_message(mcp_mqtt_transport_data_t* data, const char* topic,
                        const void* payload, size_t payload_len, int qos, bool retain) {
    if (!data || !topic || !payload || payload_len == 0) {
        return -1;
    }
    
    // Check queue size limit
    mcp_mutex_lock(data->message_mutex);
    if (data->message_queue_size >= (int)data->max_queue_size) {
        mcp_mutex_unlock(data->message_mutex);
        mcp_log_warn("MQTT message queue is full, dropping message");
        return -1;
    }
    mcp_mutex_unlock(data->message_mutex);
    
    // Create queue entry
    mcp_mqtt_message_queue_entry_t* entry = malloc(sizeof(mcp_mqtt_message_queue_entry_t));
    if (!entry) {
        mcp_log_error("Failed to allocate message queue entry");
        return -1;
    }
    
    entry->topic = mcp_strdup(topic);
    entry->payload = malloc(payload_len);
    if (!entry->topic || !entry->payload) {
        free(entry->topic);
        free(entry->payload);
        free(entry);
        mcp_log_error("Failed to allocate message data");
        return -1;
    }
    
    memcpy(entry->payload, payload, payload_len);
    entry->payload_len = payload_len;
    entry->qos = qos;
    entry->retain = retain;
    entry->timestamp = mcp_get_time_ms();
    entry->retry_count = 0;
    entry->next = NULL;
    
    // Add to queue
    mcp_mutex_lock(data->message_mutex);

    if (data->message_queue_tail) {
        data->message_queue_tail->next = entry;
        data->message_queue_tail = entry;
    } else {
        data->message_queue_head = data->message_queue_tail = entry;
    }

    data->message_queue_size++;

    mcp_mutex_unlock(data->message_mutex);
    
    mcp_log_debug("Enqueued MQTT message to topic: %s, size: %zu", topic, payload_len);
    
    return 0;
}

/**
 * @brief Dequeues and processes messages
 */
int mqtt_process_message_queue(mcp_mqtt_transport_data_t* data) {
    if (!data) {
        return -1;
    }
    
    mcp_mqtt_message_queue_entry_t* entry = NULL;
    
    // Get next message from queue
    mcp_mutex_lock(data->message_mutex);

    if (data->message_queue_head) {
        entry = data->message_queue_head;
        data->message_queue_head = entry->next;
        if (!data->message_queue_head) {
            data->message_queue_tail = NULL;
        }
        data->message_queue_size--;
    }

    mcp_mutex_unlock(data->message_mutex);
    
    if (!entry) {
        return 0; // No messages to process
    }
    
    // Process the message
    int result = -1;
    if (data->wsi) {
        // Use libwebsockets MQTT API to send the message
        lws_mqtt_publish_param_t pub = {0};
        pub.topic = entry->topic;
        pub.topic_len = strlen(entry->topic);
        pub.payload = entry->payload;
        pub.payload_len = entry->payload_len;
        pub.qos = (lws_mqtt_qos_levels_t)entry->qos;
        pub.retain = entry->retain;

        mcp_log_debug("Publishing MQTT message to topic: %s, size: %zu", entry->topic, entry->payload_len);
        result = lws_mqtt_client_send_publish(data->wsi, &pub, entry->payload, entry->payload_len, 1);

        if (result < 0) {
            mcp_log_error("Failed to publish MQTT message: %d", result);
        }
    }
    
    // Update statistics
    if (result == 0) {
        mcp_mutex_lock(data->stats_mutex);
        data->messages_sent++;
        data->bytes_sent += entry->payload_len;
        mcp_mutex_unlock(data->stats_mutex);
    }
    
    // Clean up
    free(entry->topic);
    free(entry->payload);
    free(entry);
    
    return result;
}

/**
 * @brief Handles incoming MQTT messages
 */
int mqtt_handle_incoming_message(mcp_mqtt_transport_data_t* data, const char* topic,
                                const void* payload, size_t payload_len) {
    if (!data || !topic || !payload || payload_len == 0) {
        return -1;
    }
    
    mcp_log_debug("Received MQTT message on topic: %s, size: %zu", topic, payload_len);
    
    // Update statistics
    mcp_mutex_lock(data->stats_mutex);
    data->messages_received++;
    data->bytes_received += payload_len;
    mcp_mutex_unlock(data->stats_mutex);
    
    // Check if this is an MCP message on our topics
    bool is_mcp_message = false;
    if (data->resolved_request_topic && strcmp(topic, data->resolved_request_topic) == 0) {
        is_mcp_message = true;
    } else if (data->resolved_response_topic && strcmp(topic, data->resolved_response_topic) == 0) {
        is_mcp_message = true;
    } else if (data->resolved_notification_topic && strcmp(topic, data->resolved_notification_topic) == 0) {
        is_mcp_message = true;
    }
    
    if (is_mcp_message && data->message_callback) {
        // Convert MQTT payload to MCP message
        void* mcp_data = NULL;
        size_t mcp_len = 0;
        
        if (mqtt_deserialize_mcp_message(payload, payload_len, &mcp_data, &mcp_len) == 0) {
            // Call MCP message callback
            int error_code = 0;
            char* response = data->message_callback(data->callback_user_data, mcp_data, mcp_len, &error_code);
            if (response) {
                free(response); // Free any response since MQTT doesn't use it directly
            }
            free(mcp_data);
        } else {
            mcp_log_error("Failed to deserialize MCP message from MQTT payload");
        }
    } else if (data->custom_message_handler) {
        // Call custom message handler for non-MCP messages
        data->custom_message_handler(topic, payload, payload_len, data->custom_handler_user_data);
    }
    
    return 0;
}
