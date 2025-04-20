/**
 * @file kmcp_profile_manager.h
 * @brief Profile management for KMCP
 *
 * This file defines the profile management API for KMCP. Profiles are named
 * collections of server configurations that can be activated or deactivated.
 * This allows users to switch between different server configurations easily.
 */

#ifndef KMCP_PROFILE_MANAGER_H
#define KMCP_PROFILE_MANAGER_H

#include <stddef.h>
#include <stdbool.h>
#include "kmcp_error.h"
#include "kmcp_server_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Profile structure
 */
typedef struct kmcp_profile kmcp_profile_t;

/**
 * @brief Profile manager structure
 */
typedef struct kmcp_profile_manager kmcp_profile_manager_t;

/**
 * @brief Create a profile manager
 *
 * @return kmcp_profile_manager_t* Returns profile manager pointer on success, NULL on failure
 */
kmcp_profile_manager_t* kmcp_profile_manager_create(void);

/**
 * @brief Close a profile manager and free resources
 *
 * @param manager Profile manager (must not be NULL)
 */
void kmcp_profile_manager_close(kmcp_profile_manager_t* manager);

/**
 * @brief Create a new profile
 *
 * @param manager Profile manager (must not be NULL)
 * @param name Profile name (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_create(kmcp_profile_manager_t* manager, const char* name);

/**
 * @brief Delete a profile
 *
 * @param manager Profile manager (must not be NULL)
 * @param name Profile name (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_delete(kmcp_profile_manager_t* manager, const char* name);

/**
 * @brief Rename a profile
 *
 * @param manager Profile manager (must not be NULL)
 * @param old_name Old profile name (must not be NULL)
 * @param new_name New profile name (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_rename(kmcp_profile_manager_t* manager, const char* old_name, const char* new_name);

/**
 * @brief Activate a profile
 *
 * @param manager Profile manager (must not be NULL)
 * @param name Profile name (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_activate(kmcp_profile_manager_t* manager, const char* name);

/**
 * @brief Deactivate a profile
 *
 * @param manager Profile manager (must not be NULL)
 * @param name Profile name (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_deactivate(kmcp_profile_manager_t* manager, const char* name);

/**
 * @brief Get the active profile name
 *
 * @param manager Profile manager (must not be NULL)
 * @return const char* Returns the active profile name, or NULL if no profile is active
 */
const char* kmcp_profile_get_active(kmcp_profile_manager_t* manager);

/**
 * @brief Check if a profile exists
 *
 * @param manager Profile manager (must not be NULL)
 * @param name Profile name (must not be NULL)
 * @return bool Returns true if the profile exists, false otherwise
 */
bool kmcp_profile_exists(kmcp_profile_manager_t* manager, const char* name);

/**
 * @brief Get the number of profiles
 *
 * @param manager Profile manager (must not be NULL)
 * @return size_t Returns the number of profiles, or 0 on error
 */
size_t kmcp_profile_get_count(kmcp_profile_manager_t* manager);

/**
 * @brief Get a list of profile names
 *
 * @param manager Profile manager (must not be NULL)
 * @param names Pointer to an array of strings to store profile names (must not be NULL)
 * @param count Pointer to store the number of profiles (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 *
 * @note The caller is responsible for freeing each string in the names array
 * and the names array itself.
 */
kmcp_error_t kmcp_profile_get_names(kmcp_profile_manager_t* manager, char*** names, size_t* count);

/**
 * @brief Add a server to a profile
 *
 * @param manager Profile manager (must not be NULL)
 * @param profile_name Profile name (must not be NULL)
 * @param config Server configuration (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_add_server(kmcp_profile_manager_t* manager, const char* profile_name, kmcp_server_config_t* config);

/**
 * @brief Remove a server from a profile
 *
 * @param manager Profile manager (must not be NULL)
 * @param profile_name Profile name (must not be NULL)
 * @param server_name Server name (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_remove_server(kmcp_profile_manager_t* manager, const char* profile_name, const char* server_name);

/**
 * @brief Copy a server from one profile to another
 *
 * @param manager Profile manager (must not be NULL)
 * @param source_profile Source profile name (must not be NULL)
 * @param source_server Source server name (must not be NULL)
 * @param target_profile Target profile name (must not be NULL)
 * @param target_server Target server name (can be NULL to use source_server)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_copy_server(
    kmcp_profile_manager_t* manager,
    const char* source_profile,
    const char* source_server,
    const char* target_profile,
    const char* target_server
);

/**
 * @brief Move a server from one profile to another
 *
 * @param manager Profile manager (must not be NULL)
 * @param source_profile Source profile name (must not be NULL)
 * @param source_server Source server name (must not be NULL)
 * @param target_profile Target profile name (must not be NULL)
 * @param target_server Target server name (can be NULL to use source_server)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_move_server(
    kmcp_profile_manager_t* manager,
    const char* source_profile,
    const char* source_server,
    const char* target_profile,
    const char* target_server
);

/**
 * @brief Get the server manager for a profile
 *
 * @param manager Profile manager (must not be NULL)
 * @param profile_name Profile name (must not be NULL)
 * @return kmcp_server_manager_t* Returns the server manager for the profile, or NULL on error
 */
kmcp_server_manager_t* kmcp_profile_get_server_manager(kmcp_profile_manager_t* manager, const char* profile_name);

/**
 * @brief Save profiles to a file
 *
 * @param manager Profile manager (must not be NULL)
 * @param file_path File path (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_save(kmcp_profile_manager_t* manager, const char* file_path);

/**
 * @brief Load profiles from a file
 *
 * @param manager Profile manager (must not be NULL)
 * @param file_path File path (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_load(kmcp_profile_manager_t* manager, const char* file_path);

/**
 * @brief Export a profile to a file
 *
 * @param manager Profile manager (must not be NULL)
 * @param profile_name Profile name (must not be NULL)
 * @param file_path File path (must not be NULL)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_export(kmcp_profile_manager_t* manager, const char* profile_name, const char* file_path);

/**
 * @brief Import a profile from a file
 *
 * @param manager Profile manager (must not be NULL)
 * @param file_path File path (must not be NULL)
 * @param profile_name Profile name (can be NULL to use the name from the file)
 * @return kmcp_error_t Returns KMCP_SUCCESS on success, or an error code on failure
 */
kmcp_error_t kmcp_profile_import(kmcp_profile_manager_t* manager, const char* file_path, const char* profile_name);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_PROFILE_MANAGER_H */
