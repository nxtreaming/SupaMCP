#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#include "win_socket_compat.h"
#endif

#include "mcp_http_session_manager.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include "mcp_string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

// Maximum number of sessions
#define MAX_SESSIONS 10000

// Session structure
struct mcp_http_session {
    char session_id[MCP_SESSION_ID_MAX_LENGTH];
    mcp_session_state_t state;
    time_t created_time;
    time_t last_access_time;
    uint32_t timeout_seconds;
    void* user_data;
    bool in_use;
};

// Session manager structure
struct mcp_http_session_manager {
    mcp_http_session_t sessions[MAX_SESSIONS];
    mcp_mutex_t* mutex;
    uint32_t default_timeout_seconds;
    mcp_session_event_callback_t event_callback;
    void* event_callback_user_data;
    size_t active_count;
};

// Forward declarations
static bool generate_session_id(char* session_id_out);
static bool is_session_expired(const mcp_http_session_t* session);
static void notify_session_event(mcp_http_session_manager_t* manager, const char* session_id, mcp_session_state_t state);

/**
 * @brief Generate a cryptographically secure session ID
 */
static bool generate_session_id(char* session_id_out) {
    if (session_id_out == NULL) {
        return false;
    }

    // Generate 16 random bytes (128 bits)
    unsigned char random_bytes[16];
    
#ifdef _WIN32
    HCRYPTPROV hCryptProv;
    if (!CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        mcp_log_error("Failed to acquire crypto context");
        return false;
    }
    
    if (!CryptGenRandom(hCryptProv, sizeof(random_bytes), random_bytes)) {
        mcp_log_error("Failed to generate random bytes");
        CryptReleaseContext(hCryptProv, 0);
        return false;
    }
    
    CryptReleaseContext(hCryptProv, 0);
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        mcp_log_error("Failed to open /dev/urandom");
        return false;
    }
    
    ssize_t bytes_read = read(fd, random_bytes, sizeof(random_bytes));
    close(fd);
    
    if (bytes_read != sizeof(random_bytes)) {
        mcp_log_error("Failed to read random bytes");
        return false;
    }
#endif

    // Convert to hex string
    for (int i = 0; i < 16; i++) {
        sprintf(session_id_out + (i * 2), "%02x", random_bytes[i]);
    }
    session_id_out[32] = '\0';
    
    return true;
}

/**
 * @brief Check if a session is expired
 */
static bool is_session_expired(const mcp_http_session_t* session) {
    if (session == NULL || !session->in_use) {
        return true;
    }
    
    if (session->state != MCP_SESSION_STATE_ACTIVE) {
        return true;
    }
    
    if (session->timeout_seconds == 0) {
        return false; // No timeout
    }
    
    time_t current_time = time(NULL);
    return (current_time - session->last_access_time) > session->timeout_seconds;
}

/**
 * @brief Notify session event callback
 */
static void notify_session_event(mcp_http_session_manager_t* manager, const char* session_id, mcp_session_state_t state) {
    if (manager != NULL && manager->event_callback != NULL) {
        manager->event_callback(session_id, state, manager->event_callback_user_data);
    }
}

mcp_http_session_manager_t* mcp_session_manager_create(uint32_t default_timeout_seconds) {
    mcp_http_session_manager_t* manager = (mcp_http_session_manager_t*)calloc(1, sizeof(mcp_http_session_manager_t));
    if (manager == NULL) {
        mcp_log_error("Failed to allocate memory for session manager");
        return NULL;
    }
    
    manager->mutex = mcp_mutex_create();
    if (manager->mutex == NULL) {
        mcp_log_error("Failed to create session manager mutex");
        free(manager);
        return NULL;
    }
    
    manager->default_timeout_seconds = default_timeout_seconds;
    manager->active_count = 0;
    
    // Initialize all sessions as unused
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        manager->sessions[i].in_use = false;
        manager->sessions[i].state = MCP_SESSION_STATE_TERMINATED;
    }
    
    mcp_log_info("Session manager created with default timeout: %u seconds", default_timeout_seconds);
    return manager;
}

void mcp_session_manager_destroy(mcp_http_session_manager_t* manager) {
    if (manager == NULL) {
        return;
    }
    
    mcp_mutex_lock(manager->mutex);
    
    // Notify about all active sessions being terminated
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (manager->sessions[i].in_use && manager->sessions[i].state == MCP_SESSION_STATE_ACTIVE) {
            manager->sessions[i].state = MCP_SESSION_STATE_TERMINATED;
            notify_session_event(manager, manager->sessions[i].session_id, MCP_SESSION_STATE_TERMINATED);
        }
    }
    
    mcp_mutex_unlock(manager->mutex);
    mcp_mutex_destroy(manager->mutex);
    free(manager);
    
    mcp_log_info("Session manager destroyed");
}

mcp_http_session_t* mcp_session_manager_create_session(mcp_http_session_manager_t* manager, 
                                                      char* session_id_out, 
                                                      int32_t timeout_seconds) {
    if (manager == NULL || session_id_out == NULL) {
        return NULL;
    }
    
    mcp_mutex_lock(manager->mutex);
    
    // Find an unused session slot
    mcp_http_session_t* session = NULL;
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (!manager->sessions[i].in_use) {
            session = &manager->sessions[i];
            break;
        }
    }
    
    if (session == NULL) {
        mcp_mutex_unlock(manager->mutex);
        mcp_log_error("No available session slots (max: %d)", MAX_SESSIONS);
        return NULL;
    }
    
    // Generate session ID
    if (!generate_session_id(session->session_id)) {
        mcp_mutex_unlock(manager->mutex);
        mcp_log_error("Failed to generate session ID");
        return NULL;
    }
    
    // Initialize session
    session->state = MCP_SESSION_STATE_ACTIVE;
    session->created_time = time(NULL);
    session->last_access_time = session->created_time;
    session->user_data = NULL;
    session->in_use = true;
    
    // Set timeout
    if (timeout_seconds == 0) {
        session->timeout_seconds = manager->default_timeout_seconds;
    } else if (timeout_seconds < 0) {
        session->timeout_seconds = 0; // No timeout
    } else {
        session->timeout_seconds = (uint32_t)timeout_seconds;
    }
    
    manager->active_count++;
    
    // Copy session ID to output
    strncpy(session_id_out, session->session_id, MCP_SESSION_ID_MAX_LENGTH - 1);
    session_id_out[MCP_SESSION_ID_MAX_LENGTH - 1] = '\0';
    
    mcp_mutex_unlock(manager->mutex);
    
    mcp_log_info("Created session: %s (timeout: %u seconds)", session->session_id, session->timeout_seconds);
    return session;
}

mcp_http_session_t* mcp_session_manager_get_session(mcp_http_session_manager_t* manager, const char* session_id) {
    if (manager == NULL || session_id == NULL) {
        return NULL;
    }
    
    mcp_mutex_lock(manager->mutex);
    
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (manager->sessions[i].in_use && 
            strcmp(manager->sessions[i].session_id, session_id) == 0) {
            
            mcp_http_session_t* session = &manager->sessions[i];
            
            // Check if expired
            if (is_session_expired(session)) {
                session->state = MCP_SESSION_STATE_EXPIRED;
                notify_session_event(manager, session_id, MCP_SESSION_STATE_EXPIRED);
                mcp_mutex_unlock(manager->mutex);
                return NULL;
            }
            
            mcp_mutex_unlock(manager->mutex);
            return session;
        }
    }
    
    mcp_mutex_unlock(manager->mutex);
    return NULL;
}

bool mcp_session_manager_terminate_session(mcp_http_session_manager_t* manager, const char* session_id) {
    if (manager == NULL || session_id == NULL) {
        return false;
    }
    
    mcp_mutex_lock(manager->mutex);
    
    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (manager->sessions[i].in_use && 
            strcmp(manager->sessions[i].session_id, session_id) == 0) {
            
            mcp_http_session_t* session = &manager->sessions[i];
            
            if (session->state == MCP_SESSION_STATE_ACTIVE) {
                session->state = MCP_SESSION_STATE_TERMINATED;
                session->in_use = false;
                manager->active_count--;
                
                notify_session_event(manager, session_id, MCP_SESSION_STATE_TERMINATED);
                mcp_log_info("Terminated session: %s", session_id);
                
                mcp_mutex_unlock(manager->mutex);
                return true;
            }
        }
    }
    
    mcp_mutex_unlock(manager->mutex);
    return false;
}

void mcp_session_touch(mcp_http_session_t* session) {
    if (session != NULL && session->in_use) {
        session->last_access_time = time(NULL);
    }
}

const char* mcp_session_get_id(mcp_http_session_t* session) {
    if (session == NULL || !session->in_use) {
        return NULL;
    }
    return session->session_id;
}

mcp_session_state_t mcp_session_get_state(mcp_http_session_t* session) {
    if (session == NULL || !session->in_use) {
        return MCP_SESSION_STATE_TERMINATED;
    }

    // Check if expired
    if (is_session_expired(session)) {
        return MCP_SESSION_STATE_EXPIRED;
    }

    return session->state;
}

void mcp_session_set_user_data(mcp_http_session_t* session, void* user_data) {
    if (session != NULL && session->in_use) {
        session->user_data = user_data;
    }
}

void* mcp_session_get_user_data(mcp_http_session_t* session) {
    if (session == NULL || !session->in_use) {
        return NULL;
    }
    return session->user_data;
}

void mcp_session_manager_set_event_callback(mcp_http_session_manager_t* manager,
                                           mcp_session_event_callback_t callback,
                                           void* user_data) {
    if (manager != NULL) {
        mcp_mutex_lock(manager->mutex);
        manager->event_callback = callback;
        manager->event_callback_user_data = user_data;
        mcp_mutex_unlock(manager->mutex);
    }
}

size_t mcp_session_manager_cleanup_expired(mcp_http_session_manager_t* manager) {
    if (manager == NULL) {
        return 0;
    }

    size_t cleaned_count = 0;
    mcp_mutex_lock(manager->mutex);

    for (size_t i = 0; i < MAX_SESSIONS; i++) {
        if (manager->sessions[i].in_use) {
            mcp_http_session_t* session = &manager->sessions[i];

            if (is_session_expired(session)) {
                session->state = MCP_SESSION_STATE_EXPIRED;
                session->in_use = false;
                manager->active_count--;
                cleaned_count++;

                notify_session_event(manager, session->session_id, MCP_SESSION_STATE_EXPIRED);
                mcp_log_debug("Cleaned up expired session: %s", session->session_id);
            }
        }
    }

    mcp_mutex_unlock(manager->mutex);

    if (cleaned_count > 0) {
        mcp_log_info("Cleaned up %zu expired sessions", cleaned_count);
    }

    return cleaned_count;
}

size_t mcp_session_manager_get_active_count(mcp_http_session_manager_t* manager) {
    if (manager == NULL) {
        return 0;
    }

    mcp_mutex_lock(manager->mutex);
    size_t count = manager->active_count;
    mcp_mutex_unlock(manager->mutex);

    return count;
}

bool mcp_session_id_is_valid(const char* session_id) {
    if (session_id == NULL) {
        return false;
    }

    size_t len = strlen(session_id);
    if (len == 0 || len >= MCP_SESSION_ID_MAX_LENGTH) {
        return false;
    }

    // Check that all characters are visible ASCII (0x21 to 0x7E)
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)session_id[i];
        if (c < 0x21 || c > 0x7E) {
            return false;
        }
    }

    return true;
}
