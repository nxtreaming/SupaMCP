/**
 * @file kmcp_profile_manager.c
 * @brief Profile management for KMCP
 */

#include "kmcp_profile_manager.h"
#include "kmcp_server_manager.h"
#include "kmcp_error.h"
#include "mcp_log.h"
#include "mcp_hashtable.h"
#include "mcp_sync.h"
#include "mcp_string_utils.h"
#include "mcp_json.h"
#include "mcp_json_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @brief Profile structure
 */
typedef struct kmcp_profile {
    char* name;                      /**< Profile name */
    kmcp_server_manager_t* servers;  /**< Server manager */
    bool is_active;                  /**< Whether this profile is active */
} kmcp_profile_t;

/**
 * @brief Callback function for freeing profile values in the hashtable
 *
 * @param key Hash table key (unused)
 * @param value Hash table value to free
 * @param user_data User data (unused)
 */
static void profile_value_free_callback(const void* key, void* value, void* user_data) {
    (void)key;       // Unused parameter
    (void)user_data; // Unused parameter
    if (value) {
        free(value);
    }
}

/**
 * @brief Profile manager structure
 */
struct kmcp_profile_manager {
    kmcp_profile_t** profiles;       /**< Array of profiles */
    size_t profile_count;            /**< Number of profiles */
    size_t profile_capacity;         /**< Capacity of profile array */
    mcp_hashtable_t* profile_map;    /**< Mapping from profile name to index */
    mcp_mutex_t* mutex;              /**< Thread safety lock */
    char* active_profile;            /**< Name of the active profile */
};

/**
 * @brief Create a profile manager
 *
 * @return kmcp_profile_manager_t* Returns a new profile manager or NULL on failure
 */
kmcp_profile_manager_t* kmcp_profile_manager_create(void) {
    mcp_log_debug("Creating profile manager");

    // Allocate memory for profile manager and initialize to zero
    kmcp_profile_manager_t* manager = (kmcp_profile_manager_t*)calloc(1, sizeof(kmcp_profile_manager_t));
    if (!manager) {
        mcp_log_error("Failed to allocate memory for profile manager");
        return NULL;
    }

    // Fields are already initialized to zero by calloc

    // Create mutex
    manager->mutex = mcp_mutex_create();
    if (!manager->mutex) {
        mcp_log_error("Failed to create mutex for profile manager");
        free(manager);
        return NULL;
    }

    // Create profile map
    manager->profile_map = mcp_hashtable_create(
        16,                             // initial_capacity
        0.75f,                          // load_factor_threshold
        mcp_hashtable_string_hash,      // hash_func
        mcp_hashtable_string_compare,   // key_compare
        mcp_hashtable_string_dup,       // key_dup
        mcp_hashtable_string_free,      // key_free
        free                            // value_free
    );
    if (!manager->profile_map) {
        mcp_log_error("Failed to create profile map");
        mcp_mutex_destroy(manager->mutex);
        free(manager);
        return NULL;
    }

    // Allocate initial profile array
    manager->profile_capacity = 8;
    manager->profiles = (kmcp_profile_t**)malloc(manager->profile_capacity * sizeof(kmcp_profile_t*));
    if (!manager->profiles) {
        mcp_log_error("Failed to allocate profile array");
        mcp_hashtable_destroy(manager->profile_map);
        mcp_mutex_destroy(manager->mutex);
        free(manager);
        return NULL;
    }

    // Initialize profile array
    memset(manager->profiles, 0, manager->profile_capacity * sizeof(kmcp_profile_t*));
    manager->profile_count = 0;
    manager->active_profile = NULL;

    mcp_log_info("Profile manager created successfully");
    return manager;
}

/**
 * @brief Close a profile manager and free resources
 *
 * @param manager Profile manager to close (can be NULL)
 */
void kmcp_profile_manager_close(kmcp_profile_manager_t* manager) {
    if (!manager) {
        return;
    }

    mcp_log_debug("Closing profile manager");

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Free profiles
    for (size_t i = 0; i < manager->profile_count; i++) {
        kmcp_profile_t* profile = manager->profiles[i];
        if (profile) {
            // Free profile name
            if (profile->name) {
                free(profile->name);
            }

            // Close server manager
            if (profile->servers) {
                kmcp_server_manager_close(profile->servers);
            }

            // Free profile
            free(profile);
        }
    }

    // Free profile array
    if (manager->profiles) {
        free(manager->profiles);
    }

    // Free active profile name
    if (manager->active_profile) {
        free(manager->active_profile);
    }

    // Unlock mutex before closing it
    mcp_mutex_unlock(manager->mutex);

    // Close profile map - need to free integer values stored in the hashtable
    if (manager->profile_map) {
        // Iterate through all entries and free the integer values
        mcp_hashtable_foreach(manager->profile_map, profile_value_free_callback, NULL);

        // Set value_free to NULL to prevent double-free in mcp_hashtable_destroy
        manager->profile_map->value_free = NULL;

        // Destroy the hashtable
        mcp_hashtable_destroy(manager->profile_map);
    }

    // Close mutex
    if (manager->mutex) {
        mcp_mutex_destroy(manager->mutex);
    }

    // Free manager
    free(manager);

    mcp_log_info("Profile manager closed");
}

/**
 * @brief Create a new profile
 */
kmcp_error_t kmcp_profile_create(kmcp_profile_manager_t* manager, const char* name) {
    if (!manager || !name) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_debug("Creating profile: %s", name);

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if profile already exists
    void* value = NULL;
    if (mcp_hashtable_get(manager->profile_map, name, &value) == 0 && value) {
        mcp_log_error("Profile already exists: %s", name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_ALREADY_EXISTS;
    }

    // Check if we need to resize the profile array
    if (manager->profile_count >= manager->profile_capacity) {
        // Double the capacity
        size_t new_capacity = manager->profile_capacity * 2;
        kmcp_profile_t** new_profiles = (kmcp_profile_t**)realloc(
            manager->profiles, new_capacity * sizeof(kmcp_profile_t*));

        if (!new_profiles) {
            mcp_log_error("Failed to resize profile array");
            mcp_mutex_unlock(manager->mutex);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        manager->profiles = new_profiles;
        manager->profile_capacity = new_capacity;
    }

    // Create new profile and initialize to zero
    kmcp_profile_t* profile = (kmcp_profile_t*)calloc(1, sizeof(kmcp_profile_t));
    if (!profile) {
        mcp_log_error("Failed to allocate memory for profile");
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }
    profile->name = mcp_strdup(name);
    if (!profile->name) {
        mcp_log_error("Failed to duplicate profile name");
        free(profile);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Create server manager for profile
    profile->servers = kmcp_server_manager_create();
    if (!profile->servers) {
        mcp_log_error("Failed to create server manager for profile");
        free(profile->name);
        free(profile);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Add profile to array
    manager->profiles[manager->profile_count] = profile;

    // Add profile to map
    int* index = (int*)malloc(sizeof(int));
    if (!index) {
        mcp_log_error("Failed to allocate memory for profile index");
        kmcp_server_manager_close(profile->servers);
        free(profile->name);
        free(profile);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    *index = (int)manager->profile_count;
    int result = mcp_hashtable_put(manager->profile_map, name, index);
    if (result != 0) {
        mcp_log_error("Failed to add profile to map");
        free(index);
        kmcp_server_manager_close(profile->servers);
        free(profile->name);
        free(profile);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_INTERNAL;
    }

    // Increment profile count
    manager->profile_count++;

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    mcp_log_info("Profile created: %s", name);
    return KMCP_SUCCESS;
}

/**
 * @brief Delete a profile
 *
 * @param manager Profile manager (must not be NULL)
 * @param name Profile name (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_delete(kmcp_profile_manager_t* manager, const char* name) {
    if (!manager || !name) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_debug("Deleting profile: %s", name);

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if profile exists
    void* value = NULL;
    if (mcp_hashtable_get(manager->profile_map, name, &value) != 0 || !value) {
        mcp_log_error("Profile not found: %s", name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_NOT_FOUND;
    }

    // Get profile index
    int index = *((int*)value);

    // Check if profile is active
    if (manager->active_profile && strcmp(manager->active_profile, name) == 0) {
        mcp_log_error("Cannot delete active profile: %s", name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_INVALID_OPERATION;
    }

    // Get profile
    kmcp_profile_t* profile = manager->profiles[index];

    // Remove profile from map
    mcp_hashtable_remove(manager->profile_map, name);

    // Free profile resources
    if (profile->name) {
        free(profile->name);
    }

    if (profile->servers) {
        kmcp_server_manager_close(profile->servers);
    }

    free(profile);

    // Shift profiles in array
    for (size_t i = index; i < manager->profile_count - 1; i++) {
        manager->profiles[i] = manager->profiles[i + 1];

        // Update index in map
        void* map_value = NULL;
        if (mcp_hashtable_get(manager->profile_map, manager->profiles[i]->name, &map_value) == 0 && map_value) {
            *((int*)map_value) = (int)i;
        }
    }

    // Clear last profile slot
    manager->profiles[manager->profile_count - 1] = NULL;

    // Decrement profile count
    manager->profile_count--;

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    mcp_log_info("Profile deleted: %s", name);
    return KMCP_SUCCESS;
}

/**
 * @brief Rename a profile
 */
kmcp_error_t kmcp_profile_rename(kmcp_profile_manager_t* manager, const char* old_name, const char* new_name) {
    if (!manager || !old_name || !new_name) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_debug("Renaming profile: %s to %s", old_name, new_name);

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if old profile exists
    void* old_value = NULL;
    if (mcp_hashtable_get(manager->profile_map, old_name, &old_value) != 0 || !old_value) {
        mcp_log_error("Profile not found: %s", old_name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_NOT_FOUND;
    }

    // Check if new profile name already exists
    void* new_value = NULL;
    if (mcp_hashtable_get(manager->profile_map, new_name, &new_value) == 0 && new_value) {
        mcp_log_error("Profile already exists: %s", new_name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_ALREADY_EXISTS;
    }

    // Get profile index
    int index = *((int*)old_value);

    // Get profile
    kmcp_profile_t* profile = manager->profiles[index];

    // Update profile name
    char* new_name_copy = mcp_strdup(new_name);
    if (!new_name_copy) {
        mcp_log_error("Failed to duplicate profile name");
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Free old name
    free(profile->name);
    profile->name = new_name_copy;

    // Remove old mapping
    mcp_hashtable_remove(manager->profile_map, old_name);

    // Add new mapping
    int* new_index = (int*)malloc(sizeof(int));
    if (!new_index) {
        mcp_log_error("Failed to allocate memory for profile index");
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    *new_index = index;
    int result = mcp_hashtable_put(manager->profile_map, new_name, new_index);
    if (result != 0) {
        mcp_log_error("Failed to add profile to map");
        free(new_index);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_INTERNAL;
    }

    // Update active profile name if needed
    if (manager->active_profile && strcmp(manager->active_profile, old_name) == 0) {
        free(manager->active_profile);
        manager->active_profile = mcp_strdup(new_name);
    }

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    mcp_log_info("Profile renamed: %s to %s", old_name, new_name);
    return KMCP_SUCCESS;
}

/**
 * @brief Activate a profile
 */
kmcp_error_t kmcp_profile_activate(kmcp_profile_manager_t* manager, const char* name) {
    if (!manager || !name) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_debug("Activating profile: %s", name);

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if profile exists
    void* value = NULL;
    if (mcp_hashtable_get(manager->profile_map, name, &value) != 0 || !value) {
        mcp_log_error("Profile not found: %s", name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_NOT_FOUND;
    }

    // Get profile index
    int index = *((int*)value);

    // Get profile
    kmcp_profile_t* profile = manager->profiles[index];

    // Check if profile is already active
    if (manager->active_profile && strcmp(manager->active_profile, name) == 0) {
        mcp_log_info("Profile is already active: %s", name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_SUCCESS;
    }

    // Deactivate current active profile if any
    if (manager->active_profile) {
        void* active_value = NULL;
        if (mcp_hashtable_get(manager->profile_map, manager->active_profile, &active_value) == 0 && active_value) {
            int active_index = *((int*)active_value);
            manager->profiles[active_index]->is_active = false;
        }

        free(manager->active_profile);
        manager->active_profile = NULL;
    }

    // Activate new profile
    profile->is_active = true;
    manager->active_profile = mcp_strdup(name);
    if (!manager->active_profile) {
        mcp_log_error("Failed to duplicate profile name");
        profile->is_active = false;
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    mcp_log_info("Profile activated: %s", name);
    return KMCP_SUCCESS;
}

/**
 * @brief Deactivate a profile
 */
kmcp_error_t kmcp_profile_deactivate(kmcp_profile_manager_t* manager, const char* name) {
    if (!manager || !name) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_debug("Deactivating profile: %s", name);

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if profile exists
    void* value = NULL;
    if (mcp_hashtable_get(manager->profile_map, name, &value) != 0 || !value) {
        mcp_log_error("Profile not found: %s", name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_NOT_FOUND;
    }

    // Check if profile is active
    if (!manager->active_profile || strcmp(manager->active_profile, name) != 0) {
        mcp_log_info("Profile is not active: %s", name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_SUCCESS;
    }

    // Get profile index
    int index = *((int*)value);

    // Get profile
    kmcp_profile_t* profile = manager->profiles[index];

    // Deactivate profile
    profile->is_active = false;
    free(manager->active_profile);
    manager->active_profile = NULL;

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    mcp_log_info("Profile deactivated: %s", name);
    return KMCP_SUCCESS;
}

/**
 * @brief Get the active profile name
 */
const char* kmcp_profile_get_active(kmcp_profile_manager_t* manager) {
    if (!manager) {
        mcp_log_error("Invalid parameter: manager is NULL");
        return NULL;
    }

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Get active profile name
    const char* active_profile = manager->active_profile;

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    return active_profile;
}

/**
 * @brief Check if a profile exists
 */
bool kmcp_profile_exists(kmcp_profile_manager_t* manager, const char* name) {
    if (!manager || !name) {
        mcp_log_error("Invalid parameters");
        return false;
    }

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if profile exists
    void* value = NULL;
    bool exists = (mcp_hashtable_get(manager->profile_map, name, &value) == 0 && value != NULL);

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    return exists;
}

/**
 * @brief Get the number of profiles
 */
size_t kmcp_profile_get_count(kmcp_profile_manager_t* manager) {
    if (!manager) {
        mcp_log_error("Invalid parameter: manager is NULL");
        return 0;
    }

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Get profile count
    size_t count = manager->profile_count;

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    return count;
}

/**
 * @brief Get a list of profile names
 */
kmcp_error_t kmcp_profile_get_names(kmcp_profile_manager_t* manager, char*** names, size_t* count) {
    if (!manager || !names || !count) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Allocate array for names
    *count = manager->profile_count;
    *names = (char**)malloc(manager->profile_count * sizeof(char*));
    if (!*names) {
        mcp_log_error("Failed to allocate memory for profile names");
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Copy profile names
    for (size_t i = 0; i < manager->profile_count; i++) {
        (*names)[i] = mcp_strdup(manager->profiles[i]->name);
        if (!(*names)[i]) {
            mcp_log_error("Failed to duplicate profile name");

            // Free already allocated names
            for (size_t j = 0; j < i; j++) {
                free((*names)[j]);
            }

            free(*names);
            *names = NULL;
            *count = 0;

            mcp_mutex_unlock(manager->mutex);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }
    }

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    return KMCP_SUCCESS;
}

/**
 * @brief Add a server to a profile
 */
kmcp_error_t kmcp_profile_add_server(kmcp_profile_manager_t* manager, const char* profile_name, kmcp_server_config_t* config) {
    if (!manager || !profile_name || !config) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_debug("Adding server to profile: %s", profile_name);

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if profile exists
    void* value = NULL;
    if (mcp_hashtable_get(manager->profile_map, profile_name, &value) != 0 || !value) {
        mcp_log_error("Profile not found: %s", profile_name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_NOT_FOUND;
    }

    // Get profile index
    int index = *((int*)value);

    // Get profile
    kmcp_profile_t* profile = manager->profiles[index];

    // Add server to profile's server manager
    kmcp_error_t result = kmcp_server_manager_add(profile->servers, config);

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to add server to profile: %s", profile_name);
    } else {
        mcp_log_info("Server added to profile: %s", profile_name);
    }

    return result;
}

/**
 * @brief Remove a server from a profile
 */
kmcp_error_t kmcp_profile_remove_server(kmcp_profile_manager_t* manager, const char* profile_name, const char* server_name) {
    if (!manager || !profile_name || !server_name) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_debug("Removing server from profile: %s, server: %s", profile_name, server_name);

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if profile exists
    void* value = NULL;
    if (mcp_hashtable_get(manager->profile_map, profile_name, &value) != 0 || !value) {
        mcp_log_error("Profile not found: %s", profile_name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_NOT_FOUND;
    }

    // Get profile index
    int index = *((int*)value);

    // Get profile
    kmcp_profile_t* profile = manager->profiles[index];

    // Remove server from profile's server manager
    kmcp_error_t result = kmcp_server_manager_remove(profile->servers, server_name);

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to remove server from profile: %s, server: %s", profile_name, server_name);
    } else {
        mcp_log_info("Server removed from profile: %s, server: %s", profile_name, server_name);
    }

    return result;
}

/**
 * @brief Copy a server from one profile to another
 */
kmcp_error_t kmcp_profile_copy_server(
    kmcp_profile_manager_t* manager,
    const char* source_profile,
    const char* source_server,
    const char* target_profile,
    const char* target_server
) {
    if (!manager || !source_profile || !source_server || !target_profile) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Use source server name as target server name if not specified
    if (!target_server) {
        target_server = source_server;
    }

    mcp_log_debug("Copying server from profile %s to profile %s: %s -> %s",
                 source_profile, target_profile, source_server, target_server);

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if source profile exists
    void* source_value = NULL;
    if (mcp_hashtable_get(manager->profile_map, source_profile, &source_value) != 0 || !source_value) {
        mcp_log_error("Source profile not found: %s", source_profile);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_NOT_FOUND;
    }

    // Check if target profile exists
    void* target_value = NULL;
    if (mcp_hashtable_get(manager->profile_map, target_profile, &target_value) != 0 || !target_value) {
        mcp_log_error("Target profile not found: %s", target_profile);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_NOT_FOUND;
    }

    // Get profile indices
    int source_index = *((int*)source_value);
    int target_index = *((int*)target_value);

    // Get profiles
    kmcp_profile_t* source = manager->profiles[source_index];
    kmcp_profile_t* target = manager->profiles[target_index];

    // Get server configuration from source profile
    kmcp_server_config_t* config = NULL;
    kmcp_error_t result = kmcp_server_manager_get_config(source->servers, source_server, &config);
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to get server configuration: %s", source_server);
        mcp_mutex_unlock(manager->mutex);
        return result;
    }

    // Create a copy of the server configuration with the new name
    kmcp_server_config_t* config_copy = kmcp_server_manager_config_clone(config);
    if (!config_copy) {
        mcp_log_error("Failed to clone server configuration");
        kmcp_server_manager_config_free(config);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Update server name if different
    if (strcmp(source_server, target_server) != 0) {
        free(config_copy->name);
        config_copy->name = mcp_strdup(target_server);
        if (!config_copy->name) {
            mcp_log_error("Failed to duplicate server name");
            kmcp_server_manager_config_free(config_copy);
            kmcp_server_manager_config_free(config);
            mcp_mutex_unlock(manager->mutex);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }
    }

    // Add server to target profile
    result = kmcp_server_manager_add(target->servers, config_copy);

    // Free original configuration
    kmcp_server_manager_config_free(config);

    // Free copy if it was added successfully (kmcp_server_manager_add makes its own copy)
    if (result == KMCP_SUCCESS) {
        kmcp_server_manager_config_free(config_copy);
    }

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to add server to target profile");
    } else {
        mcp_log_info("Server copied from profile %s to profile %s: %s -> %s",
                     source_profile, target_profile, source_server, target_server);
    }

    return result;
}

/**
 * @brief Move a server from one profile to another
 */
kmcp_error_t kmcp_profile_move_server(
    kmcp_profile_manager_t* manager,
    const char* source_profile,
    const char* source_server,
    const char* target_profile,
    const char* target_server
) {
    if (!manager || !source_profile || !source_server || !target_profile) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Use source server name as target server name if not specified
    if (!target_server) {
        target_server = source_server;
    }

    mcp_log_debug("Moving server from profile %s to profile %s: %s -> %s",
                 source_profile, target_profile, source_server, target_server);

    // First copy the server
    kmcp_error_t result = kmcp_profile_copy_server(
        manager, source_profile, source_server, target_profile, target_server);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to copy server during move operation");
        return result;
    }

    // Then remove it from the source profile
    result = kmcp_profile_remove_server(manager, source_profile, source_server);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to remove server from source profile during move operation");
        // Try to clean up by removing the server from the target profile
        kmcp_profile_remove_server(manager, target_profile, target_server);
        return result;
    }

    mcp_log_info("Server moved from profile %s to profile %s: %s -> %s",
                 source_profile, target_profile, source_server, target_server);

    return KMCP_SUCCESS;
}

/**
 * @brief Get the server manager for a profile
 */
kmcp_server_manager_t* kmcp_profile_get_server_manager(kmcp_profile_manager_t* manager, const char* profile_name) {
    if (!manager || !profile_name) {
        mcp_log_error("Invalid parameters");
        return NULL;
    }

    mcp_log_debug("Getting server manager for profile: %s", profile_name);

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if profile exists
    void* value = NULL;
    if (mcp_hashtable_get(manager->profile_map, profile_name, &value) != 0 || !value) {
        mcp_log_error("Profile not found: %s", profile_name);
        mcp_mutex_unlock(manager->mutex);
        return NULL;
    }

    // Get profile index
    int index = *((int*)value);

    // Get profile
    kmcp_profile_t* profile = manager->profiles[index];

    // Get server manager
    kmcp_server_manager_t* server_manager = profile->servers;

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    return server_manager;
}

/**
 * @brief Save profiles to a file
 */
kmcp_error_t kmcp_profile_save(kmcp_profile_manager_t* manager, const char* file_path) {
    if (!manager || !file_path) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_debug("Saving profiles to file: %s", file_path);

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Create JSON object
    mcp_json_t* json = mcp_json_object_create();
    if (!json) {
        mcp_log_error("Failed to create JSON object");
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Add version
    mcp_json_t* version_json = mcp_json_string_create("1.0");
    if (version_json) {
        mcp_json_object_set_property(json, "version", version_json);
    }

    // Add active profile
    if (manager->active_profile) {
        mcp_json_t* active_profile_json = mcp_json_string_create(manager->active_profile);
        if (active_profile_json) {
            mcp_json_object_set_property(json, "activeProfile", active_profile_json);
        }
    }

    // Create profiles array
    mcp_json_t* profiles_array = mcp_json_array_create();
    if (!profiles_array) {
        mcp_log_error("Failed to create profiles array");
        mcp_json_destroy(json);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Add profiles array to root object
    mcp_json_object_set_property(json, "profiles", profiles_array);

    // Add each profile
    for (size_t i = 0; i < manager->profile_count; i++) {
        kmcp_profile_t* profile = manager->profiles[i];

        // Create profile object
        mcp_json_t* profile_obj = mcp_json_object_create();
        if (!profile_obj) {
            mcp_log_error("Failed to create profile object");
            mcp_json_destroy(json);
            mcp_mutex_unlock(manager->mutex);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        // Add profile name
        mcp_json_t* name_json = mcp_json_string_create(profile->name);
        if (name_json) {
            mcp_json_object_set_property(profile_obj, "name", name_json);
        }

        // Add profile active state
        mcp_json_t* active_json = mcp_json_boolean_create(profile->is_active);
        if (active_json) {
            mcp_json_object_set_property(profile_obj, "active", active_json);
        }

        // Add servers
        mcp_json_t* servers_obj = mcp_json_object_create();
        if (!servers_obj) {
            mcp_log_error("Failed to create servers object");
            mcp_json_destroy(profile_obj);
            mcp_json_destroy(json);
            mcp_mutex_unlock(manager->mutex);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        // Add servers object to profile
        mcp_json_object_set_property(profile_obj, "servers", servers_obj);

        // Get server configurations
        size_t server_count = kmcp_server_manager_get_count(profile->servers);
        for (size_t j = 0; j < server_count; j++) {
            // Get server configuration
            kmcp_server_config_t* config = NULL;
            kmcp_error_t result = kmcp_server_manager_get_config_by_index(profile->servers, j, &config);
            if (result != KMCP_SUCCESS || !config) {
                mcp_log_warn("Failed to get server configuration at index %zu", j);
                continue;
            }

            // Create server object
            mcp_json_t* server_obj = mcp_json_object_create();
            if (!server_obj) {
                mcp_log_error("Failed to create server object");
                kmcp_server_manager_config_free(config);
                mcp_json_destroy(json);
                mcp_mutex_unlock(manager->mutex);
                return KMCP_ERROR_MEMORY_ALLOCATION;
            }

            // Add server properties
            if (config->is_http) {
                // HTTP server
                mcp_json_t* url_json = mcp_json_string_create(config->url ? config->url : "");
                if (url_json) {
                    mcp_json_object_set_property(server_obj, "url", url_json);
                }
                if (config->api_key) {
                    mcp_json_t* api_key_json = mcp_json_string_create(config->api_key);
                    if (api_key_json) {
                        mcp_json_object_set_property(server_obj, "apiKey", api_key_json);
                    }
                }
            } else {
                // Local process server
                mcp_json_t* command_json = mcp_json_string_create(config->command ? config->command : "");
                if (command_json) {
                    mcp_json_object_set_property(server_obj, "command", command_json);
                }

                // Add arguments array
                if (config->args && config->args_count > 0) {
                    mcp_json_t* args_array = mcp_json_array_create();
                    if (args_array) {
                        for (size_t k = 0; k < config->args_count; k++) {
                            if (config->args[k]) {
                                mcp_json_t* arg_json = mcp_json_string_create(config->args[k]);
                                if (arg_json) {
                                    mcp_json_array_add_item(args_array, arg_json);
                                }
                            }
                        }
                        mcp_json_object_set_property(server_obj, "args", args_array);
                    }
                }

                // Add environment variables array
                if (config->env && config->env_count > 0) {
                    mcp_json_t* env_array = mcp_json_array_create();
                    if (env_array) {
                        for (size_t k = 0; k < config->env_count; k++) {
                            if (config->env[k]) {
                                mcp_json_t* env_json = mcp_json_string_create(config->env[k]);
                                if (env_json) {
                                    mcp_json_array_add_item(env_array, env_json);
                                }
                            }
                        }
                        mcp_json_object_set_property(server_obj, "env", env_array);
                    }
                }
            }

            // Add server object to servers object
            mcp_json_object_set_property(servers_obj, config->name, server_obj);

            // Free server configuration
            kmcp_server_manager_config_free(config);
        }

        // Add profile object to profiles array
        mcp_json_array_add_item(profiles_array, profile_obj);
    }

    // Convert JSON to string
    char* json_str = mcp_json_stringify(json);

    // Free JSON object
    mcp_json_destroy(json);

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    if (!json_str) {
        mcp_log_error("Failed to convert JSON to string");
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Write to file
    FILE* file = fopen(file_path, "w");
    if (!file) {
        mcp_log_error("Failed to open file for writing: %s", file_path);
        free(json_str);
        return KMCP_ERROR_FILE_NOT_FOUND;
    }

    // Write JSON string to file
    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, file);

    // Close file
    fclose(file);

    // Free JSON string
    free(json_str);

    if (written != len) {
        mcp_log_error("Failed to write to file: %s", file_path);
        return KMCP_ERROR_IO;
    }

    mcp_log_info("Profiles saved to file: %s", file_path);
    return KMCP_SUCCESS;
}

/**
 * @brief Load profiles from a file (Part 1 - Function header and initial validation)
 */
kmcp_error_t kmcp_profile_load(kmcp_profile_manager_t* manager, const char* file_path) {
    if (!manager || !file_path) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_debug("Loading profiles from file: %s", file_path);

    // Read file content
    FILE* file = fopen(file_path, "r");
    if (!file) {
        mcp_log_error("Failed to open file for reading: %s", file_path);
        return KMCP_ERROR_FILE_NOT_FOUND;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        mcp_log_error("Empty or invalid file: %s", file_path);
        fclose(file);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Allocate memory for file content
    char* json_str = (char*)malloc(file_size + 1);
    if (!json_str) {
        mcp_log_error("Failed to allocate memory for file content");
        fclose(file);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Read file content
    size_t read_size = fread(json_str, 1, file_size, file);
    fclose(file);

    if (read_size != (size_t)file_size) {
        mcp_log_error("Failed to read file content: %s", file_path);
        free(json_str);
        return KMCP_ERROR_IO;
    }

    // Null-terminate the string
    json_str[file_size] = '\0';

    // Parse JSON
    mcp_json_t* json = mcp_json_parse(json_str);
    free(json_str); // Free the string as we don't need it anymore

    if (!json) {
        mcp_log_error("Failed to parse JSON from file: %s", file_path);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Clear existing profiles
    for (size_t i = 0; i < manager->profile_count; i++) {
        kmcp_profile_t* profile = manager->profiles[i];
        if (profile) {
            // Free profile name
            if (profile->name) {
                free(profile->name);
            }

            // Close server manager
            if (profile->servers) {
                kmcp_server_manager_close(profile->servers);
            }

            // Free profile
            free(profile);
        }
    }

    // Reset profile count
    manager->profile_count = 0;

    // Clear profile map
    mcp_hashtable_clear(manager->profile_map);

    // Free active profile name
    if (manager->active_profile) {
        free(manager->active_profile);
        manager->active_profile = NULL;
    }

    // Get active profile from JSON
    const char* active_profile = NULL;
    mcp_json_get_string(mcp_json_object_get_property(json, "activeProfile"), &active_profile);
    if (active_profile) {
        manager->active_profile = mcp_strdup(active_profile);
    }

    // Get profiles array
    mcp_json_t* profiles_array = mcp_json_object_get_property(json, "profiles");
    if (profiles_array && mcp_json_get_type(profiles_array) != MCP_JSON_ARRAY) {
        profiles_array = NULL;
    }
    if (!profiles_array) {
        mcp_log_error("No profiles array found in file: %s", file_path);
        mcp_json_destroy(json);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Get number of profiles
    int array_size = mcp_json_array_get_size(profiles_array);
    size_t profile_count = (array_size >= 0) ? (size_t)array_size : 0;

    // Check if we need to resize the profile array
    if (profile_count > manager->profile_capacity) {
        // Resize the profile array
        kmcp_profile_t** new_profiles = (kmcp_profile_t**)realloc(
            manager->profiles, profile_count * sizeof(kmcp_profile_t*));

        if (!new_profiles) {
            mcp_log_error("Failed to resize profile array");
            mcp_json_destroy(json);
            mcp_mutex_unlock(manager->mutex);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        manager->profiles = new_profiles;
        manager->profile_capacity = profile_count;
    }

    // Process each profile
    for (size_t i = 0; i < profile_count; i++) {
        // Get profile object
        mcp_json_t* profile_obj = mcp_json_array_get_item(profiles_array, (int)i);
        if (!profile_obj) {
            mcp_log_warn("Failed to get profile at index %zu", i);
            continue;
        }

        // Get profile name
        const char* name = NULL;
        mcp_json_get_string(mcp_json_object_get_property(profile_obj, "name"), &name);
        if (!name) {
            mcp_log_warn("Profile at index %zu has no name", i);
            continue;
        }

        // Create new profile
        kmcp_profile_t* profile = (kmcp_profile_t*)malloc(sizeof(kmcp_profile_t));
        if (!profile) {
            mcp_log_error("Failed to allocate memory for profile");
            continue;
        }

        // Initialize profile
        memset(profile, 0, sizeof(kmcp_profile_t));
        profile->name = mcp_strdup(name);
        if (!profile->name) {
            mcp_log_error("Failed to duplicate profile name");
            free(profile);
            continue;
        }

        // Get profile active state
        mcp_json_t* active_json = mcp_json_object_get_property(profile_obj, "active");
        if (active_json && mcp_json_get_type(active_json) == MCP_JSON_BOOLEAN) {
            bool active_value = false;
            if (mcp_json_get_boolean(active_json, &active_value) == 0) {
                profile->is_active = active_value;
            }
        }

        // Create server manager for profile
        profile->servers = kmcp_server_manager_create();
        if (!profile->servers) {
            mcp_log_error("Failed to create server manager for profile");
            free(profile->name);
            free(profile);
            continue;
        }

        // Get servers object
        mcp_json_t* servers_obj = mcp_json_object_get_property(profile_obj, "servers");
        if (servers_obj && mcp_json_get_type(servers_obj) != MCP_JSON_OBJECT) {
            servers_obj = NULL;
        }
        if (servers_obj) {
            // Process each server
            char** server_names = NULL;
            size_t server_count = 0;
            mcp_json_object_get_property_names(servers_obj, &server_names, &server_count);

            if (server_names) {
                for (size_t j = 0; j < server_count; j++) {
                    const char* server_name = server_names[j];
                    if (!server_name) {
                        continue;
                    }

                    // Get server object
                    mcp_json_t* server_obj = mcp_json_object_get_property(servers_obj, server_name);
                    if (server_obj && mcp_json_get_type(server_obj) != MCP_JSON_OBJECT) {
                        server_obj = NULL;
                    }
                    if (!server_obj) {
                        continue;
                    }

                    // Create server configuration
                    kmcp_server_config_t* config = (kmcp_server_config_t*)malloc(sizeof(kmcp_server_config_t));
                    if (!config) {
                        mcp_log_error("Failed to allocate memory for server configuration");
                        continue;
                    }

                    // Initialize configuration
                    memset(config, 0, sizeof(kmcp_server_config_t));
                    config->name = mcp_strdup(server_name);

                    // Check if this is an HTTP server
                    const char* url = NULL;
                    mcp_json_get_string(mcp_json_object_get_property(server_obj, "url"), &url);
                    if (url) {
                        // HTTP server
                        config->is_http = true;
                        config->url = mcp_strdup(url);

                        // Get API key if present
                        const char* api_key = NULL;
                        mcp_json_get_string(mcp_json_object_get_property(server_obj, "apiKey"), &api_key);
                        if (api_key) {
                            config->api_key = mcp_strdup(api_key);
                        }
                    } else {
                        // Local process server
                        config->is_http = false;

                        // Get command
                        const char* command = NULL;
                        mcp_json_get_string(mcp_json_object_get_property(server_obj, "command"), &command);
                        if (command) {
                            config->command = mcp_strdup(command);
                        }

                        // Get arguments
                        mcp_json_t* args_array = mcp_json_object_get_property(server_obj, "args");
                        if (args_array && mcp_json_get_type(args_array) != MCP_JSON_ARRAY) {
                            args_array = NULL;
                        }
                        if (args_array) {
                            int args_array_size = mcp_json_array_get_size(args_array);
                            size_t args_count = (args_array_size >= 0) ? (size_t)args_array_size : 0;
                            if (args_count > 0) {
                                config->args = (char**)malloc(args_count * sizeof(char*));
                                if (config->args) {
                                    config->args_count = args_count;

                                    for (size_t k = 0; k < args_count; k++) {
                                        const char* arg = NULL;
                                        mcp_json_t* arg_item = mcp_json_array_get_item(args_array, (int)k);
                                        if (arg_item) {
                                            mcp_json_get_string(arg_item, &arg);
                                        }
                                        if (arg) {
                                            config->args[k] = mcp_strdup(arg);
                                        } else {
                                            config->args[k] = NULL;
                                        }
                                    }
                                }
                            }
                        }

                        // Get environment variables
                        mcp_json_t* env_array = mcp_json_object_get_property(server_obj, "env");
                        if (env_array && mcp_json_get_type(env_array) != MCP_JSON_ARRAY) {
                            env_array = NULL;
                        }
                        if (env_array) {
                            int env_array_size = mcp_json_array_get_size(env_array);
                            size_t env_count = (env_array_size >= 0) ? (size_t)env_array_size : 0;
                            if (env_count > 0) {
                                config->env = (char**)malloc(env_count * sizeof(char*));
                                if (config->env) {
                                    config->env_count = env_count;

                                    for (size_t k = 0; k < env_count; k++) {
                                        const char* env = NULL;
                                        mcp_json_t* env_item = mcp_json_array_get_item(env_array, (int)k);
                                        if (env_item) {
                                            mcp_json_get_string(env_item, &env);
                                        }
                                        if (env) {
                                            config->env[k] = mcp_strdup(env);
                                        } else {
                                            config->env[k] = NULL;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Add server to profile
                    kmcp_error_t result = kmcp_server_manager_add(profile->servers, config);
                    if (result != KMCP_SUCCESS) {
                        mcp_log_error("Failed to add server to profile: %s", server_name);
                    }

                    // Free server configuration (kmcp_server_manager_add makes a copy)
                    kmcp_server_manager_config_free(config);
                }

                // Free server names
                for (size_t j = 0; j < server_count; j++) {
                    if (server_names[j]) {
                        free(server_names[j]);
                    }
                }
                free(server_names);
            }
        }

        // Add profile to array
        manager->profiles[manager->profile_count] = profile;

        // Add profile to map
        int* index = (int*)malloc(sizeof(int));
        if (!index) {
            mcp_log_error("Failed to allocate memory for profile index");
            continue;
        }

        *index = (int)manager->profile_count;
        int result = mcp_hashtable_put(manager->profile_map, name, index);
        if (result != 0) {
            mcp_log_error("Failed to add profile to map");
            free(index);
            continue;
        }

        // Increment profile count
        manager->profile_count++;
    }

    // Free JSON object
    mcp_json_destroy(json);

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    mcp_log_info("Profiles loaded from file: %s", file_path);
    return KMCP_SUCCESS;
}

/**
 * @brief Export a profile to a file
 */
kmcp_error_t kmcp_profile_export(kmcp_profile_manager_t* manager, const char* profile_name, const char* file_path) {
    if (!manager || !profile_name || !file_path) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_debug("Exporting profile to file: %s -> %s", profile_name, file_path);

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if profile exists
    void* value = NULL;
    if (mcp_hashtable_get(manager->profile_map, profile_name, &value) != 0 || !value) {
        mcp_log_error("Profile not found: %s", profile_name);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_NOT_FOUND;
    }

    // Get profile index
    int index = *((int*)value);

    // Get profile
    kmcp_profile_t* profile = manager->profiles[index];

    // Create JSON object
    mcp_json_t* json = mcp_json_object_create();
    if (!json) {
        mcp_log_error("Failed to create JSON object");
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Add version
    mcp_json_t* version_json = mcp_json_string_create("1.0");
    if (version_json) {
        mcp_json_object_set_property(json, "version", version_json);
    }

    // Create profiles array
    mcp_json_t* profiles_array = mcp_json_array_create();
    if (!profiles_array) {
        mcp_log_error("Failed to create profiles array");
        mcp_json_destroy(json);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Add profiles array to root object
    mcp_json_object_set_property(json, "profiles", profiles_array);

    // Create profile object
    mcp_json_t* profile_obj = mcp_json_object_create();
    if (!profile_obj) {
        mcp_log_error("Failed to create profile object");
        mcp_json_destroy(json);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Add profile name
    mcp_json_t* name_json = mcp_json_string_create(profile->name);
    if (name_json) {
        mcp_json_object_set_property(profile_obj, "name", name_json);
    }

    // Add profile active state
    mcp_json_t* active_json = mcp_json_boolean_create(profile->is_active);
    if (active_json) {
        mcp_json_object_set_property(profile_obj, "active", active_json);
    }

    // Add servers
    mcp_json_t* servers_obj = mcp_json_object_create();
    if (!servers_obj) {
        mcp_log_error("Failed to create servers object");
        mcp_json_destroy(profile_obj);
        mcp_json_destroy(json);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Add servers object to profile
    mcp_json_object_set_property(profile_obj, "servers", servers_obj);

    // Get server configurations
    size_t server_count = kmcp_server_manager_get_count(profile->servers);
    for (size_t j = 0; j < server_count; j++) {
        // Get server configuration
        kmcp_server_config_t* config = NULL;
        kmcp_error_t result = kmcp_server_manager_get_config_by_index(profile->servers, j, &config);
        if (result != KMCP_SUCCESS || !config) {
            mcp_log_warn("Failed to get server configuration at index %zu", j);
            continue;
        }

        // Create server object
        mcp_json_t* server_obj = mcp_json_object_create();
        if (!server_obj) {
            mcp_log_error("Failed to create server object");
            kmcp_server_manager_config_free(config);
            mcp_json_destroy(json);
            mcp_mutex_unlock(manager->mutex);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        // Add server properties
        if (config->is_http) {
            // HTTP server
            mcp_json_t* url_json = mcp_json_string_create(config->url ? config->url : "");
            if (url_json) {
                mcp_json_object_set_property(server_obj, "url", url_json);
            }
            if (config->api_key) {
                mcp_json_t* api_key_json = mcp_json_string_create(config->api_key);
                if (api_key_json) {
                    mcp_json_object_set_property(server_obj, "apiKey", api_key_json);
                }
            }
        } else {
            // Local process server
            mcp_json_t* command_json = mcp_json_string_create(config->command ? config->command : "");
            if (command_json) {
                mcp_json_object_set_property(server_obj, "command", command_json);
            }

            // Add arguments array
            if (config->args && config->args_count > 0) {
                mcp_json_t* args_array = mcp_json_array_create();
                if (args_array) {
                    for (size_t k = 0; k < config->args_count; k++) {
                        if (config->args[k]) {
                            mcp_json_t* arg_json = mcp_json_string_create(config->args[k]);
                            if (arg_json) {
                                mcp_json_array_add_item(args_array, arg_json);
                            }
                        }
                    }
                    mcp_json_object_set_property(server_obj, "args", args_array);
                }
            }

            // Add environment variables array
            if (config->env && config->env_count > 0) {
                mcp_json_t* env_array = mcp_json_array_create();
                if (env_array) {
                    for (size_t k = 0; k < config->env_count; k++) {
                        if (config->env[k]) {
                            mcp_json_t* env_json = mcp_json_string_create(config->env[k]);
                            if (env_json) {
                                mcp_json_array_add_item(env_array, env_json);
                            }
                        }
                    }
                    mcp_json_object_set_property(server_obj, "env", env_array);
                }
            }
        }

        // Add server object to servers object
        mcp_json_object_set_property(servers_obj, config->name, server_obj);

        // Free server configuration
        kmcp_server_manager_config_free(config);
    }

    // Add profile object to profiles array
    mcp_json_array_add_item(profiles_array, profile_obj);

    // Convert JSON to string
    char* json_str = mcp_json_stringify(json);

    // Free JSON object
    mcp_json_destroy(json);

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    if (!json_str) {
        mcp_log_error("Failed to convert JSON to string");
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Write to file
    FILE* file = fopen(file_path, "w");
    if (!file) {
        mcp_log_error("Failed to open file for writing: %s", file_path);
        free(json_str);
        return KMCP_ERROR_FILE_NOT_FOUND;
    }

    // Write JSON string to file
    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, file);

    // Close file
    fclose(file);

    // Free JSON string
    free(json_str);

    if (written != len) {
        mcp_log_error("Failed to write to file: %s", file_path);
        return KMCP_ERROR_IO;
    }

    mcp_log_info("Profile exported to file: %s -> %s", profile_name, file_path);
    return KMCP_SUCCESS;
}

/**
 * @brief Import a profile from a file
 */
kmcp_error_t kmcp_profile_import(kmcp_profile_manager_t* manager, const char* file_path, const char* profile_name) {
    if (!manager || !file_path) {
        mcp_log_error("Invalid parameters");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_debug("Importing profile from file: %s", file_path);

    // Read file content
    FILE* file = fopen(file_path, "r");
    if (!file) {
        mcp_log_error("Failed to open file for reading: %s", file_path);
        return KMCP_ERROR_FILE_NOT_FOUND;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        mcp_log_error("Empty or invalid file: %s", file_path);
        fclose(file);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Allocate memory for file content
    char* json_str = (char*)malloc(file_size + 1);
    if (!json_str) {
        mcp_log_error("Failed to allocate memory for file content");
        fclose(file);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Read file content
    size_t read_size = fread(json_str, 1, file_size, file);
    fclose(file);

    if (read_size != (size_t)file_size) {
        mcp_log_error("Failed to read file content: %s", file_path);
        free(json_str);
        return KMCP_ERROR_IO;
    }

    // Null-terminate the string
    json_str[file_size] = '\0';

    // Parse JSON
    mcp_json_t* json = mcp_json_parse(json_str);
    free(json_str); // Free the string as we don't need it anymore

    if (!json) {
        mcp_log_error("Failed to parse JSON from file: %s", file_path);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Get profiles array
    mcp_json_t* profiles_array = mcp_json_object_get_property(json, "profiles");
    if (profiles_array && mcp_json_get_type(profiles_array) != MCP_JSON_ARRAY) {
        profiles_array = NULL;
    }
    if (!profiles_array) {
        mcp_log_error("No profiles array found in file: %s", file_path);
        mcp_json_destroy(json);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Get number of profiles
    int array_size = mcp_json_array_get_size(profiles_array);
    size_t profile_count = (array_size >= 0) ? (size_t)array_size : 0;
    if (profile_count == 0) {
        mcp_log_error("No profiles found in file: %s", file_path);
        mcp_json_destroy(json);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Get first profile
    mcp_json_t* profile_obj = mcp_json_array_get_item(profiles_array, 0);
    if (!profile_obj) {
        mcp_log_error("Failed to get profile from file: %s", file_path);
        mcp_json_destroy(json);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Get profile name
    const char* name = NULL;
    mcp_json_get_string(mcp_json_object_get_property(profile_obj, "name"), &name);
    if (!name) {
        mcp_log_error("Profile has no name in file: %s", file_path);
        mcp_json_destroy(json);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Use provided profile name if specified
    if (profile_name) {
        name = profile_name;
    }

    // Lock mutex
    mcp_mutex_lock(manager->mutex);

    // Check if profile already exists
    void* value = NULL;
    if (mcp_hashtable_get(manager->profile_map, name, &value) == 0 && value) {
        mcp_log_error("Profile already exists: %s", name);
        mcp_json_destroy(json);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_ALREADY_EXISTS;
    }

    // Create new profile
    kmcp_profile_t* profile = (kmcp_profile_t*)malloc(sizeof(kmcp_profile_t));
    if (!profile) {
        mcp_log_error("Failed to allocate memory for profile");
        mcp_json_destroy(json);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Initialize profile
    memset(profile, 0, sizeof(kmcp_profile_t));
    profile->name = mcp_strdup(name);
    if (!profile->name) {
        mcp_log_error("Failed to duplicate profile name");
        free(profile);
        mcp_json_destroy(json);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Get profile active state
    mcp_json_t* active_json = mcp_json_object_get_property(profile_obj, "active");
    if (active_json && mcp_json_get_type(active_json) == MCP_JSON_BOOLEAN) {
        bool active_value = false;
        if (mcp_json_get_boolean(active_json, &active_value) == 0) {
            profile->is_active = active_value;
        }
    }

    // Create server manager for profile
    profile->servers = kmcp_server_manager_create();
    if (!profile->servers) {
        mcp_log_error("Failed to create server manager for profile");
        free(profile->name);
        free(profile);
        mcp_json_destroy(json);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Get servers object
    mcp_json_t* servers_obj = mcp_json_object_get_property(profile_obj, "servers");
    if (servers_obj && mcp_json_get_type(servers_obj) != MCP_JSON_OBJECT) {
        servers_obj = NULL;
    }
    if (servers_obj) {
        // Process each server
        char** server_names = NULL;
        size_t server_count = 0;
        mcp_json_object_get_property_names(servers_obj, &server_names, &server_count);

        if (server_names) {
            for (size_t j = 0; j < server_count; j++) {
                const char* server_name = server_names[j];
                if (!server_name) {
                    continue;
                }

                // Get server object
                mcp_json_t* server_obj = mcp_json_object_get_property(servers_obj, server_name);
                if (server_obj && mcp_json_get_type(server_obj) != MCP_JSON_OBJECT) {
                    server_obj = NULL;
                }
                if (!server_obj) {
                    continue;
                }

                // Create server configuration
                kmcp_server_config_t* config = (kmcp_server_config_t*)malloc(sizeof(kmcp_server_config_t));
                if (!config) {
                    mcp_log_error("Failed to allocate memory for server configuration");
                    continue;
                }

                // Initialize configuration
                memset(config, 0, sizeof(kmcp_server_config_t));
                config->name = mcp_strdup(server_name);

                // Check if this is an HTTP server
                const char* url = NULL;
                mcp_json_get_string(mcp_json_object_get_property(server_obj, "url"), &url);
                if (url) {
                    // HTTP server
                    config->is_http = true;
                    config->url = mcp_strdup(url);

                    // Get API key if present
                    const char* api_key = NULL;
                    mcp_json_get_string(mcp_json_object_get_property(server_obj, "apiKey"), &api_key);
                    if (api_key) {
                        config->api_key = mcp_strdup(api_key);
                    }
                } else {
                    // Local process server
                    config->is_http = false;

                    // Get command
                    const char* command = NULL;
                    mcp_json_get_string(mcp_json_object_get_property(server_obj, "command"), &command);
                    if (command) {
                        config->command = mcp_strdup(command);
                    }

                    // Get arguments
                    mcp_json_t* args_array = mcp_json_object_get_property(server_obj, "args");
                    if (args_array && mcp_json_get_type(args_array) != MCP_JSON_ARRAY) {
                        args_array = NULL;
                    }
                    if (args_array) {
                        int args_array_size = mcp_json_array_get_size(args_array);
                        size_t args_count = (args_array_size >= 0) ? (size_t)args_array_size : 0;
                        if (args_count > 0) {
                            config->args = (char**)malloc(args_count * sizeof(char*));
                            if (config->args) {
                                config->args_count = args_count;

                                for (size_t k = 0; k < args_count; k++) {
                                    const char* arg = NULL;
                                    mcp_json_t* arg_item = mcp_json_array_get_item(args_array, (int)k);
                                    if (arg_item) {
                                        mcp_json_get_string(arg_item, &arg);
                                    }
                                    if (arg) {
                                        config->args[k] = mcp_strdup(arg);
                                    } else {
                                        config->args[k] = NULL;
                                    }
                                }
                            }
                        }
                    }

                    // Get environment variables
                    mcp_json_t* env_array = mcp_json_object_get_property(server_obj, "env");
                    if (env_array && mcp_json_get_type(env_array) != MCP_JSON_ARRAY) {
                        env_array = NULL;
                    }
                    if (env_array) {
                        int env_array_size = mcp_json_array_get_size(env_array);
                        size_t env_count = (env_array_size >= 0) ? (size_t)env_array_size : 0;
                        if (env_count > 0) {
                            config->env = (char**)malloc(env_count * sizeof(char*));
                            if (config->env) {
                                config->env_count = env_count;

                                for (size_t k = 0; k < env_count; k++) {
                                    const char* env = NULL;
                                    mcp_json_t* env_item = mcp_json_array_get_item(env_array, (int)k);
                                    if (env_item) {
                                        mcp_json_get_string(env_item, &env);
                                    }
                                    if (env) {
                                        config->env[k] = mcp_strdup(env);
                                    } else {
                                        config->env[k] = NULL;
                                    }
                                }
                            }
                        }
                    }
                }

                // Add server to profile
                kmcp_error_t result = kmcp_server_manager_add(profile->servers, config);
                if (result != KMCP_SUCCESS) {
                    mcp_log_error("Failed to add server to profile: %s", server_name);
                }

                // Free server configuration (kmcp_server_manager_add makes a copy)
                kmcp_server_manager_config_free(config);
            }

            // Free server names
            for (size_t j = 0; j < server_count; j++) {
                if (server_names[j]) {
                    free(server_names[j]);
                }
            }
            free(server_names);
        }
    }

    // Check if we need to resize the profile array
    if (manager->profile_count >= manager->profile_capacity) {
        // Double the capacity
        size_t new_capacity = manager->profile_capacity * 2;
        kmcp_profile_t** new_profiles = (kmcp_profile_t**)realloc(
            manager->profiles, new_capacity * sizeof(kmcp_profile_t*));

        if (!new_profiles) {
            mcp_log_error("Failed to resize profile array");
            kmcp_server_manager_close(profile->servers);
            free(profile->name);
            free(profile);
            mcp_json_destroy(json);
            mcp_mutex_unlock(manager->mutex);
            return KMCP_ERROR_MEMORY_ALLOCATION;
        }

        manager->profiles = new_profiles;
        manager->profile_capacity = new_capacity;
    }

    // Add profile to array
    manager->profiles[manager->profile_count] = profile;

    // Add profile to map
    int* index = (int*)malloc(sizeof(int));
    if (!index) {
        mcp_log_error("Failed to allocate memory for profile index");
        kmcp_server_manager_close(profile->servers);
        free(profile->name);
        free(profile);
        mcp_json_destroy(json);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    *index = (int)manager->profile_count;
    int result = mcp_hashtable_put(manager->profile_map, name, index);
    if (result != 0) {
        mcp_log_error("Failed to add profile to map");
        free(index);
        kmcp_server_manager_close(profile->servers);
        free(profile->name);
        free(profile);
        mcp_json_destroy(json);
        mcp_mutex_unlock(manager->mutex);
        return KMCP_ERROR_INTERNAL;
    }

    // Increment profile count
    manager->profile_count++;

    // Free JSON object
    mcp_json_destroy(json);

    // Unlock mutex
    mcp_mutex_unlock(manager->mutex);

    mcp_log_info("Profile imported from file: %s -> %s", file_path, name);
    return KMCP_SUCCESS;
}