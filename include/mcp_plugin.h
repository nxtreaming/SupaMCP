#ifndef MCP_PLUGIN_H
#define MCP_PLUGIN_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle representing a loaded plugin instance.
 * The internal structure would likely hold the dynamic library handle (e.g., void* from dlopen/LoadLibrary)
 * and a pointer to the plugin's descriptor.
 */
typedef struct mcp_plugin mcp_plugin_t;

/**
 * @brief Describes the capabilities and entry points of an MCP plugin.
 *
 * Each plugin shared library must export a function (e.g., `mcp_plugin_get_descriptor`)
 * that returns a pointer to a statically defined instance of this structure.
 */
typedef struct {
    const char* name;        /**< Unique name of the plugin (e.g., "database_connector", "file_system_provider"). */
    const char* version;     /**< Version string of the plugin (e.g., "1.0.2"). */
    const char* author;      /**< Author or maintainer of the plugin. */
    const char* description; /**< Brief description of the plugin's purpose. */

    /**
     * @brief Plugin initialization function. Called once when the plugin is loaded.
     * @param server_context A pointer to the main server context or relevant data structure,
     *                       allowing the plugin to interact with the server core if needed.
     * @return 0 on successful initialization, non-zero on failure.
     */
    int (*initialize)(void* server_context);

    /**
     * @brief Plugin finalization function. Called once just before the plugin is unloaded.
     * Used for cleanup (e.g., closing connections, freeing resources).
     * @return 0 on successful finalization, non-zero on failure (though failure might be ignored during shutdown).
     */
    int (*finalize)(void);

    /**
     * @brief Handler for resource requests delegated to this plugin. (Optional)
     * If implemented, the server can route ReadResource requests matching certain URI schemes
     * or patterns to this plugin.
     * @param uri The resource URI being requested.
     * @param context Opaque context (e.g., client connection info, auth context).
     * @param[out] result Pointer to store the allocated result data (e.g., file content, API response).
     *                    The plugin is responsible for allocating this memory (e.g., using malloc).
     *                    The server core (or caller) is responsible for freeing it.
     * @param[out] result_size Pointer to store the size of the allocated result data.
     * @return 0 on success, non-zero if the plugin cannot handle the URI or an error occurs.
     */
    int (*handle_resource)(const char* uri, void* context, void** result, size_t* result_size);

    /**
     * @brief Handler for tool calls delegated to this plugin. (Optional)
     * If implemented, the server can route CallTool requests matching tool names provided
     * by this plugin.
     * @param name The name of the tool being called.
     * @param args The arguments for the tool call (e.g., a JSON string).
     * @param context Opaque context (e.g., client connection info, auth context).
     * @param[out] result Pointer to store the allocated result data (e.g., JSON string response).
     *                    The plugin is responsible for allocating this memory (e.g., using malloc).
     *                    The server core (or caller) is responsible for freeing it.
     * @param[out] result_size Pointer to store the size of the allocated result data.
     * @param[out] is_error Pointer to a boolean indicating if the result represents an error.
     * @return 0 on success (tool executed), non-zero if the plugin cannot handle the tool or an error occurs.
     */
    int (*handle_tool)(const char* name, const char* args, void* context, void** result, size_t* result_size, bool* is_error);

    // Add other potential hooks: e.g., register_tool, register_resource_provider, handle_event

} mcp_plugin_descriptor_t;

/**
 * @brief Loads a plugin from a shared library file.
 *
 * This function dynamically loads the shared library (e.g., .so, .dll) specified by `path`,
 * finds the exported `mcp_plugin_get_descriptor` function, retrieves the descriptor,
 * and calls the plugin's `initialize` function.
 *
 * @param path The file path to the plugin shared library.
 * @param server_context A pointer passed to the plugin's `initialize` function.
 * @return A handle to the loaded plugin instance, or NULL on failure (e.g., file not found,
 *         symbol not found, initialization failed).
 */
mcp_plugin_t* mcp_plugin_load(const char* path, void* server_context);

/**
 * @brief Unloads a previously loaded plugin.
 *
 * Calls the plugin's `finalize` function and then unloads the shared library,
 * freeing associated resources.
 *
 * @param plugin The plugin instance handle returned by `mcp_plugin_load`.
 * @return 0 on success, -1 on failure (e.g., invalid handle, finalization failed).
 */
int mcp_plugin_unload(mcp_plugin_t* plugin);

/**
 * @brief Retrieves the descriptor structure for a loaded plugin.
 *
 * @param plugin The plugin instance handle.
 * @return A pointer to the plugin's descriptor structure, or NULL if the handle is invalid.
 *         The returned pointer points to data within the loaded plugin's memory space; do not free it.
 */
const mcp_plugin_descriptor_t* mcp_plugin_get_descriptor(mcp_plugin_t* plugin);

// Optional: Functions to manage a collection of loaded plugins
// int mcp_plugin_manager_load_directory(const char* directory_path, void* server_context);
// void mcp_plugin_manager_unload_all(void);
// mcp_plugin_t* mcp_plugin_manager_find_by_name(const char* name);


#ifdef __cplusplus
}
#endif

#endif // MCP_PLUGIN_H
