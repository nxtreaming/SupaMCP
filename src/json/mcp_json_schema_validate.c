#include "mcp_json.h"
#include "internal/mcp_json_schema_cache.h"
#include "mcp_log.h"

// Global schema cache
static mcp_json_schema_cache_t* g_schema_cache = NULL;

// Initialize the global schema cache
static void init_global_schema_cache(void) {
    if (g_schema_cache == NULL) {
        g_schema_cache = mcp_json_schema_cache_create(100); // Default capacity
        if (g_schema_cache == NULL) {
            mcp_log_error("Failed to create global schema cache");
        } else {
            mcp_log_info("Initialized global schema cache");
        }
    }
}

// Original schema validation function (placeholder)
int mcp_json_validate_schema(const char* json_str, const char* schema_str) {
    // Initialize the global schema cache if needed
    if (g_schema_cache == NULL) {
        init_global_schema_cache();
    }

    // If we have a cache, use it
    if (g_schema_cache != NULL) {
        return mcp_json_schema_validate(g_schema_cache, json_str, schema_str);
    }

    // Fallback to placeholder implementation
    mcp_log_warn("mcp_json_validate_schema: Using placeholder implementation (no cache available)");
    return 0; // Placeholder: Assume valid
}

// Cached schema validation function
int mcp_json_schema_validate_cached(mcp_json_schema_cache_t* cache, const char* json_str, const char* schema_str) {
    if (cache == NULL) {
        mcp_log_error("mcp_json_schema_validate_cached: NULL cache provided");
        return -1;
    }

    if (json_str == NULL || schema_str == NULL) {
        mcp_log_error("mcp_json_schema_validate_cached: NULL json_str or schema_str provided");
        return -1;
    }

    return mcp_json_schema_validate(cache, json_str, schema_str);
}
