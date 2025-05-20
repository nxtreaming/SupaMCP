#include "mcp_plugin.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/**
 * @brief Name of the function that plugins must export to provide their descriptor
 */
#define MCP_PLUGIN_DESCRIPTOR_FUNC "mcp_plugin_get_descriptor"

/**
 * @brief Function pointer type for the plugin descriptor function
 */
typedef const mcp_plugin_descriptor_t* (*mcp_plugin_get_descriptor_func_t)(void);

/**
 * @internal
 * @brief Internal structure representing a loaded plugin instance.
 */
struct mcp_plugin {
#ifdef _WIN32
    HINSTANCE library_handle; /**< Handle from LoadLibrary on Windows */
#else
    void* library_handle;     /**< Handle from dlopen on POSIX systems */
#endif
    const mcp_plugin_descriptor_t* descriptor; /**< Pointer to the plugin's descriptor */
    char* path;               /**< Path to the plugin file (for logging/debugging) */
};

/**
 * @brief Frees a plugin structure and its resources
 *
 * @param plugin The plugin to free
 */
static void free_plugin(mcp_plugin_t* plugin) {
    if (!plugin) return;

    free(plugin->path);
    free(plugin);
}

/**
 * @brief Unloads a plugin's library and frees the plugin structure
 *
 * @param plugin The plugin to unload and free
 */
static void unload_and_free_plugin(mcp_plugin_t* plugin) {
    if (!plugin) return;

#ifdef _WIN32
    if (plugin->library_handle) {
        FreeLibrary(plugin->library_handle);
    }
#else
    if (plugin->library_handle) {
        dlclose(plugin->library_handle);
    }
#endif

    free_plugin(plugin);
}

#ifdef _WIN32
/**
 * @brief Gets a formatted Windows error message
 *
 * @param error_code The Windows error code
 * @return Formatted error message (must be freed with LocalFree)
 */
static LPSTR get_windows_error_message(DWORD error_code) {
    LPSTR message_buffer = NULL;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&message_buffer,
        0,
        NULL
    );
    return message_buffer;
}
#endif

/**
 * @brief Loads a plugin from a shared library file.
 *
 * @param path Path to the plugin shared library
 * @param server_context Context pointer to pass to the plugin's initialize function
 * @return Loaded plugin handle or NULL on failure
 */
mcp_plugin_t* mcp_plugin_load(const char* path, void* server_context) {
    if (!path) {
        mcp_log_error("Plugin path is NULL");
        return NULL;
    }

    mcp_log_info("Attempting to load plugin from: %s", path);

    // Allocate plugin structure
    mcp_plugin_t* plugin = (mcp_plugin_t*)calloc(1, sizeof(mcp_plugin_t));
    if (!plugin) {
        mcp_log_error("Failed to allocate plugin structure for %s", path);
        return NULL;
    }

    // Store path for logging/debugging
    plugin->path = mcp_strdup(path);
    if (!plugin->path) {
        mcp_log_error("Failed to duplicate plugin path string for %s", path);
        free_plugin(plugin);
        return NULL;
    }

#ifdef _WIN32
    plugin->library_handle = LoadLibraryA(path);
    if (!plugin->library_handle) {
        DWORD error_code = GetLastError();
        LPSTR message_buffer = get_windows_error_message(error_code);

        mcp_log_error("Failed to load library '%s'. Error %lu: %s",
                path, error_code, message_buffer ? message_buffer : "Unknown error");

        if (message_buffer) {
            LocalFree(message_buffer);
        }

        free_plugin(plugin);
        return NULL;
    }

    // Get the descriptor function pointer
    mcp_plugin_get_descriptor_func_t get_descriptor_func =
        (mcp_plugin_get_descriptor_func_t)GetProcAddress(plugin->library_handle, MCP_PLUGIN_DESCRIPTOR_FUNC);

    if (!get_descriptor_func) {
        mcp_log_error("Failed to find symbol '%s' in plugin '%s'. Error %lu",
                MCP_PLUGIN_DESCRIPTOR_FUNC, path, GetLastError());

        unload_and_free_plugin(plugin);
        return NULL;
    }
#else
    // RTLD_NOW: Resolve all symbols immediately
    // RTLD_LOCAL: Symbols are not available for subsequently loaded libraries
    plugin->library_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!plugin->library_handle) {
        mcp_log_error("Failed to load library '%s'. Error: %s",
                     path, dlerror());

        free_plugin(plugin);
        return NULL;
    }

    // Clear any existing error
    dlerror();

    // Get the descriptor function pointer
    // Note: Casting function pointer from void* is technically undefined behavior in C standard,
    // but required by dlsym and generally works on POSIX systems.
    mcp_plugin_get_descriptor_func_t get_descriptor_func = NULL;
    *(void**)(&get_descriptor_func) = dlsym(plugin->library_handle, MCP_PLUGIN_DESCRIPTOR_FUNC);

    const char* dlsym_error = dlerror();
    if (dlsym_error != NULL || !get_descriptor_func) {
        mcp_log_error("Failed to find symbol '%s' in plugin '%s'. Error: %s",
                MCP_PLUGIN_DESCRIPTOR_FUNC, path,
                dlsym_error ? dlsym_error : "Symbol not found");

        unload_and_free_plugin(plugin);
        return NULL;
    }
#endif

    // Call the function to get the descriptor
    plugin->descriptor = get_descriptor_func();
    if (!plugin->descriptor) {
        mcp_log_error("Plugin '%s' returned a NULL descriptor", path);
        unload_and_free_plugin(plugin);
        return NULL;
    }

    // Validate essential descriptor fields
    if (!plugin->descriptor->name ||
        !plugin->descriptor->version ||
        !plugin->descriptor->initialize ||
        !plugin->descriptor->finalize) {

        mcp_log_error("Plugin '%s' descriptor is missing required fields (name, version, initialize, finalize)",
                     path);

        unload_and_free_plugin(plugin);
        return NULL;
    }

    mcp_log_info("Plugin '%s' version '%s' descriptor loaded",
                plugin->descriptor->name, plugin->descriptor->version);

    // Call the plugin's initialize function
    if (plugin->descriptor->initialize(server_context) != 0) {
        mcp_log_error("Plugin '%s' initialization failed", plugin->descriptor->name);
        unload_and_free_plugin(plugin);
        return NULL;
    }

    mcp_log_info("Plugin '%s' initialized successfully", plugin->descriptor->name);
    return plugin;
}

/**
 * @brief Unloads a previously loaded plugin.
 *
 * @param plugin The plugin to unload
 * @return 0 on success, non-zero on failure
 */
int mcp_plugin_unload(mcp_plugin_t* plugin) {
    if (!plugin || !plugin->descriptor || !plugin->library_handle) {
        mcp_log_error("Invalid plugin handle provided");
        return -1;
    }

    const char* plugin_name = plugin->descriptor->name;
    const char* plugin_path = plugin->path;

    mcp_log_info("Unloading plugin '%s' from %s", plugin_name, plugin_path);

    // Call the plugin's finalize function
    int finalize_status = plugin->descriptor->finalize();
    if (finalize_status != 0) {
        // Log warning but proceed with unloading anyway
        mcp_log_warn("Plugin '%s' finalize function returned non-zero status (%d)",
                     plugin_name, finalize_status);
    }

    int unload_status = 0;
#ifdef _WIN32
    if (!FreeLibrary(plugin->library_handle)) {
        DWORD error_code = GetLastError();
        LPSTR message_buffer = get_windows_error_message(error_code);

        mcp_log_error("FreeLibrary failed for plugin '%s'. Error %lu: %s",
                     plugin_name, error_code,
                     message_buffer ? message_buffer : "Unknown error");

        if (message_buffer) {
            LocalFree(message_buffer);
        }

        unload_status = -1;
    }
#else
    if (dlclose(plugin->library_handle) != 0) {
        mcp_log_error("dlclose failed for plugin '%s'. Error: %s",
                     plugin_name, dlerror());
        unload_status = -1;
    }
#endif

    // Free the plugin structure itself
    free_plugin(plugin);

    if (unload_status == 0) {
        mcp_log_info("Plugin '%s' unloaded successfully", plugin_name);
    }

    // Return finalize status if unload was ok, otherwise return unload error status
    return (unload_status == 0) ? finalize_status : unload_status;
}

/**
 * @brief Retrieves the descriptor structure for a loaded plugin.
 *
 * @param plugin The plugin handle
 * @return Pointer to the plugin descriptor, or NULL if plugin is invalid
 */
const mcp_plugin_descriptor_t* mcp_plugin_get_descriptor(mcp_plugin_t* plugin) {
    if (!plugin) {
        return NULL;
    }

    return plugin->descriptor;
}
