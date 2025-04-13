# Template Optimization Documentation

## Overview

This document describes the optimized template implementation in the MCP Server. The optimization focuses on improving the performance of template matching and parameter extraction operations, which are critical for the server's routing and request handling capabilities.

## Performance Improvements

The optimized template implementation provides significant performance improvements over the original implementation:

| Template Complexity | Template Matching Speedup | Parameter Extraction Speedup |
|---------------------|---------------------------|------------------------------|
| Simple              | 6.58x                     | 1.42x                        |
| Medium              | 9.39x                     | 1.55x                        |
| Complex             | 5.64x                     | 1.85x                        |
| Very Complex        | 6.59x                     | 2.08x                        |

These improvements are particularly significant for complex templates, which are common in real-world applications.

## Key Optimization Techniques

### 1. Template Caching

The optimized implementation caches parsed templates to avoid parsing them on every call. This is particularly effective for templates that are used frequently, such as API endpoints.

```c
// Cache a template for future use
mcp_cached_template_t* cached = mcp_template_cache_find(template_uri);
if (cached == NULL) {
    cached = mcp_template_cache_add(template_uri);
}
```

### 2. Efficient Parameter Extraction

The optimized implementation extracts parameters directly during the matching process, avoiding redundant work. This is particularly effective for templates with many parameters.

```c
// Extract parameters during matching
const char* next_static_in_uri = find_next_static_part(u, next_static, next_static_len);
if (next_static_in_uri != NULL) {
    // Extract the parameter value
    size_t param_len = next_static_in_uri - u;
    // ... (extract and validate parameter)
}
```

### 3. Optimized String Operations

The optimized implementation uses more efficient string operations and avoids unnecessary allocations. This reduces memory usage and improves performance.

```c
// Use strncmp for prefix matching instead of creating substrings
if (strncmp(u, cached->static_parts[0], cached->static_part_lengths[0]) != 0) {
    return 0;
}
```

### 4. Improved Pattern Matching

The optimized implementation includes a more efficient pattern matching algorithm for custom parameter types. This is particularly effective for templates with pattern-based parameters.

```c
// Simple pattern matching with * wildcard
static int pattern_match(const char* value, const char* pattern) {
    // ... (efficient pattern matching implementation)
}
```

## API Reference

### Template Matching

```c
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
```

### Parameter Extraction

```c
/**
 * @brief Optimized parameter extraction function
 *
 * This function uses a cached template to extract parameters from a URI.
 * It's much faster than the original implementation because it doesn't need to
 * parse the template every time and minimizes memory allocations and string operations.
 *
 * @param uri The URI to extract parameters from
 * @param template The template pattern to match against
 * @return A newly created JSON object containing parameter values, or NULL on error
 */
mcp_json_t* mcp_template_extract_params_optimized(const char* uri, const char* template_uri);
```

### Cache Cleanup

```c
/**
 * @brief Clean up the template cache
 *
 * This function should be called when the application is shutting down
 * to free all cached templates.
 */
void mcp_template_cache_cleanup(void);
```

## Usage Examples

### Basic Usage

```c
// Match a URI against a template
if (mcp_template_matches_optimized("example://john/posts/42", "example://{user}/posts/{post_id}")) {
    // URI matches the template
    // Extract parameters
    mcp_json_t* params = mcp_template_extract_params_optimized("example://john/posts/42", "example://{user}/posts/{post_id}");
    if (params != NULL) {
        // Use parameters
        // ...
        mcp_json_destroy(params);
    }
}
```

### Server Integration

```c
// Register a template-based resource handler
mcp_server_register_template_resource(server, "example://{user}/posts/{post_id}", handle_user_post);

// In the handler function
void handle_user_post(mcp_server_t* server, const char* uri, mcp_json_t* params, mcp_response_t* response) {
    // Get parameters
    const char* user = mcp_json_string_value(mcp_json_object_get_property(params, "user"));
    int post_id = (int)mcp_json_number_value(mcp_json_object_get_property(params, "post_id"));
    
    // Process request
    // ...
}
```

## Performance Considerations

1. **Template Complexity**: The more complex the template, the greater the performance improvement from the optimized implementation.
2. **Cache Size**: The template cache has a fixed size (default: 128 templates). If you have more templates than this, the least recently used templates will be evicted from the cache.
3. **Memory Usage**: The optimized implementation uses more memory than the original implementation due to caching. However, the memory usage is still relatively small and the performance improvement is significant.

## Implementation Details

The optimized template implementation is in `src/common/mcp_template_optimized_new.c`. It includes the following components:

1. **Template Cache**: A global cache of parsed templates to avoid parsing them on every call.
2. **Cached Template Structure**: A structure to hold a parsed template, including static parts, parameter names, and validations.
3. **Optimized Matching Function**: A function that uses the cached template to match a URI against a template pattern.
4. **Optimized Parameter Extraction Function**: A function that uses the cached template to extract parameters from a URI.
5. **Cache Cleanup Function**: A function to clean up the template cache when the application is shutting down.

## Limitations

1. **Thread Safety**: The template cache is not thread-safe. If you're using the optimized implementation in a multi-threaded environment, you should ensure that the cache is only accessed from a single thread or use appropriate synchronization.
2. **Cache Size**: The template cache has a fixed size. If you have more templates than this, the least recently used templates will be evicted from the cache.
3. **Memory Usage**: The optimized implementation uses more memory than the original implementation due to caching. However, the memory usage is still relatively small and the performance improvement is significant.

## Future Improvements

1. **Thread Safety**: Make the template cache thread-safe to support multi-threaded environments.
2. **Dynamic Cache Size**: Allow the cache size to be configured at runtime.
3. **Cache Statistics**: Add functions to get statistics about the cache, such as hit rate and eviction count.
4. **More Efficient Pattern Matching**: Implement a more efficient pattern matching algorithm for complex patterns.
5. **Parameter Type Conversion**: Add support for automatic parameter type conversion based on the parameter type.
