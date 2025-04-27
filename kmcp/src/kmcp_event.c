#include "kmcp_event.h"
#include "kmcp_common.h"
#include "mcp_log.h"
#include "mcp_sync.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

/**
 * @brief Event listener structure
 */
typedef struct {
    kmcp_event_listener_fn listener;  /**< Listener callback function */
    void* user_data;                  /**< User data to pass to the listener */
    bool active;                      /**< Whether the listener is active */
} kmcp_event_listener_t;

/**
 * @brief Event system structure
 */
typedef struct {
    kmcp_event_listener_t listeners[KMCP_EVENT_MAX_LISTENERS][KMCP_EVENT_CUSTOM + 100];  /**< Array of listeners */
    mcp_mutex_t* mutex;                                                                 /**< Mutex for thread safety */
    bool initialized;                                                                   /**< Whether the event system is initialized */
} kmcp_event_system_t;

/**
 * @brief Global event system instance
 */
static kmcp_event_system_t g_event_system = {0};

/**
 * @brief Get current timestamp in milliseconds
 *
 * @return unsigned long long Current timestamp in milliseconds
 */
static unsigned long long get_timestamp_ms(void) {
#ifdef _WIN32
    FILETIME ft;
    ULARGE_INTEGER li;
    GetSystemTimeAsFileTime(&ft);
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    // Convert from 100-nanosecond intervals to milliseconds
    return li.QuadPart / 10000;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)(tv.tv_sec) * 1000 + (unsigned long long)(tv.tv_usec) / 1000;
#endif
}

/**
 * @brief Initialize the event system
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_event_init(void) {
    // Check if already initialized
    if (g_event_system.initialized) {
        return KMCP_SUCCESS;
    }

    // Initialize mutex
    g_event_system.mutex = mcp_mutex_create();
    if (!g_event_system.mutex) {
        return KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION, "Failed to create event system mutex");
    }

    // Initialize listener array
    memset(g_event_system.listeners, 0, sizeof(g_event_system.listeners));
    g_event_system.initialized = true;

    mcp_log_info("KMCP event system initialized");
    return KMCP_SUCCESS;
}

/**
 * @brief Shut down the event system
 */
void kmcp_event_shutdown(void) {
    if (!g_event_system.initialized) {
        return;
    }

    // Acquire mutex
    mcp_mutex_lock(g_event_system.mutex);

    // Clear all listeners
    memset(g_event_system.listeners, 0, sizeof(g_event_system.listeners));

    // Release mutex and destroy it
    mcp_mutex_unlock(g_event_system.mutex);
    mcp_mutex_destroy(g_event_system.mutex);
    g_event_system.mutex = NULL;
    g_event_system.initialized = false;

    mcp_log_info("KMCP event system shut down");
}

/**
 * @brief Register an event listener
 *
 * @param event_type Event type to listen for
 * @param listener Listener callback function
 * @param user_data User data to pass to the listener
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_event_register_listener(kmcp_event_type_t event_type,
                                         kmcp_event_listener_fn listener,
                                         void* user_data) {
    // Check parameters
    if (!listener) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER, "Listener function cannot be NULL");
    }

    // Check if event system is initialized
    if (!g_event_system.initialized) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_OPERATION, "Event system not initialized");
    }

    // Acquire mutex
    mcp_mutex_lock(g_event_system.mutex);

    // Find an empty slot or check if listener is already registered
    int empty_slot = -1;
    for (int i = 0; i < KMCP_EVENT_MAX_LISTENERS; i++) {
        if (g_event_system.listeners[i][event_type].active) {
            if (g_event_system.listeners[i][event_type].listener == listener &&
                g_event_system.listeners[i][event_type].user_data == user_data) {
                // Listener already registered
                mcp_mutex_unlock(g_event_system.mutex);
                return KMCP_SUCCESS;
            }
        } else if (empty_slot == -1) {
            empty_slot = i;
        }
    }

    // Check if we found an empty slot
    if (empty_slot == -1) {
        mcp_mutex_unlock(g_event_system.mutex);
        return KMCP_ERROR_LOG(KMCP_ERROR_RESOURCE_BUSY,
                             "Maximum number of listeners reached for event type %d",
                             event_type);
    }

    // Register the listener
    g_event_system.listeners[empty_slot][event_type].listener = listener;
    g_event_system.listeners[empty_slot][event_type].user_data = user_data;
    g_event_system.listeners[empty_slot][event_type].active = true;

    // Release mutex
    mcp_mutex_unlock(g_event_system.mutex);

    mcp_log_debug("Registered listener for event type %d", event_type);
    return KMCP_SUCCESS;
}

/**
 * @brief Unregister an event listener
 *
 * @param event_type Event type to stop listening for
 * @param listener Listener callback function to unregister
 * @param user_data User data that was passed to the listener registration function
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_event_unregister_listener(kmcp_event_type_t event_type,
                                           kmcp_event_listener_fn listener,
                                           void* user_data) {
    // Check parameters
    if (!listener) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER, "Listener function cannot be NULL");
    }

    // Check if event system is initialized
    if (!g_event_system.initialized) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_OPERATION, "Event system not initialized");
    }

    // Acquire mutex
    mcp_mutex_lock(g_event_system.mutex);

    // Find the listener
    bool found = false;
    for (int i = 0; i < KMCP_EVENT_MAX_LISTENERS; i++) {
        if (g_event_system.listeners[i][event_type].active &&
            g_event_system.listeners[i][event_type].listener == listener &&
            g_event_system.listeners[i][event_type].user_data == user_data) {
            // Found the listener, deactivate it
            g_event_system.listeners[i][event_type].active = false;
            found = true;
            break;
        }
    }

    // Release mutex
    mcp_mutex_unlock(g_event_system.mutex);

    if (!found) {
        return KMCP_ERROR_LOG(KMCP_ERROR_NOT_FOUND,
                             "Listener not found for event type %d",
                             event_type);
    }

    mcp_log_debug("Unregistered listener for event type %d", event_type);
    return KMCP_SUCCESS;
}

/**
 * @brief Trigger an event
 *
 * @param event Event to trigger
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_event_trigger(const kmcp_event_t* event) {
    // Check parameters
    if (!event) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_PARAMETER, "Event cannot be NULL");
    }

    // Check if event system is initialized
    if (!g_event_system.initialized) {
        return KMCP_ERROR_LOG(KMCP_ERROR_INVALID_OPERATION, "Event system not initialized");
    }

    // Acquire mutex
    mcp_mutex_lock(g_event_system.mutex);

    // Call all registered listeners
    for (int i = 0; i < KMCP_EVENT_MAX_LISTENERS; i++) {
        if (g_event_system.listeners[i][event->type].active) {
            kmcp_event_listener_fn listener = g_event_system.listeners[i][event->type].listener;
            void* user_data = g_event_system.listeners[i][event->type].user_data;

            // Release mutex while calling the listener
            mcp_mutex_unlock(g_event_system.mutex);

            // Call the listener
            bool continue_processing = listener(event, user_data);

            // Reacquire mutex
            mcp_mutex_lock(g_event_system.mutex);

            // Stop processing if the listener returned false
            if (!continue_processing) {
                break;
            }
        }
    }

    // Release mutex
    mcp_mutex_unlock(g_event_system.mutex);

    mcp_log_debug("Triggered event type %d", event->type);
    return KMCP_SUCCESS;
}

/**
 * @brief Create an event
 *
 * @param type Event type
 * @param data Event data
 * @param data_size Size of event data
 * @param source Event source
 * @param source_name Name of event source
 * @return kmcp_event_t* Returns a pointer to the created event, or NULL on failure
 */
kmcp_event_t* kmcp_event_create(kmcp_event_type_t type,
                               const void* data,
                               size_t data_size,
                               void* source,
                               const char* source_name) {
    // Allocate memory for the event
    kmcp_event_t* event = (kmcp_event_t*)malloc(sizeof(kmcp_event_t));
    if (!event) {
        KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION, "Failed to allocate memory for event");
        return NULL;
    }

    // Initialize the event
    event->type = type;
    event->source = source;
    event->source_name = source_name ? source_name : "unknown";
    event->timestamp = get_timestamp_ms();

    // Copy the data if provided
    if (data && data_size > 0) {
        event->data = malloc(data_size);
        if (!event->data) {
            KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION, "Failed to allocate memory for event data");
            free(event);
            return NULL;
        }
        memcpy(event->data, data, data_size);
        event->data_size = data_size;
    } else {
        event->data = NULL;
        event->data_size = 0;
    }

    return event;
}

/**
 * @brief Free an event
 *
 * @param event Event to free
 */
void kmcp_event_free(kmcp_event_t* event) {
    if (!event) {
        return;
    }

    // Free the event data if allocated
    if (event->data) {
        free(event->data);
    }

    // Free the event itself
    free(event);
}

/**
 * @brief Convenience function to trigger an event with the specified type and data
 *
 * @param type Event type
 * @param data Event data
 * @param data_size Size of event data
 * @param source Event source
 * @param source_name Name of event source
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_event_trigger_with_data(kmcp_event_type_t type,
                                         const void* data,
                                         size_t data_size,
                                         void* source,
                                         const char* source_name) {
    // Create the event
    kmcp_event_t* event = kmcp_event_create(type, data, data_size, source, source_name);
    if (!event) {
        return KMCP_ERROR_LOG(KMCP_ERROR_MEMORY_ALLOCATION, "Failed to create event");
    }

    // Trigger the event
    kmcp_error_t result = kmcp_event_trigger(event);

    // Free the event
    kmcp_event_free(event);

    return result;
}

/**
 * @brief Get the name of an event type
 *
 * @param event_type Event type
 * @return const char* Returns the name of the event type
 */
const char* kmcp_event_type_name(kmcp_event_type_t event_type) {
    switch (event_type) {
        case KMCP_EVENT_NONE:
            return "None";
        case KMCP_EVENT_SERVER_CONNECTED:
            return "ServerConnected";
        case KMCP_EVENT_SERVER_DISCONNECTED:
            return "ServerDisconnected";
        case KMCP_EVENT_SERVER_STARTED:
            return "ServerStarted";
        case KMCP_EVENT_SERVER_STOPPED:
            return "ServerStopped";
        case KMCP_EVENT_TOOL_CALLED:
            return "ToolCalled";
        case KMCP_EVENT_TOOL_COMPLETED:
            return "ToolCompleted";
        case KMCP_EVENT_RESOURCE_ACCESSED:
            return "ResourceAccessed";
        case KMCP_EVENT_CONFIG_CHANGED:
            return "ConfigChanged";
        case KMCP_EVENT_PROFILE_ACTIVATED:
            return "ProfileActivated";
        case KMCP_EVENT_PROFILE_DEACTIVATED:
            return "ProfileDeactivated";
        case KMCP_EVENT_ERROR:
            return "Error";
        case KMCP_EVENT_WARNING:
            return "Warning";
        case KMCP_EVENT_INFO:
            return "Info";
        case KMCP_EVENT_DEBUG:
            return "Debug";
        default:
            if (event_type >= KMCP_EVENT_CUSTOM) {
                return "Custom";
            }
            return "Unknown";
    }
}
