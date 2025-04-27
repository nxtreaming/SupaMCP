/**
 * @file kmcp_registry.c
 * @brief Implementation of server registry integration
 */

#include "kmcp_registry.h"
#include "kmcp_http_client.h"
#include "kmcp_error.h"
#include "kmcp_server_manager.h"
#include "mcp_log.h"
#include "mcp_string_utils.h"
#include "mcp_hashtable.h"
#include "mcp_sync.h"
#include "mcp_json.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#ifdef _WIN32
// Windows compatibility for snprintf
#if _MSC_VER < 1900 // Visual Studio 2015 and earlier
#define snprintf _snprintf
#endif
#endif

// Default values
#define DEFAULT_CACHE_TTL_SECONDS 300  // 5 minutes
#define DEFAULT_CONNECT_TIMEOUT_MS 5000  // 5 seconds
#define DEFAULT_REQUEST_TIMEOUT_MS 30000  // 30 seconds
#define DEFAULT_MAX_RETRIES 3

/**
 * @brief Complete definition of registry structure
 */
struct kmcp_registry {
    char* registry_url;             // Registry URL
    char* api_key;                  // API key
    kmcp_http_client_t* http_client; // HTTP client
    mcp_hashtable_t* cache;         // Cache
    int cache_ttl_seconds;          // Cache TTL in seconds
    time_t last_refresh;            // Last refresh time
    mcp_mutex_t* mutex;             // Mutex
};

/**
 * @brief Cache entry structure
 */
typedef struct cache_entry {
    time_t timestamp;               // Timestamp when the entry was created
    char* data;                     // JSON data
} cache_entry_t;

// Forward declarations of internal functions
static void free_cache_entry(void* entry);
static kmcp_error_t parse_server_info(const char* json_str, kmcp_server_info_t** server_info);
static kmcp_error_t parse_server_info_array(const char* json_str, kmcp_server_info_t** servers, size_t* count);
static void free_string_array(char** array, size_t count);

/**
 * @brief Create a registry connection
 */
kmcp_registry_t* kmcp_registry_create(const char* registry_url) {
    // Create a default configuration
    kmcp_registry_config_t config;
    memset(&config, 0, sizeof(config));
    config.registry_url = registry_url;
    config.cache_ttl_seconds = DEFAULT_CACHE_TTL_SECONDS;
    config.connect_timeout_ms = DEFAULT_CONNECT_TIMEOUT_MS;
    config.request_timeout_ms = DEFAULT_REQUEST_TIMEOUT_MS;
    config.max_retries = DEFAULT_MAX_RETRIES;

    return kmcp_registry_create_with_config(&config);
}

/**
 * @brief Create a registry connection with custom configuration
 */
kmcp_registry_t* kmcp_registry_create_with_config(const kmcp_registry_config_t* config) {
    if (!config || !config->registry_url) {
        mcp_log_error("Invalid parameter: config or registry_url is NULL");
        return NULL;
    }

    // Allocate memory for registry
    kmcp_registry_t* registry = (kmcp_registry_t*)malloc(sizeof(kmcp_registry_t));
    if (!registry) {
        mcp_log_error("Failed to allocate memory for registry");
        return NULL;
    }

    // Initialize fields
    memset(registry, 0, sizeof(kmcp_registry_t));
    registry->registry_url = mcp_strdup(config->registry_url);
    registry->api_key = config->api_key ? mcp_strdup(config->api_key) : NULL;
    registry->cache_ttl_seconds = config->cache_ttl_seconds > 0 ?
                                config->cache_ttl_seconds : DEFAULT_CACHE_TTL_SECONDS;
    registry->last_refresh = 0;  // Never refreshed

    // Create mutex
    registry->mutex = mcp_mutex_create();
    if (!registry->mutex) {
        mcp_log_error("Failed to create mutex");
        free(registry->registry_url);
        if (registry->api_key) free(registry->api_key);
        free(registry);
        return NULL;
    }

    // Create cache
    registry->cache = mcp_hashtable_create(
        16,                             // initial_capacity
        0.75f,                          // load_factor_threshold
        mcp_hashtable_string_hash,      // hash_func
        mcp_hashtable_string_compare,   // key_compare
        mcp_hashtable_string_dup,       // key_dup
        mcp_hashtable_string_free,      // key_free
        free_cache_entry                // value_free
    );
    if (!registry->cache) {
        mcp_log_error("Failed to create cache");
        mcp_mutex_destroy(registry->mutex);
        free(registry->registry_url);
        if (registry->api_key) free(registry->api_key);
        free(registry);
        return NULL;
    }

    // Create HTTP client
    kmcp_http_client_config_t http_config;
    memset(&http_config, 0, sizeof(http_config));
    http_config.base_url = config->registry_url;
    http_config.api_key = config->api_key;
    http_config.connect_timeout_ms = config->connect_timeout_ms > 0 ?
                                  config->connect_timeout_ms : DEFAULT_CONNECT_TIMEOUT_MS;
    http_config.request_timeout_ms = config->request_timeout_ms > 0 ?
                                  config->request_timeout_ms : DEFAULT_REQUEST_TIMEOUT_MS;
    http_config.max_retries = config->max_retries >= 0 ?
                           config->max_retries : DEFAULT_MAX_RETRIES;

    registry->http_client = kmcp_http_client_create_with_config(&http_config);
    if (!registry->http_client) {
        mcp_log_error("Failed to create HTTP client");
        mcp_hashtable_destroy(registry->cache);
        mcp_mutex_destroy(registry->mutex);
        free(registry->registry_url);
        if (registry->api_key) free(registry->api_key);
        free(registry);
        return NULL;
    }

    mcp_log_info("Registry created: %s", registry->registry_url);
    return registry;
}

/**
 * @brief Close a registry connection
 */
void kmcp_registry_close(kmcp_registry_t* registry) {
    if (!registry) {
        return;
    }

    mcp_log_info("Closing registry: %s", registry->registry_url);

    // Close HTTP client
    if (registry->http_client) {
        kmcp_http_client_close(registry->http_client);
        registry->http_client = NULL;
    }

    // Destroy cache
    if (registry->cache) {
        mcp_hashtable_destroy(registry->cache);
        registry->cache = NULL;
    }

    // Destroy mutex
    if (registry->mutex) {
        mcp_mutex_destroy(registry->mutex);
        registry->mutex = NULL;
    }

    // Free strings
    if (registry->registry_url) {
        free(registry->registry_url);
        registry->registry_url = NULL;
    }

    if (registry->api_key) {
        free(registry->api_key);
        registry->api_key = NULL;
    }

    // Free registry
    free(registry);
}

/**
 * @brief Free a cache entry
 */
static void free_cache_entry(void* entry) {
    if (!entry) {
        return;
    }

    cache_entry_t* cache_entry = (cache_entry_t*)entry;

    if (cache_entry->data) {
        free(cache_entry->data);
        cache_entry->data = NULL;
    }

    free(cache_entry);
}

/**
 * @brief Refresh the registry cache
 */
kmcp_error_t kmcp_registry_refresh_cache(kmcp_registry_t* registry) {
    if (!registry) {
        mcp_log_error("Invalid parameter: registry is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    mcp_log_info("Refreshing registry cache: %s", registry->registry_url);

    // Lock mutex
    mcp_mutex_lock(registry->mutex);

    // Send request to registry
    char* response = NULL;
    int status = 0;
    kmcp_error_t result = kmcp_http_client_send(
        registry->http_client,
        "GET",
        "/servers",
        NULL,
        NULL,
        &response,
        &status
    );

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to get servers from registry: %s", kmcp_error_message(result));
        mcp_mutex_unlock(registry->mutex);
        return result;
    }

    if (status != 200) {
        mcp_log_error("Registry returned error status: %d", status);
        free(response);
        mcp_mutex_unlock(registry->mutex);
        return KMCP_ERROR_SERVER_ERROR;
    }

    if (!response) {
        mcp_log_error("Registry returned empty response");
        mcp_mutex_unlock(registry->mutex);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Clear existing cache
    mcp_hashtable_clear(registry->cache);

    // Create cache entry
    cache_entry_t* cache_entry = (cache_entry_t*)malloc(sizeof(cache_entry_t));
    if (!cache_entry) {
        mcp_log_error("Failed to allocate memory for cache entry");
        free(response);
        mcp_mutex_unlock(registry->mutex);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    cache_entry->timestamp = time(NULL);
    cache_entry->data = response;  // Transfer ownership of response

    // Add to cache
    int cache_result = mcp_hashtable_put(registry->cache, "servers", cache_entry);
    if (cache_result != 0) {
        mcp_log_error("Failed to add servers to cache");
        free(cache_entry->data);
        free(cache_entry);
        mcp_mutex_unlock(registry->mutex);
        return KMCP_ERROR_INTERNAL;
    }

    // Update last refresh time
    registry->last_refresh = time(NULL);

    // Unlock mutex
    mcp_mutex_unlock(registry->mutex);

    mcp_log_info("Registry cache refreshed");
    return KMCP_SUCCESS;
}

/**
 * @brief Get available servers from the registry
 */
kmcp_error_t kmcp_registry_get_servers(kmcp_registry_t* registry, kmcp_server_info_t** servers, size_t* count) {
    if (!registry || !servers || !count) {
        mcp_log_error("Invalid parameter: registry, servers, or count is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize output parameters
    *servers = NULL;
    *count = 0;

    // Lock mutex
    mcp_mutex_lock(registry->mutex);

    // Check if cache needs refresh
    time_t now = time(NULL);
    if (registry->last_refresh == 0 || (now - registry->last_refresh) > registry->cache_ttl_seconds) {
        // Cache is stale or doesn't exist, refresh it
        mcp_mutex_unlock(registry->mutex);
        kmcp_error_t refresh_result = kmcp_registry_refresh_cache(registry);
        if (refresh_result != KMCP_SUCCESS) {
            return refresh_result;
        }
        mcp_mutex_lock(registry->mutex);
    }

    // Get servers from cache
    void* value = NULL;
    int get_result = mcp_hashtable_get(registry->cache, "servers", &value);
    cache_entry_t* cache_entry = (cache_entry_t*)value;

    if (get_result != 0 || !cache_entry || !cache_entry->data) {
        mcp_log_error("No servers found in cache");
        mcp_mutex_unlock(registry->mutex);
        return KMCP_ERROR_NOT_FOUND;
    }

    // Parse server information
    kmcp_error_t result = parse_server_info_array(cache_entry->data, servers, count);
    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to parse server information: %s", kmcp_error_message(result));
        mcp_mutex_unlock(registry->mutex);
        return result;
    }

    // Unlock mutex
    mcp_mutex_unlock(registry->mutex);

    mcp_log_info("Got %zu servers from registry", *count);
    return KMCP_SUCCESS;
}

/**
 * @brief Search for servers in the registry
 */
kmcp_error_t kmcp_registry_search_servers(kmcp_registry_t* registry, const char* query, kmcp_server_info_t** servers, size_t* count) {
    if (!registry || !query || !servers || !count) {
        mcp_log_error("Invalid parameter: registry, query, servers, or count is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize output parameters
    *servers = NULL;
    *count = 0;

    // Lock mutex
    mcp_mutex_lock(registry->mutex);

    // Check if cache needs refresh
    time_t now = time(NULL);
    if (registry->last_refresh == 0 || (now - registry->last_refresh) > registry->cache_ttl_seconds) {
        // Cache is stale or doesn't exist, refresh it
        mcp_mutex_unlock(registry->mutex);
        kmcp_error_t refresh_result = kmcp_registry_refresh_cache(registry);
        if (refresh_result != KMCP_SUCCESS) {
            return refresh_result;
        }
        mcp_mutex_lock(registry->mutex);
    }

    // Prepare search URL
    char search_url[256];
    snprintf(search_url, sizeof(search_url), "/servers/search?q=%s", query);

    // Send search request to registry
    char* response = NULL;
    int status = 0;
    kmcp_error_t result = kmcp_http_client_send(
        registry->http_client,
        "GET",
        search_url,
        NULL,
        NULL,
        &response,
        &status
    );

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to search servers in registry: %s", kmcp_error_message(result));
        mcp_mutex_unlock(registry->mutex);
        return result;
    }

    if (status != 200) {
        mcp_log_error("Registry returned error status: %d", status);
        free(response);
        mcp_mutex_unlock(registry->mutex);
        return KMCP_ERROR_SERVER_ERROR;
    }

    if (!response) {
        mcp_log_error("Registry returned empty response");
        mcp_mutex_unlock(registry->mutex);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Parse server information
    result = parse_server_info_array(response, servers, count);
    free(response);  // Free response regardless of parse result

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to parse server information: %s", kmcp_error_message(result));
        mcp_mutex_unlock(registry->mutex);
        return result;
    }

    // Unlock mutex
    mcp_mutex_unlock(registry->mutex);

    mcp_log_info("Found %zu servers matching query '%s'", *count, query);
    return KMCP_SUCCESS;
}

/**
 * @brief Get server information from the registry
 */
kmcp_error_t kmcp_registry_get_server_info(kmcp_registry_t* registry, const char* server_id, kmcp_server_info_t** server_info) {
    if (!registry || !server_id || !server_info) {
        mcp_log_error("Invalid parameter: registry, server_id, or server_info is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize output parameter
    *server_info = NULL;

    // Lock mutex
    mcp_mutex_lock(registry->mutex);

    // Prepare server URL
    char server_url[256];
    snprintf(server_url, sizeof(server_url), "/servers/%s", server_id);

    // Send request to registry
    char* response = NULL;
    int status = 0;
    kmcp_error_t result = kmcp_http_client_send(
        registry->http_client,
        "GET",
        server_url,
        NULL,
        NULL,
        &response,
        &status
    );

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to get server info from registry: %s", kmcp_error_message(result));
        mcp_mutex_unlock(registry->mutex);
        return result;
    }

    if (status == 404) {
        mcp_log_error("Server not found: %s", server_id);
        free(response);
        mcp_mutex_unlock(registry->mutex);
        return KMCP_ERROR_SERVER_NOT_FOUND;
    }

    if (status != 200) {
        mcp_log_error("Registry returned error status: %d", status);
        free(response);
        mcp_mutex_unlock(registry->mutex);
        return KMCP_ERROR_SERVER_ERROR;
    }

    if (!response) {
        mcp_log_error("Registry returned empty response");
        mcp_mutex_unlock(registry->mutex);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Parse server information
    result = parse_server_info(response, server_info);
    free(response);  // Free response regardless of parse result

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to parse server information: %s", kmcp_error_message(result));
        mcp_mutex_unlock(registry->mutex);
        return result;
    }

    // Unlock mutex
    mcp_mutex_unlock(registry->mutex);

    mcp_log_info("Got server info for %s", server_id);
    return KMCP_SUCCESS;
}

/**
 * @brief Add a server from the registry to a server manager
 */
kmcp_error_t kmcp_registry_add_server(kmcp_registry_t* registry, kmcp_server_manager_t* manager, const char* server_id) {
    if (!registry || !manager || !server_id) {
        mcp_log_error("Invalid parameter: registry, manager, or server_id is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Get server information
    kmcp_server_info_t* server_info = NULL;
    kmcp_error_t result = kmcp_registry_get_server_info(registry, server_id, &server_info);
    if (result != KMCP_SUCCESS) {
        return result;
    }

    // Create server config
    kmcp_server_config_t config;
    memset(&config, 0, sizeof(config));
    config.name = server_info->name;
    config.url = server_info->url;
    config.is_http = true;

    // Add server to manager
    result = kmcp_server_add(manager, &config);

    // Free server information
    kmcp_registry_free_server_info(server_info);

    if (result != KMCP_SUCCESS) {
        mcp_log_error("Failed to add server to manager: %s", kmcp_error_message(result));
        return result;
    }

    mcp_log_info("Added server %s to manager", server_id);
    return KMCP_SUCCESS;
}

/**
 * @brief Add a server from the registry to a server manager by URL
 */
kmcp_error_t kmcp_registry_add_server_by_url(kmcp_registry_t* registry, kmcp_server_manager_t* manager, const char* url) {
    if (!registry || !manager || !url) {
        mcp_log_error("Invalid parameter: registry, manager, or url is NULL");
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Lock mutex
    mcp_mutex_lock(registry->mutex);

    // Get all servers
    kmcp_server_info_t* servers = NULL;
    size_t count = 0;
    kmcp_error_t result = kmcp_registry_get_servers(registry, &servers, &count);
    if (result != KMCP_SUCCESS) {
        mcp_mutex_unlock(registry->mutex);
        return result;
    }

    // Find server with matching URL
    kmcp_error_t add_result = KMCP_ERROR_SERVER_NOT_FOUND;
    for (size_t i = 0; i < count; i++) {
        if (servers[i].url && strcmp(servers[i].url, url) == 0) {
            // Create server config
            kmcp_server_config_t config;
            memset(&config, 0, sizeof(config));
            config.name = servers[i].name;
            config.url = servers[i].url;
            config.is_http = true;

            // Add server to manager
            add_result = kmcp_server_add(manager, &config);
            break;
        }
    }

    // Free server information
    kmcp_registry_free_server_info_array(servers, count);

    // Unlock mutex
    mcp_mutex_unlock(registry->mutex);

    if (add_result != KMCP_SUCCESS) {
        if (add_result == KMCP_ERROR_SERVER_NOT_FOUND) {
            mcp_log_error("Server with URL %s not found in registry", url);
        } else {
            mcp_log_error("Failed to add server to manager: %s", kmcp_error_message(add_result));
        }
        return add_result;
    }

    mcp_log_info("Added server with URL %s to manager", url);
    return KMCP_SUCCESS;
}

/**
 * @brief Free server information
 */
void kmcp_registry_free_server_info(kmcp_server_info_t* server_info) {
    if (!server_info) {
        return;
    }

    // Free strings
    if (server_info->id) free(server_info->id);
    if (server_info->name) free(server_info->name);
    if (server_info->url) free(server_info->url);
    if (server_info->description) free(server_info->description);
    if (server_info->version) free(server_info->version);

    // Free arrays
    free_string_array(server_info->capabilities, server_info->capabilities_count);
    free_string_array(server_info->tools, server_info->tools_count);
    free_string_array(server_info->resources, server_info->resources_count);

    // Free server info
    free(server_info);
}

/**
 * @brief Free an array of server information structures
 */
void kmcp_registry_free_server_info_array(kmcp_server_info_t* servers, size_t count) {
    if (!servers) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        // Free strings
        if (servers[i].id) free(servers[i].id);
        if (servers[i].name) free(servers[i].name);
        if (servers[i].url) free(servers[i].url);
        if (servers[i].description) free(servers[i].description);
        if (servers[i].version) free(servers[i].version);

        // Free arrays
        free_string_array(servers[i].capabilities, servers[i].capabilities_count);
        free_string_array(servers[i].tools, servers[i].tools_count);
        free_string_array(servers[i].resources, servers[i].resources_count);
    }

    // Free array
    free(servers);
}

/**
 * @brief Free a string array
 */
static void free_string_array(char** array, size_t count) {
    if (!array) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (array[i]) {
            free(array[i]);
            array[i] = NULL;
        }
    }

    free(array);
}

/**
 * @brief Parse server information from JSON
 */
static kmcp_error_t parse_server_info(const char* json_str, kmcp_server_info_t** server_info) {
    if (!json_str || !server_info) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Parse JSON
    mcp_json_t* json = mcp_json_parse(json_str);
    if (!json) {
        mcp_log_error("Failed to parse JSON");
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Allocate server info
    kmcp_server_info_t* info = (kmcp_server_info_t*)calloc(1, sizeof(kmcp_server_info_t));
    if (!info) {
        mcp_log_error("Failed to allocate memory for server info");
        mcp_json_destroy(json);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Get server properties
    mcp_json_t* id_json = mcp_json_object_get_property(json, "id");
    mcp_json_t* name_json = mcp_json_object_get_property(json, "name");
    mcp_json_t* url_json = mcp_json_object_get_property(json, "url");
    mcp_json_t* desc_json = mcp_json_object_get_property(json, "description");
    mcp_json_t* version_json = mcp_json_object_get_property(json, "version");
    mcp_json_t* public_json = mcp_json_object_get_property(json, "isPublic");
    mcp_json_t* last_seen_json = mcp_json_object_get_property(json, "lastSeen");

    // Get string properties
    if (id_json && mcp_json_get_type(id_json) == MCP_JSON_STRING) {
        const char* id_str = NULL;
        if (mcp_json_get_string(id_json, &id_str) == 0 && id_str) {
            info->id = mcp_strdup(id_str);
        }
    }

    if (name_json && mcp_json_get_type(name_json) == MCP_JSON_STRING) {
        const char* name_str = NULL;
        if (mcp_json_get_string(name_json, &name_str) == 0 && name_str) {
            info->name = mcp_strdup(name_str);
        }
    }

    if (url_json && mcp_json_get_type(url_json) == MCP_JSON_STRING) {
        const char* url_str = NULL;
        if (mcp_json_get_string(url_json, &url_str) == 0 && url_str) {
            info->url = mcp_strdup(url_str);
        }
    }

    if (desc_json && mcp_json_get_type(desc_json) == MCP_JSON_STRING) {
        const char* desc_str = NULL;
        if (mcp_json_get_string(desc_json, &desc_str) == 0 && desc_str) {
            info->description = mcp_strdup(desc_str);
        }
    }

    if (version_json && mcp_json_get_type(version_json) == MCP_JSON_STRING) {
        const char* version_str = NULL;
        if (mcp_json_get_string(version_json, &version_str) == 0 && version_str) {
            info->version = mcp_strdup(version_str);
        }
    }

    // Get boolean property
    if (public_json && mcp_json_get_type(public_json) == MCP_JSON_BOOLEAN) {
        mcp_json_get_boolean(public_json, &info->is_public);
    }

    // Get timestamp
    if (last_seen_json && mcp_json_get_type(last_seen_json) == MCP_JSON_NUMBER) {
        double timestamp = 0;
        if (mcp_json_get_number(last_seen_json, &timestamp) == 0) {
            info->last_seen = (time_t)timestamp;
        }
    }

    // Get array properties
    mcp_json_t* capabilities_json = mcp_json_object_get_property(json, "capabilities");
    mcp_json_t* tools_json = mcp_json_object_get_property(json, "tools");
    mcp_json_t* resources_json = mcp_json_object_get_property(json, "resources");

    // Parse capabilities array
    if (capabilities_json && mcp_json_get_type(capabilities_json) == MCP_JSON_ARRAY) {
        int array_size = mcp_json_array_get_size(capabilities_json);
        if (array_size > 0) {
            info->capabilities_count = (size_t)array_size;
            info->capabilities = (char**)calloc(info->capabilities_count, sizeof(char*));
            if (info->capabilities) {
                for (size_t i = 0; i < info->capabilities_count; i++) {
                    mcp_json_t* item = mcp_json_array_get_item(capabilities_json, (int)i);
                    if (item && mcp_json_get_type(item) == MCP_JSON_STRING) {
                        const char* str = NULL;
                        if (mcp_json_get_string(item, &str) == 0 && str) {
                            info->capabilities[i] = mcp_strdup(str);
                        }
                    }
                }
            }
        }
    }

    // Parse tools array
    if (tools_json && mcp_json_get_type(tools_json) == MCP_JSON_ARRAY) {
        int array_size = mcp_json_array_get_size(tools_json);
        if (array_size > 0) {
            info->tools_count = (size_t)array_size;
            info->tools = (char**)calloc(info->tools_count, sizeof(char*));
            if (info->tools) {
                for (size_t i = 0; i < info->tools_count; i++) {
                    mcp_json_t* item = mcp_json_array_get_item(tools_json, (int)i);
                    if (item && mcp_json_get_type(item) == MCP_JSON_STRING) {
                        const char* str = NULL;
                        if (mcp_json_get_string(item, &str) == 0 && str) {
                            info->tools[i] = mcp_strdup(str);
                        }
                    }
                }
            }
        }
    }

    // Parse resources array
    if (resources_json && mcp_json_get_type(resources_json) == MCP_JSON_ARRAY) {
        int array_size = mcp_json_array_get_size(resources_json);
        if (array_size > 0) {
            info->resources_count = (size_t)array_size;
            info->resources = (char**)calloc(info->resources_count, sizeof(char*));
            if (info->resources) {
                for (size_t i = 0; i < info->resources_count; i++) {
                    mcp_json_t* item = mcp_json_array_get_item(resources_json, (int)i);
                    if (item && mcp_json_get_type(item) == MCP_JSON_STRING) {
                        const char* str = NULL;
                        if (mcp_json_get_string(item, &str) == 0 && str) {
                            info->resources[i] = mcp_strdup(str);
                        }
                    }
                }
            }
        }
    }

    // Clean up
    mcp_json_destroy(json);

    // Set output parameter
    *server_info = info;

    return KMCP_SUCCESS;
}

/**
 * @brief Parse an array of server information from JSON
 */
static kmcp_error_t parse_server_info_array(const char* json_str, kmcp_server_info_t** servers, size_t* count) {
    if (!json_str || !servers || !count) {
        return KMCP_ERROR_INVALID_PARAMETER;
    }

    // Initialize output parameters
    *servers = NULL;
    *count = 0;

    // Parse JSON
    mcp_json_t* json = mcp_json_parse(json_str);
    if (!json) {
        mcp_log_error("Failed to parse JSON");
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Check if JSON is an array
    if (mcp_json_get_type(json) != MCP_JSON_ARRAY) {
        mcp_log_error("JSON is not an array");
        mcp_json_destroy(json);
        return KMCP_ERROR_PARSE_FAILED;
    }

    // Get array size
    int array_size = mcp_json_array_get_size(json);
    if (array_size <= 0) {
        // Empty array is valid
        mcp_json_destroy(json);
        return KMCP_SUCCESS;
    }

    // Allocate server info array
    kmcp_server_info_t* server_array = (kmcp_server_info_t*)calloc((size_t)array_size, sizeof(kmcp_server_info_t));
    if (!server_array) {
        mcp_log_error("Failed to allocate memory for server info array");
        mcp_json_destroy(json);
        return KMCP_ERROR_MEMORY_ALLOCATION;
    }

    // Parse each server
    size_t valid_count = 0;
    for (int i = 0; i < array_size; i++) {
        mcp_json_t* server_json = mcp_json_array_get_item(json, i);
        if (!server_json || mcp_json_get_type(server_json) != MCP_JSON_OBJECT) {
            continue;  // Skip invalid items
        }

        // Convert JSON object to string
        char* server_json_str = mcp_json_stringify(server_json);
        if (!server_json_str) {
            continue;  // Skip if can't stringify
        }

        // Parse server info
        kmcp_server_info_t* server_info = NULL;
        kmcp_error_t result = parse_server_info(server_json_str, &server_info);
        free(server_json_str);

        if (result != KMCP_SUCCESS || !server_info) {
            continue;  // Skip if parse failed
        }

        // Copy server info to array
        server_array[valid_count].id = server_info->id;
        server_array[valid_count].name = server_info->name;
        server_array[valid_count].url = server_info->url;
        server_array[valid_count].description = server_info->description;
        server_array[valid_count].version = server_info->version;
        server_array[valid_count].capabilities = server_info->capabilities;
        server_array[valid_count].capabilities_count = server_info->capabilities_count;
        server_array[valid_count].tools = server_info->tools;
        server_array[valid_count].tools_count = server_info->tools_count;
        server_array[valid_count].resources = server_info->resources;
        server_array[valid_count].resources_count = server_info->resources_count;
        server_array[valid_count].is_public = server_info->is_public;
        server_array[valid_count].last_seen = server_info->last_seen;

        // Clear pointers in original to avoid double free
        server_info->id = NULL;
        server_info->name = NULL;
        server_info->url = NULL;
        server_info->description = NULL;
        server_info->version = NULL;
        server_info->capabilities = NULL;
        server_info->tools = NULL;
        server_info->resources = NULL;

        // Free original
        free(server_info);

        valid_count++;
    }

    // Clean up
    mcp_json_destroy(json);

    // If no valid servers were found, free the array
    if (valid_count == 0) {
        free(server_array);
        return KMCP_SUCCESS;
    }

    // Resize array if needed
    if (valid_count < (size_t)array_size) {
        kmcp_server_info_t* resized_array = (kmcp_server_info_t*)realloc(server_array, valid_count * sizeof(kmcp_server_info_t));
        if (resized_array) {
            server_array = resized_array;
        }
    }

    // Set output parameters
    *servers = server_array;
    *count = valid_count;

    return KMCP_SUCCESS;
}