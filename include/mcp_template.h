#ifndef MCP_TEMPLATE_H
#define MCP_TEMPLATE_H

#include "mcp_json.h"

/**
 * @brief Expands a URI template by replacing placeholders with values.
 * 
 * This function supports simple {name} placeholders in URI templates.
 * It replaces each placeholder with the corresponding value from the params object.
 * 
 * @param template The URI template string (e.g., "example://{name}/resource")
 * @param params A JSON object containing parameter values (e.g., {"name": "test"})
 * @return A newly allocated string with the expanded URI, or NULL on error
 */
char* mcp_template_expand(const char* template, const mcp_json_t* params);

/**
 * @brief Checks if a URI matches a template pattern.
 * 
 * This function determines if a URI could have been generated from a template.
 * It doesn't extract parameter values, just checks if the pattern matches.
 * 
 * @param uri The URI to check
 * @param template The template pattern to match against
 * @return 1 if the URI matches the template pattern, 0 otherwise
 */
int mcp_template_matches(const char* uri, const char* template);

/**
 * @brief Extracts parameter values from a URI based on a template pattern.
 * 
 * This function extracts the values of parameters from a URI that matches a template.
 * It returns a JSON object containing the parameter values.
 * 
 * @param uri The URI to extract parameters from
 * @param template The template pattern to match against
 * @return A newly created JSON object containing parameter values, or NULL on error
 */
mcp_json_t* mcp_template_extract_params(const char* uri, const char* template);

#endif /* MCP_TEMPLATE_H */
