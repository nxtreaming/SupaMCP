#include "mcp_plugin.h"
#include "mcp_log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// Platform-specific includes for dynamic library loading
#ifdef _WIN32
#include <windows.h>
#else // Linux/macOS
#include <dlfcn.h>
#endif

/**
 * @internal
 * @brief Internal structure representing a loaded plugin instance.
 */
struct mcp_plugin {
#ifdef _WIN32
    HINSTANCE library_handle; // Handle from LoadLibrary
#else
    void* library_handle;     // Handle from dlopen
#endif
    const mcp_plugin_descriptor_t* descriptor; // Pointer to the descriptor obtained from the plugin
    char* path; // Store the path for potential debugging/logging
};

// Define the expected symbol name for the descriptor function in plugins
#define MCP_PLUGIN_DESCRIPTOR_FUNC_NAME "mcp_plugin_get_descriptor"

// Define the function pointer type for the descriptor function
typedef const mcp_plugin_descriptor_t* (*mcp_plugin_get_descriptor_func_t)(void);

// Helper for strdup if not available elsewhere
#ifndef mcp_strdup
static char* mcp_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* new_s = (char*)malloc(len);
    if (new_s) {
        memcpy(new_s, s, len);
    }
    return new_s;
}
#endif

/**
 * @brief Loads a plugin from a shared library file.
 */
mcp_plugin_t* mcp_plugin_load(const char* path, void* server_context) {
    if (!path) {
        mcp_log_error("mcp_plugin_load: Plugin path is NULL.");
        return NULL;
    }

    mcp_log_info("Attempting to load plugin from: %s", path);

    mcp_plugin_t* plugin = (mcp_plugin_t*)calloc(1, sizeof(mcp_plugin_t));
    if (!plugin) {
        mcp_log_error("mcp_plugin_load: Failed to allocate plugin structure for %s.", path);
        return NULL;
    }

    plugin->path = mcp_strdup(path); // Store path copy
    if (!plugin->path) {
         mcp_log_error("mcp_plugin_load: Failed to duplicate plugin path string for %s.", path);
         free(plugin);
         return NULL;
    }

    // --- Platform-specific library loading ---
#ifdef _WIN32
    plugin->library_handle = LoadLibraryA(path);
    if (!plugin->library_handle) {
        // Use GetLastError() for detailed error info on Windows
        DWORD error_code = GetLastError();
        LPSTR messageBuffer = NULL;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
        mcp_log_error("mcp_plugin_load: Failed to load library '%s'. Error %lu: %s",
                path, error_code, messageBuffer ? messageBuffer : "Unknown error");
        LocalFree(messageBuffer); // Free the error message buffer
        free(plugin->path);
        free(plugin);
        return NULL;
    }
    // Get the descriptor function pointer
    mcp_plugin_get_descriptor_func_t get_descriptor_func =
        (mcp_plugin_get_descriptor_func_t)GetProcAddress(plugin->library_handle, MCP_PLUGIN_DESCRIPTOR_FUNC_NAME);
    if (!get_descriptor_func) {
        mcp_log_error("mcp_plugin_load: Failed to find symbol '%s' in plugin '%s'. Error %lu",
                MCP_PLUGIN_DESCRIPTOR_FUNC_NAME, path, GetLastError());
        FreeLibrary(plugin->library_handle);
        free(plugin->path);
        free(plugin);
        return NULL;
    }
#else // Linux/macOS
    // RTLD_NOW: Resolve all symbols immediately. RTLD_LAZY: Resolve on demand.
    // RTLD_LOCAL: Symbols are not available for subsequently loaded libraries.
    // RTLD_GLOBAL: Symbols are available. Use LOCAL unless plugins need to share symbols.
    plugin->library_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!plugin->library_handle) {
        mcp_log_error("mcp_plugin_load: Failed to load library '%s'. Error: %s", path, dlerror());
        free(plugin->path);
        free(plugin);
        return NULL;
    }
    // Clear any existing error
    dlerror();
    // Get the descriptor function pointer
    // Note: Casting function pointer from void* is technically undefined behavior in C standard,
    // but required by dlsym and generally works on POSIX systems.
    *(void**)(&get_descriptor_func) = dlsym(plugin->library_handle, MCP_PLUGIN_DESCRIPTOR_FUNC_NAME);
    const char* dlsym_error = dlerror();
    if (dlsym_error != NULL || !get_descriptor_func) {
        mcp_log_error("mcp_plugin_load: Failed to find symbol '%s' in plugin '%s'. Error: %s",
                MCP_PLUGIN_DESCRIPTOR_FUNC_NAME, path, dlsym_error ? dlsym_error : "Symbol not found");
        dlclose(plugin->library_handle);
        free(plugin->path);
        free(plugin);
        return NULL;
    }
#endif
    // --- End platform-specific ---

    // Call the function to get the descriptor
    plugin->descriptor = get_descriptor_func();
    if (!plugin->descriptor) {
         mcp_log_error("mcp_plugin_load: Plugin '%s' returned a NULL descriptor.", path);
         #ifdef _WIN32
              FreeLibrary(plugin->library_handle);
         #else
             dlclose(plugin->library_handle);
         #endif
         free(plugin->path);
         free(plugin);
         return NULL;
    }

    // Validate essential descriptor fields
    if (!plugin->descriptor->name || !plugin->descriptor->version || !plugin->descriptor->initialize || !plugin->descriptor->finalize) {
         mcp_log_error("mcp_plugin_load: Plugin '%s' descriptor is missing required fields (name, version, initialize, finalize).", path);
         #ifdef _WIN32
              FreeLibrary(plugin->library_handle);
         #else
             dlclose(plugin->library_handle);
         #endif
         free(plugin->path);
         free(plugin);
         return NULL;
    }

    mcp_log_info("Plugin '%s' version '%s' descriptor loaded.", plugin->descriptor->name, plugin->descriptor->version);

    // Call the plugin's initialize function
    if (plugin->descriptor->initialize(server_context) != 0) {
        mcp_log_error("mcp_plugin_load: Plugin '%s' initialization failed.", plugin->descriptor->name);
        #ifdef _WIN32
             FreeLibrary(plugin->library_handle);
        #else
            dlclose(plugin->library_handle);
        #endif
        free(plugin->path);
        free(plugin);
         return NULL;
    }

    mcp_log_info("Plugin '%s' initialized successfully.", plugin->descriptor->name);
    return plugin;
}

/**
 * @brief Unloads a previously loaded plugin.
 */
int mcp_plugin_unload(mcp_plugin_t* plugin) {
    if (!plugin || !plugin->descriptor || !plugin->library_handle) {
        mcp_log_error("mcp_plugin_unload: Invalid plugin handle provided.");
        return -1;
    }

    mcp_log_info("Unloading plugin '%s' from %s", plugin->descriptor->name, plugin->path);

    // Call the plugin's finalize function
    int finalize_status = plugin->descriptor->finalize();
    if (finalize_status != 0) {
        // Log warning but proceed with unloading anyway
        mcp_log_warn("Plugin '%s' finalize function returned non-zero status (%d).",
                plugin->descriptor->name, finalize_status);
    }

    // --- Platform-specific library unloading ---
    int unload_status = 0;
#ifdef _WIN32
    if (!FreeLibrary(plugin->library_handle)) {
        // Log error
        mcp_log_error("mcp_plugin_unload: FreeLibrary failed for plugin '%s'. Error %lu",
                plugin->descriptor->name, GetLastError());
        unload_status = -1; // Indicate failure
    }
#else // Linux/macOS
    if (dlclose(plugin->library_handle) != 0) {
        // Log error
        mcp_log_error("mcp_plugin_unload: dlclose failed for plugin '%s'. Error: %s",
                plugin->descriptor->name, dlerror());
        unload_status = -1; // Indicate failure
    }
#endif
    // --- End platform-specific ---

    // Free the plugin structure itself
    free(plugin->path);
    free(plugin);

    if (unload_status == 0) {
         mcp_log_info("Plugin unloaded successfully.");
    }

    // Return finalize status if unload was ok, otherwise return unload error status
    return (unload_status == 0) ? finalize_status : unload_status;
}

/**
 * @brief Retrieves the descriptor structure for a loaded plugin.
 */
const mcp_plugin_descriptor_t* mcp_plugin_get_descriptor(mcp_plugin_t* plugin) {
    if (!plugin) {
        return NULL;
    }
    // The descriptor pointer was obtained during load
    return plugin->descriptor;
}
