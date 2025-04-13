#ifndef MCP_TEMPLATE_OPTIMIZED_H
#define MCP_TEMPLATE_OPTIMIZED_H

#include "mcp_template.h"
#include "mcp_json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Optimized template matching function
 * 
 * This function uses a cached template to match a URI against a template pattern.
 * It's much faster than the original implementation because it doesn't need to
 * parse the template every time.
 * 
 * @param uri The URI to match
 * @param template The template pattern to match against
 * @return 1 if the URI matches the template pattern, 0 otherwise
 */
int mcp_template_matches_optimized(const char* uri, const char* template_uri);

/**
 * @brief Optimized parameter extraction function
 * 
 * This function uses a cached template to extract parameters from a URI.
 * It's much faster than the original implementation because it doesn't need to
 * parse the template every time.
 * 
 * @param uri The URI to extract parameters from
 * @param template The template pattern to match against
 * @return A newly created JSON object containing parameter values, or NULL on error
 */
mcp_json_t* mcp_template_extract_params_optimized(const char* uri, const char* template_uri);

/**
 * @brief Clean up the template cache
 * 
 * This function should be called when the application is shutting down
 * to free all cached templates.
 */
void mcp_template_cache_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* MCP_TEMPLATE_OPTIMIZED_H */
