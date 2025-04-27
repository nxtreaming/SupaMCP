/**
 * @file kmcp_event.h
 * @brief Event system for KMCP module
 *
 * This file defines the event system for the KMCP module, which allows
 * components to communicate with each other in a loosely coupled manner.
 */

#ifndef KMCP_EVENT_H
#define KMCP_EVENT_H

#include "kmcp_error.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum number of event listeners per event type
 */
#define KMCP_EVENT_MAX_LISTENERS 16

/**
 * @brief Event types
 */
typedef enum {
    KMCP_EVENT_NONE = 0,                  /**< No event */
    KMCP_EVENT_SERVER_CONNECTED = 1,      /**< Server connected */
    KMCP_EVENT_SERVER_DISCONNECTED = 2,   /**< Server disconnected */
    KMCP_EVENT_SERVER_STARTED = 3,        /**< Server started */
    KMCP_EVENT_SERVER_STOPPED = 4,        /**< Server stopped */
    KMCP_EVENT_TOOL_CALLED = 5,           /**< Tool called */
    KMCP_EVENT_TOOL_COMPLETED = 6,        /**< Tool completed */
    KMCP_EVENT_RESOURCE_ACCESSED = 7,     /**< Resource accessed */
    KMCP_EVENT_CONFIG_CHANGED = 8,        /**< Configuration changed */
    KMCP_EVENT_PROFILE_ACTIVATED = 9,     /**< Profile activated */
    KMCP_EVENT_PROFILE_DEACTIVATED = 10,  /**< Profile deactivated */
    KMCP_EVENT_ERROR = 11,                /**< Error occurred */
    KMCP_EVENT_WARNING = 12,              /**< Warning occurred */
    KMCP_EVENT_INFO = 13,                 /**< Information message */
    KMCP_EVENT_DEBUG = 14,                /**< Debug message */
    KMCP_EVENT_CUSTOM = 1000              /**< Base for custom events */
} kmcp_event_type_t;

/**
 * @brief Event structure
 */
typedef struct {
    kmcp_event_type_t type;               /**< Event type */
    void* data;                           /**< Event data */
    size_t data_size;                     /**< Size of event data */
    void* source;                         /**< Event source */
    const char* source_name;              /**< Name of event source */
    unsigned long long timestamp;         /**< Event timestamp (milliseconds since epoch) */
} kmcp_event_t;

/**
 * @brief Event listener callback function
 *
 * @param event Event data
 * @param user_data User data passed to the listener registration function
 * @return bool Return true to continue processing the event, false to stop
 */
typedef bool (*kmcp_event_listener_fn)(const kmcp_event_t* event, void* user_data);

/**
 * @brief Initialize the event system
 *
 * This function initializes the event system. It must be called before any other
 * event system functions.
 *
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_event_init(void);

/**
 * @brief Shut down the event system
 *
 * This function shuts down the event system and frees all resources.
 */
void kmcp_event_shutdown(void);

/**
 * @brief Register an event listener
 *
 * This function registers a listener for a specific event type.
 *
 * @param event_type Event type to listen for
 * @param listener Listener callback function
 * @param user_data User data to pass to the listener
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_event_register_listener(kmcp_event_type_t event_type, 
                                         kmcp_event_listener_fn listener, 
                                         void* user_data);

/**
 * @brief Unregister an event listener
 *
 * This function unregisters a listener for a specific event type.
 *
 * @param event_type Event type to stop listening for
 * @param listener Listener callback function to unregister
 * @param user_data User data that was passed to the listener registration function
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_event_unregister_listener(kmcp_event_type_t event_type, 
                                           kmcp_event_listener_fn listener, 
                                           void* user_data);

/**
 * @brief Trigger an event
 *
 * This function triggers an event, which will be processed by all registered listeners.
 *
 * @param event Event to trigger
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, error code otherwise
 */
kmcp_error_t kmcp_event_trigger(const kmcp_event_t* event);

/**
 * @brief Create an event
 *
 * This function creates an event with the specified type and data.
 *
 * @param type Event type
 * @param data Event data
 * @param data_size Size of event data
 * @param source Event source
 * @param source_name Name of event source
 * @return kmcp_event_t* Returns a pointer to the created event, or NULL on failure
 *
 * @note The caller is responsible for freeing the event using kmcp_event_free()
 */
kmcp_event_t* kmcp_event_create(kmcp_event_type_t type, 
                               const void* data, 
                               size_t data_size, 
                               void* source, 
                               const char* source_name);

/**
 * @brief Free an event
 *
 * This function frees an event created with kmcp_event_create().
 *
 * @param event Event to free
 */
void kmcp_event_free(kmcp_event_t* event);

/**
 * @brief Convenience function to trigger an event with the specified type and data
 *
 * This function creates an event with the specified type and data, triggers it,
 * and then frees the event.
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
                                         const char* source_name);

/**
 * @brief Get the name of an event type
 *
 * This function returns the name of an event type.
 *
 * @param event_type Event type
 * @return const char* Returns the name of the event type
 */
const char* kmcp_event_type_name(kmcp_event_type_t event_type);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_EVENT_H */
