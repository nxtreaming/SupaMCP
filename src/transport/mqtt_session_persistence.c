#include "internal/mqtt_session_persistence.h"
#include "internal/mqtt_client_internal.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_sys_utils.h"
#include "mcp_string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define F_OK 0
#define access _access
#else
#include <unistd.h>
#include <dirent.h>
#endif

// File format version for compatibility
#define MQTT_SESSION_FILE_VERSION 1

// Magic number for session files
#define MQTT_SESSION_MAGIC 0x4D435053  // "MCPS"

// Internal helper functions that assume lock is already held
static bool mqtt_session_is_expired_internal(const char* client_id);
static int mqtt_session_delete_internal(const char* client_id);

static char* g_storage_path = NULL;
static mcp_mutex_t* g_session_mutex = NULL;
static volatile bool g_persistence_initialized = false;
static volatile bool g_persistence_shutting_down = false;

/**
 * @brief Safe file write function with error checking
 * @param ptr Pointer to data to write
 * @param size Size of each element
 * @param count Number of elements to write
 * @param stream File stream to write to
 * @return 0 on success, -1 on failure
 */
static int safe_fwrite(const void* ptr, size_t size, size_t count, FILE* stream) {
    if (!ptr || !stream || size == 0 || count == 0) {
        mcp_log_error("Invalid parameters for file write operation");
        return -1;
    }

    size_t written = fwrite(ptr, size, count, stream);
    if (written != count) {
        size_t expected_bytes = size * count;
        size_t actual_bytes = written * size;
        mcp_log_error("Failed to write %zu bytes to file (wrote %zu bytes, errno: %d)",
                     expected_bytes, actual_bytes, errno);
        return -1;
    }
    return 0;
}

// Helper function to check if persistence system is ready and safe to use
static bool is_persistence_ready(void) {
    return g_persistence_initialized && !g_persistence_shutting_down && g_session_mutex != NULL;
}

int mqtt_session_persistence_init(const char* storage_path) {
    if (!storage_path) {
        return -1;
    }

    // Check if shutting down
    if (g_persistence_shutting_down) {
        return -1;
    }

    // Initialize mutex if not already done
    if (!g_session_mutex) {
        g_session_mutex = mcp_mutex_create();
        if (!g_session_mutex) {
            return -1;
        }
    }

    mcp_mutex_lock(g_session_mutex);

    // Double-check shutdown flag after acquiring lock
    if (g_persistence_shutting_down) {
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }
    
    // Free existing path if any
    if (g_storage_path) {
        free(g_storage_path);
        g_storage_path = NULL;
    }

    // Create storage directory if it doesn't exist
#ifdef _WIN32
    _mkdir(storage_path);
#else
    mkdir(storage_path, 0755);
#endif

    // Store the path
    g_storage_path = mcp_strdup(storage_path);
    if (!g_storage_path) {
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    g_persistence_initialized = true;
    mcp_mutex_unlock(g_session_mutex);
    return 0;
}

static char* get_session_path(const char* client_id) {
    if (!g_storage_path || !client_id) {
        return NULL;
    }

    // Create a safe filename from client_id
    char* safe_id = mcp_strdup(client_id);
    if (!safe_id) {
        return NULL;
    }

    // Replace invalid filename characters
    for (char* p = safe_id; *p; ++p) {
        if (*p == '\\' || *p == '/' || *p == ':' || *p == '*' || 
            *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|') {
            *p = '_';
        }
    }

    // Allocate path buffer
    size_t path_len = strlen(g_storage_path) + strlen(safe_id) + 6; // +6 for "/.mcp_"
    char* path = (char*)malloc(path_len);
    if (!path) {
        free(safe_id);
        return NULL;
    }

    snprintf(path, path_len, "%s/.mcp_%s", g_storage_path, safe_id);
    free(safe_id);
    return path;
}

int mqtt_session_save(const char* client_id, const mqtt_session_data_t* session) {
    if (!client_id || !session) {
        return -1;
    }

    // Check if persistence system is ready
    if (!is_persistence_ready()) {
        return -1;
    }

    // Acquire lock for thread-safe file operations
    mcp_mutex_lock(g_session_mutex);

    // Double-check after acquiring lock
    if (!is_persistence_ready()) {
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    char* path = get_session_path(client_id);
    if (!path) {
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    FILE* fp = fopen(path, "wb");
    if (!fp) {
        mcp_log_error("Failed to open session file for writing: %s (errno: %d)", path, errno);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Write session data
    uint32_t magic = MQTT_SESSION_MAGIC;
    if (safe_fwrite(&magic, sizeof(magic), 1, fp) != 0) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Write file format version
    uint16_t version = MQTT_SESSION_FILE_VERSION;
    if (safe_fwrite(&version, sizeof(version), 1, fp) != 0) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Write session metadata
    uint64_t current_time = mcp_get_time_ms();
    if (safe_fwrite(&session->session_created_time, sizeof(session->session_created_time), 1, fp) != 0 ||
        safe_fwrite(&current_time, sizeof(current_time), 1, fp) != 0 ||
        safe_fwrite(&session->session_expiry_interval, sizeof(session->session_expiry_interval), 1, fp) != 0) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Write client ID length and data
    uint16_t client_id_len = (uint16_t)strlen(client_id);
    if (safe_fwrite(&client_id_len, sizeof(client_id_len), 1, fp) != 0 ||
        safe_fwrite(client_id, 1, client_id_len, fp) != 0) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Write subscription count
    uint16_t sub_count = 0;
    struct mqtt_subscription* sub = session->subscriptions;
    while (sub) {
        sub_count++;
        sub = sub->next;
    }
    if (safe_fwrite(&sub_count, sizeof(sub_count), 1, fp) != 0) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Write subscriptions
    sub = session->subscriptions;
    while (sub) {
        uint16_t topic_len = (uint16_t)strlen(sub->topic);
        if (safe_fwrite(&topic_len, sizeof(topic_len), 1, fp) != 0 ||
            safe_fwrite(sub->topic, 1, topic_len, fp) != 0 ||
            safe_fwrite(&sub->qos, sizeof(sub->qos), 1, fp) != 0) {
            fclose(fp);
            free(path);
            mcp_mutex_unlock(g_session_mutex);
            return -1;
        }
        sub = sub->next;
    }

    // Write last packet ID
    if (safe_fwrite(&session->last_packet_id, sizeof(session->last_packet_id), 1, fp) != 0) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Write in-flight message count
    uint16_t inflight_count = 0;
    struct mqtt_inflight_message* inflight = session->inflight_messages;
    while (inflight) {
        inflight_count++;
        inflight = inflight->next;
    }
    if (safe_fwrite(&inflight_count, sizeof(inflight_count), 1, fp) != 0) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Write in-flight messages
    inflight = session->inflight_messages;
    while (inflight) {
        if (safe_fwrite(&inflight->packet_id, sizeof(inflight->packet_id), 1, fp) != 0) {
            fclose(fp);
            free(path);
            mcp_mutex_unlock(g_session_mutex);
            return -1;
        }

        uint16_t topic_len = (uint16_t)strlen(inflight->topic);
        if (safe_fwrite(&topic_len, sizeof(topic_len), 1, fp) != 0 ||
            safe_fwrite(inflight->topic, 1, topic_len, fp) != 0) {
            fclose(fp);
            free(path);
            mcp_mutex_unlock(g_session_mutex);
            return -1;
        }

        uint32_t payload_len = (uint32_t)inflight->payload_len;
        if (safe_fwrite(&payload_len, sizeof(payload_len), 1, fp) != 0 ||
            safe_fwrite(inflight->payload, 1, inflight->payload_len, fp) != 0) {
            fclose(fp);
            free(path);
            mcp_mutex_unlock(g_session_mutex);
            return -1;
        }

        if (safe_fwrite(&inflight->qos, sizeof(inflight->qos), 1, fp) != 0 ||
            safe_fwrite(&inflight->retain, sizeof(inflight->retain), 1, fp) != 0 ||
            safe_fwrite(&inflight->send_time, sizeof(inflight->send_time), 1, fp) != 0 ||
            safe_fwrite(&inflight->retry_count, sizeof(inflight->retry_count), 1, fp) != 0) {
            fclose(fp);
            free(path);
            mcp_mutex_unlock(g_session_mutex);
            return -1;
        }

        inflight = inflight->next;
    }

    fclose(fp);
    free(path);
    mcp_mutex_unlock(g_session_mutex);
    return 0;
}

int mqtt_session_load(const char* client_id, mqtt_session_data_t* session) {
    if (!client_id || !session) {
        return -1;
    }

    // Check if persistence system is ready
    if (!is_persistence_ready()) {
        return -1;
    }

    // Acquire lock for thread-safe file operations
    mcp_mutex_lock(g_session_mutex);

    // Double-check after acquiring lock
    if (!is_persistence_ready()) {
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    char* path = get_session_path(client_id);
    if (!path) {
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        mcp_log_warn("Failed to open session file for reading: %s (errno: %d)", path, errno);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Read and verify magic number
    uint32_t magic;
    if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != MQTT_SESSION_MAGIC) {
        mcp_log_warn("Invalid session file magic number for client: %s", client_id);
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Read and verify file format version
    uint16_t version;
    if (fread(&version, sizeof(version), 1, fp) != 1) {
        mcp_log_warn("Failed to read session file version for client: %s", client_id);
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    if (version > MQTT_SESSION_FILE_VERSION) {
        mcp_log_warn("Unsupported session file version %u for client: %s", version, client_id);
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Read session metadata
    if (fread(&session->session_created_time, sizeof(session->session_created_time), 1, fp) != 1 ||
        fread(&session->session_last_access_time, sizeof(session->session_last_access_time), 1, fp) != 1 ||
        fread(&session->session_expiry_interval, sizeof(session->session_expiry_interval), 1, fp) != 1) {
        mcp_log_warn("Failed to read session metadata for client: %s", client_id);
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Check if session has expired
    if (session->session_expiry_interval > 0) {
        uint64_t current_time = mcp_get_time_ms();
        uint64_t elapsed_seconds = (current_time - session->session_last_access_time) / 1000;
        if (elapsed_seconds > session->session_expiry_interval) {
            mcp_log_info("Session expired for client: %s (elapsed: %llu, expiry: %u)",
                        client_id, (unsigned long long)elapsed_seconds, session->session_expiry_interval);
            fclose(fp);
            free(path);
            mcp_mutex_unlock(g_session_mutex);
            // Delete expired session file
            mqtt_session_delete(client_id);
            return -1;
        }
    }

    // Read client ID
    uint16_t client_id_len;
    if (fread(&client_id_len, sizeof(client_id_len), 1, fp) != 1) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    char* loaded_client_id = (char*)malloc(client_id_len + 1);
    if (!loaded_client_id) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    if (fread(loaded_client_id, 1, client_id_len, fp) != client_id_len) {
        free(loaded_client_id);
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }
    loaded_client_id[client_id_len] = '\0';

    // Verify client ID matches
    if (strcmp(loaded_client_id, client_id) != 0) {
        free(loaded_client_id);
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }
    free(loaded_client_id);

    // Read subscription count
    uint16_t sub_count;
    if (fread(&sub_count, sizeof(sub_count), 1, fp) != 1) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Read subscriptions
    for (uint16_t i = 0; i < sub_count; i++) {
        uint16_t topic_len;
        if (fread(&topic_len, sizeof(topic_len), 1, fp) != 1) {
            mcp_log_warn("Failed to read subscription topic length for client: %s", client_id);
            goto cleanup_and_exit;
        }

        char* topic = (char*)malloc(topic_len + 1);
        if (!topic) {
            mcp_log_error("Failed to allocate subscription topic for client: %s", client_id);
            goto cleanup_and_exit;
        }

        if (fread(topic, 1, topic_len, fp) != topic_len) {
            free(topic);
            mcp_log_warn("Failed to read subscription topic data for client: %s", client_id);
            goto cleanup_and_exit;
        }
        topic[topic_len] = '\0';

        int qos;
        if (fread(&qos, sizeof(qos), 1, fp) != 1) {
            free(topic);
            mcp_log_warn("Failed to read subscription QoS for client: %s", client_id);
            goto cleanup_and_exit;
        }

        // Add to session
        struct mqtt_subscription* sub = (struct mqtt_subscription*)malloc(sizeof(struct mqtt_subscription));
        if (!sub) {
            free(topic);
            mcp_log_error("Failed to allocate subscription for client: %s", client_id);
            goto cleanup_and_exit;
        }

        sub->topic = topic;
        sub->qos = qos;
        sub->next = session->subscriptions;
        session->subscriptions = sub;
    }

    // Read last packet ID
    if (fread(&session->last_packet_id, sizeof(session->last_packet_id), 1, fp) != 1) {
        mcp_log_warn("Failed to read last packet ID for client: %s", client_id);
        // Continue anyway, this is not critical
        session->last_packet_id = 1;
    }

    // Read in-flight message count
    uint16_t inflight_count = 0;
    if (fread(&inflight_count, sizeof(inflight_count), 1, fp) == 1) {
        // Read in-flight messages
        for (uint16_t i = 0; i < inflight_count; i++) {
            struct mqtt_inflight_message* inflight = malloc(sizeof(struct mqtt_inflight_message));
            if (!inflight) {
                mcp_log_error("Failed to allocate in-flight message for client: %s", client_id);
                goto cleanup_and_exit;
            }

            // Read packet ID
            if (fread(&inflight->packet_id, sizeof(inflight->packet_id), 1, fp) != 1) {
                free(inflight);
                mcp_log_warn("Failed to read in-flight packet ID for client: %s", client_id);
                goto cleanup_and_exit;
            }

            // Read topic
            uint16_t topic_len;
            if (fread(&topic_len, sizeof(topic_len), 1, fp) != 1) {
                free(inflight);
                mcp_log_warn("Failed to read in-flight topic length for client: %s", client_id);
                goto cleanup_and_exit;
            }

            inflight->topic = malloc(topic_len + 1);
            if (!inflight->topic) {
                free(inflight);
                mcp_log_error("Failed to allocate in-flight topic for client: %s", client_id);
                goto cleanup_and_exit;
            }

            if (fread(inflight->topic, 1, topic_len, fp) != topic_len) {
                free(inflight->topic);
                free(inflight);
                mcp_log_warn("Failed to read in-flight topic data for client: %s", client_id);
                goto cleanup_and_exit;
            }
            inflight->topic[topic_len] = '\0';

            // Read payload
            uint32_t payload_len;
            if (fread(&payload_len, sizeof(payload_len), 1, fp) != 1) {
                free(inflight->topic);
                free(inflight);
                mcp_log_warn("Failed to read in-flight payload length for client: %s", client_id);
                goto cleanup_and_exit;
            }

            inflight->payload = malloc(payload_len);
            if (!inflight->payload) {
                free(inflight->topic);
                free(inflight);
                mcp_log_error("Failed to allocate in-flight payload for client: %s", client_id);
                goto cleanup_and_exit;
            }

            if (fread(inflight->payload, 1, payload_len, fp) != payload_len) {
                free(inflight->topic);
                free(inflight->payload);
                free(inflight);
                mcp_log_warn("Failed to read in-flight payload data for client: %s", client_id);
                goto cleanup_and_exit;
            }
            inflight->payload_len = payload_len;

            // Read other fields
            if (fread(&inflight->qos, sizeof(inflight->qos), 1, fp) != 1 ||
                fread(&inflight->retain, sizeof(inflight->retain), 1, fp) != 1 ||
                fread(&inflight->send_time, sizeof(inflight->send_time), 1, fp) != 1 ||
                fread(&inflight->retry_count, sizeof(inflight->retry_count), 1, fp) != 1) {
                free(inflight->topic);
                free(inflight->payload);
                free(inflight);
                mcp_log_warn("Failed to read in-flight message fields for client: %s", client_id);
                goto cleanup_and_exit;
            }

            // Add to list
            inflight->next = session->inflight_messages;
            session->inflight_messages = inflight;
        }
    }

    // Update access time
    session->session_last_access_time = mcp_get_time_ms();

    fclose(fp);
    free(path);
    mcp_mutex_unlock(g_session_mutex);
    return 0;

cleanup_and_exit:
    // Clean up any partially loaded data
    struct mqtt_subscription* sub = session->subscriptions;
    while (sub) {
        struct mqtt_subscription* next = sub->next;
        free(sub->topic);
        free(sub);
        sub = next;
    }
    session->subscriptions = NULL;

    struct mqtt_inflight_message* inflight = session->inflight_messages;
    while (inflight) {
        struct mqtt_inflight_message* next = inflight->next;
        free(inflight->topic);
        free(inflight->payload);
        free(inflight);
        inflight = next;
    }
    session->inflight_messages = NULL;

    fclose(fp);
    free(path);
    mcp_mutex_unlock(g_session_mutex);
    return -1;
}

int mqtt_session_delete(const char* client_id) {
    if (!client_id) {
        return -1;
    }

    // Check if persistence system is ready
    if (!is_persistence_ready()) {
        return -1;
    }

    // Acquire lock for thread-safe file operations
    mcp_mutex_lock(g_session_mutex);

    // Double-check after acquiring lock
    if (!is_persistence_ready()) {
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    char* path = get_session_path(client_id);
    if (!path) {
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    int result = 0;
#ifdef _WIN32
    if (_unlink(path) != 0) {
        mcp_log_warn("Failed to delete session file: %s (errno: %d)", path, errno);
        result = -1;
    }
#else
    if (unlink(path) != 0) {
        mcp_log_warn("Failed to delete session file: %s (errno: %d)", path, errno);
        result = -1;
    }
#endif

    free(path);
    mcp_mutex_unlock(g_session_mutex);
    return result;
}

bool mqtt_session_exists(const char* client_id) {
    if (!client_id) {
        return false;
    }

    // Check if persistence system is ready
    if (!is_persistence_ready()) {
        return false;
    }

    // Acquire lock for thread-safe file operations
    mcp_mutex_lock(g_session_mutex);

    // Double-check after acquiring lock
    if (!is_persistence_ready()) {
        mcp_mutex_unlock(g_session_mutex);
        return false;
    }

    char* path = get_session_path(client_id);
    if (!path) {
        mcp_mutex_unlock(g_session_mutex);
        return false;
    }

    bool exists = (access(path, F_OK) == 0);
    free(path);
    mcp_mutex_unlock(g_session_mutex);
    return exists;
}

bool mqtt_session_is_expired(const char* client_id) {
    if (!client_id) {
        return true;
    }

    uint64_t created_time, last_access_time;
    uint32_t expiry_interval;

    if (mqtt_session_get_info(client_id, &created_time, &last_access_time, &expiry_interval) != 0) {
        return true; // If we can't read info, consider it expired
    }

    if (expiry_interval == 0) {
        return false; // No expiry
    }

    uint64_t current_time = mcp_get_time_ms();
    uint64_t elapsed_seconds = (current_time - last_access_time) / 1000;
    return elapsed_seconds > expiry_interval;
}

int mqtt_session_get_info(const char* client_id, uint64_t* created_time,
                         uint64_t* last_access_time, uint32_t* expiry_interval) {
    if (!client_id || !created_time || !last_access_time || !expiry_interval) {
        return -1;
    }

    // Check if persistence system is ready
    if (!is_persistence_ready()) {
        return -1;
    }

    // Acquire lock for thread-safe file operations
    mcp_mutex_lock(g_session_mutex);

    // Double-check after acquiring lock
    if (!is_persistence_ready()) {
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    char* path = get_session_path(client_id);
    if (!path) {
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        mcp_log_debug("Failed to open session file for info reading: %s (errno: %d)", path, errno);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Read magic number
    uint32_t magic;
    if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != MQTT_SESSION_MAGIC) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Read version
    uint16_t version;
    if (fread(&version, sizeof(version), 1, fp) != 1) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    // Read session metadata
    if (fread(created_time, sizeof(*created_time), 1, fp) != 1 ||
        fread(last_access_time, sizeof(*last_access_time), 1, fp) != 1 ||
        fread(expiry_interval, sizeof(*expiry_interval), 1, fp) != 1) {
        fclose(fp);
        free(path);
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    fclose(fp);
    free(path);
    mcp_mutex_unlock(g_session_mutex);
    return 0;
}

int mqtt_session_update_access_time(const char* client_id) {
    if (!client_id) {
        return -1;
    }

    // Load session data
    mqtt_session_data_t session_data = {0};
    if (mqtt_session_load(client_id, &session_data) != 0) {
        return -1;
    }

    // Update access time
    session_data.session_last_access_time = mcp_get_time_ms();

    // Save session data
    int result = mqtt_session_save(client_id, &session_data);

    // Cleanup loaded data
    struct mqtt_subscription* sub = session_data.subscriptions;
    while (sub) {
        struct mqtt_subscription* next = sub->next;
        free(sub->topic);
        free(sub);
        sub = next;
    }

    struct mqtt_inflight_message* inflight = session_data.inflight_messages;
    while (inflight) {
        struct mqtt_inflight_message* next = inflight->next;
        free(inflight->topic);
        free(inflight->payload);
        free(inflight);
        inflight = next;
    }

    return result;
}

int mqtt_session_cleanup_expired(void) {
    // Check if persistence system is ready
    if (!is_persistence_ready()) {
        return -1;
    }

    // Acquire lock for thread-safe directory operations
    mcp_mutex_lock(g_session_mutex);

    // Double-check after acquiring lock
    if (!is_persistence_ready()) {
        mcp_mutex_unlock(g_session_mutex);
        return -1;
    }

    int cleaned_count = 0;

#ifdef _WIN32
    // Windows implementation
    char search_pattern[512];
    snprintf(search_pattern, sizeof(search_pattern), "%s\\.mcp_*", g_storage_path);

    WIN32_FIND_DATA find_data;
    HANDLE find_handle = FindFirstFile(search_pattern, &find_data);

    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                // Extract client ID from filename
                const char* filename = find_data.cFileName;
                if (strncmp(filename, ".mcp_", 5) == 0) {
                    const char* client_id = filename + 5;

                    if (mqtt_session_is_expired_internal(client_id)) {
                        if (mqtt_session_delete_internal(client_id) == 0) {
                            cleaned_count++;
                            mcp_log_info("Cleaned expired session for client: %s", client_id);
                        }
                    }
                }
            }
        } while (FindNextFile(find_handle, &find_data));

        FindClose(find_handle);
    }
#else
    // Unix implementation
    DIR* dir = opendir(g_storage_path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strncmp(entry->d_name, ".mcp_", 5) == 0) {
                const char* client_id = entry->d_name + 5;

                if (mqtt_session_is_expired_internal(client_id)) {
                    if (mqtt_session_delete_internal(client_id) == 0) {
                        cleaned_count++;
                        mcp_log_info("Cleaned expired session for client: %s", client_id);
                    }
                }
            }
        }
        closedir(dir);
    }
#endif

    mcp_mutex_unlock(g_session_mutex);

    if (cleaned_count > 0) {
        mcp_log_info("Cleaned %d expired MQTT sessions", cleaned_count);
    }

    return cleaned_count;
}

void mqtt_session_persistence_cleanup(void) {
    // Set shutdown flag first to prevent new operations
    g_persistence_shutting_down = true;

    if (g_session_mutex) {
        mcp_mutex_lock(g_session_mutex);

        // Clean up storage path
        if (g_storage_path) {
            free(g_storage_path);
            g_storage_path = NULL;
        }

        g_persistence_initialized = false;

        // Unlock before destroying the mutex
        mcp_mutex_unlock(g_session_mutex);

        // Small delay to allow any pending operations to complete
        mcp_sleep_ms(10);

        // Destroy the mutex
        mcp_mutex_destroy(g_session_mutex);
        g_session_mutex = NULL;
    }

    // Reset shutdown flag after cleanup is complete
    g_persistence_shutting_down = false;
}

// Internal helper function - assumes lock is already held
static bool mqtt_session_is_expired_internal(const char* client_id) {
    if (!client_id) {
        return true;
    }

    uint64_t created_time, last_access_time;
    uint32_t expiry_interval;

    char* path = get_session_path(client_id);
    if (!path) {
        return true;
    }

    FILE* fp = fopen(path, "rb");
    if (!fp) {
        free(path);
        return true;
    }

    // Read magic number
    uint32_t magic;
    if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != MQTT_SESSION_MAGIC) {
        fclose(fp);
        free(path);
        return true;
    }

    // Read version
    uint16_t version;
    if (fread(&version, sizeof(version), 1, fp) != 1) {
        fclose(fp);
        free(path);
        return true;
    }

    // Read session metadata
    if (fread(&created_time, sizeof(created_time), 1, fp) != 1 ||
        fread(&last_access_time, sizeof(last_access_time), 1, fp) != 1 ||
        fread(&expiry_interval, sizeof(expiry_interval), 1, fp) != 1) {
        fclose(fp);
        free(path);
        return true;
    }

    fclose(fp);
    free(path);

    if (expiry_interval == 0) {
        return false; // No expiry
    }

    uint64_t current_time = mcp_get_time_ms();
    uint64_t elapsed_seconds = (current_time - last_access_time) / 1000;
    return elapsed_seconds > expiry_interval;
}

// Internal helper function - assumes lock is already held
static int mqtt_session_delete_internal(const char* client_id) {
    if (!client_id) {
        return -1;
    }

    char* path = get_session_path(client_id);
    if (!path) {
        return -1;
    }

    int result = 0;
#ifdef _WIN32
    if (_unlink(path) != 0) {
        result = -1;
    }
#else
    if (unlink(path) != 0) {
        result = -1;
    }
#endif

    free(path);
    return result;
}
