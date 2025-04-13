# MCP Template System Guide

## Introduction

The MCP Template System provides a powerful way to define, match, and extract parameters from URI templates. This guide explains how to use the template system effectively in your MCP applications.

## Template Syntax

Templates use a simple syntax with placeholders enclosed in curly braces:

```
example://{name}/profile
```

This template matches URIs like `example://john/profile` and extracts the parameter `name` with the value `john`.

### Parameter Types

The template system supports several parameter types:

- **Simple**: `{name}` - A required parameter with no type validation
- **Optional**: `{name?}` - An optional parameter that can be omitted
- **Default value**: `{name=default}` - A parameter with a default value if not provided
- **Typed**: `{name:type}` - A parameter with type validation (int, float, bool, string)
- **Pattern matching**: `{name:pattern:pattern*}` - A parameter that must match a pattern with * wildcards
- **Combined**: `{name:type=default}` - A parameter with both type validation and a default value
- **Combined**: `{name:type?}` - An optional parameter with type validation

### Supported Types

The following types are supported:

- `int`: Integer values (validated with strtol)
- `float`: Floating-point values (validated with strtof)
- `bool`: Boolean values ("true", "false", "1", "0")
- `string`: String values (default)
- `pattern:xyz*`: Pattern matching with * wildcard

## Template Functions

### Template Expansion

```c
char* mcp_template_expand(const char* template, const mcp_json_t* params);
```

This function expands a template by replacing placeholders with values from a JSON object. It returns a newly allocated string that must be freed by the caller.

Example:

```c
const char* template = "example://{name}/profile";
mcp_json_t* params = mcp_json_object_create();
mcp_json_object_set_property(params, "name", mcp_json_string_create("john"));

char* expanded = mcp_template_expand(template, params);
// expanded = "example://john/profile"

free(expanded);
mcp_json_destroy(params);
```

### Template Matching

```c
int mcp_template_matches(const char* uri, const char* template);
```

This function checks if a URI matches a template pattern. It returns 1 if the URI matches, 0 otherwise.

Example:

```c
const char* template = "example://{name}/profile";
const char* uri = "example://john/profile";

int matches = mcp_template_matches(uri, template);
// matches = 1
```

### Parameter Extraction

```c
mcp_json_t* mcp_template_extract_params(const char* uri, const char* template);
```

This function extracts parameters from a URI based on a template pattern. It returns a newly created JSON object containing the parameter values, or NULL on error.

Example:

```c
const char* template = "example://{name}/profile";
const char* uri = "example://john/profile";

mcp_json_t* params = mcp_template_extract_params(uri, template);
// params = {"name": "john"}

mcp_json_destroy(params);
```

### Optimized Functions

For better performance, the template system provides optimized versions of the matching and parameter extraction functions:

```c
int mcp_template_matches_optimized(const char* uri, const char* template);
mcp_json_t* mcp_template_extract_params_optimized(const char* uri, const char* template);
```

These functions use a template cache to avoid parsing the template on every call. They are significantly faster than the non-optimized versions and should be used in performance-critical code.

## Template Security

The template security system provides a way to control access to template-based resources:

### Adding Access Control Lists

```c
int mcp_server_add_template_acl(
    mcp_server_t* server,
    const char* template_uri,
    const char** allowed_roles,
    size_t allowed_roles_count
);
```

This function adds an access control list entry for a template URI pattern. Only users with the specified roles will be allowed to access the template.

Example:

```c
const char* roles[] = {"user", "admin"};
mcp_server_add_template_acl(server, "example://{name}", roles, 2);
```

### Setting Validators

```c
int mcp_server_set_template_validator(
    mcp_server_t* server,
    const char* template_uri,
    mcp_template_validator_t validator,
    void* validator_data
);
```

This function sets a custom validator for a template URI pattern. The validator will be called to validate parameters for the template.

Example:

```c
bool my_validator(const char* template_uri, const mcp_json_t* params, void* user_data) {
    // Check if the 'name' parameter exists and is valid
    mcp_json_t* name_param = mcp_json_object_get_property(params, "name");
    if (name_param == NULL || !mcp_json_is_string(name_param)) {
        return false;
    }
    
    const char* name = mcp_json_string_value(name_param);
    if (name == NULL || strlen(name) == 0) {
        return false;
    }
    
    return true;
}

mcp_server_set_template_validator(server, "example://{name}", my_validator, NULL);
```

## Server-Side Template Handling

### Registering Template Handlers

```c
int mcp_server_register_template_handler(
    mcp_server_t* server,
    const char* template_uri,
    mcp_resource_handler_t handler,
    void* user_data
);
```

This function registers a handler for a template URI pattern. The handler will be called when a URI matching the template is requested.

Example:

```c
mcp_error_code_t template_handler(
    mcp_server_t* server,
    const char* uri,
    void* user_data,
    mcp_content_item_t*** content,
    size_t* content_count,
    char** error_message
) {
    // Extract parameters from the URI
    const char* template_uri = (const char*)user_data;
    mcp_json_t* params = mcp_template_extract_params(uri, template_uri);
    
    // Process the request based on the parameters
    // ...
    
    mcp_json_destroy(params);
    return MCP_ERROR_NONE;
}

mcp_server_register_template_handler(server, "example://{name}", template_handler, "example://{name}");
```

### Adding Resource Templates

```c
int mcp_server_add_resource_template(
    mcp_server_t* server,
    const mcp_resource_template_t* template
);
```

This function adds a resource template to the server. Resource templates are used to describe the available template-based resources to clients.

Example:

```c
mcp_resource_template_t template = {0};
template.uri_template = "example://{name}";
template.name = "User";
template.description = "Access a user by name";
mcp_server_add_resource_template(server, &template);
```

## Client-Side Template Handling

### Expanding Templates

```c
int mcp_client_expand_template(
    mcp_client_t* client,
    const char* template_uri,
    const char* params_json,
    char** expanded_uri
);
```

This function expands a template on the client side. It takes a JSON string containing parameter values and returns the expanded URI.

Example:

```c
char* expanded_uri = NULL;
mcp_client_expand_template(client, "example://{name}", "{\"name\":\"john\"}", &expanded_uri);
// expanded_uri = "example://john"
free(expanded_uri);
```

### Reading Resources with Templates

```c
int mcp_client_read_resource_with_template(
    mcp_client_t* client,
    const char* template_uri,
    const char* params_json,
    mcp_content_item_t*** content,
    size_t* count
);
```

This function reads a resource using a template and parameters. It expands the template and then reads the resource at the expanded URI.

Example:

```c
mcp_content_item_t** content = NULL;
size_t count = 0;
mcp_client_read_resource_with_template(client, "example://{name}", "{\"name\":\"john\"}", &content, &count);
// Process content...
mcp_client_free_content_items(content, count);
```

## Best Practices

### Template Design

1. **Keep templates simple**: Use the simplest template that meets your needs.
2. **Use descriptive parameter names**: Choose parameter names that clearly indicate their purpose.
3. **Validate parameters**: Use type validation and custom validators to ensure parameter values are valid.
4. **Provide default values**: Use default values for optional parameters to make templates more robust.
5. **Use pattern matching sparingly**: Pattern matching is powerful but can be less efficient than simple parameters.

### Performance Optimization

1. **Use optimized functions**: Always use the optimized versions of template functions in performance-critical code.
2. **Cache templates**: The template system caches templates automatically, but you can pre-cache them by calling the optimized functions once during initialization.
3. **Limit template complexity**: Complex templates with many parameters or pattern matching can be slower to process.
4. **Reuse parameter objects**: When possible, reuse JSON parameter objects instead of creating new ones for each template expansion.

### Security Considerations

1. **Validate all parameters**: Always validate parameter values to prevent injection attacks.
2. **Use access control lists**: Restrict access to sensitive templates based on user roles.
3. **Implement custom validators**: Use custom validators to enforce business rules and security policies.
4. **Rate limit template-based resources**: Implement rate limiting to prevent abuse of template-based resources.

## Troubleshooting

### Common Issues

1. **Template doesn't match**: Check that the URI and template have the same structure and that parameter values are valid.
2. **Parameter extraction fails**: Ensure that the URI matches the template pattern and that parameter values are valid.
3. **Template expansion fails**: Check that all required parameters are provided and that parameter values are valid.
4. **Access denied**: Verify that the user has the required role to access the template and that parameter values pass validation.

### Debugging Tips

1. **Enable debug logging**: Set the log level to DEBUG to see detailed information about template processing.
2. **Check parameter values**: Print parameter values to ensure they are what you expect.
3. **Test templates individually**: Test each template separately to isolate issues.
4. **Use the non-optimized functions**: When debugging, use the non-optimized functions to see more detailed error messages.

## Examples

### Simple Template

```c
// Template: example://{name}
// URI: example://john
// Parameters: {"name": "john"}

// Server-side
mcp_server_register_template_handler(server, "example://{name}", handler, "example://{name}");

// Client-side
mcp_client_read_resource_with_template(client, "example://{name}", "{\"name\":\"john\"}", &content, &count);
```

### Complex Template

```c
// Template: example://{user}/posts/{post_id:int}/{comment_id:int?}
// URI: example://john/posts/42/123
// Parameters: {"user": "john", "post_id": 42, "comment_id": 123}

// Server-side
mcp_server_register_template_handler(server, "example://{user}/posts/{post_id:int}/{comment_id:int?}", handler, NULL);

// Client-side
mcp_client_read_resource_with_template(client, "example://{user}/posts/{post_id:int}/{comment_id:int?}", 
    "{\"user\":\"john\",\"post_id\":42,\"comment_id\":123}", &content, &count);
```

### Secure Template

```c
// Template: example://{name}
// URI: example://john
// Parameters: {"name": "john"}

// Server-side
const char* roles[] = {"user", "admin"};
mcp_server_add_template_acl(server, "example://{name}", roles, 2);
mcp_server_set_template_validator(server, "example://{name}", validator, NULL);
mcp_server_register_template_handler(server, "example://{name}", handler, NULL);

// Client-side
mcp_client_read_resource_with_template(client, "example://{name}", "{\"name\":\"john\"}", &content, &count);
```

## Conclusion

The MCP Template System provides a powerful and flexible way to define, match, and extract parameters from URI templates. By following the guidelines in this document, you can use templates effectively in your MCP applications.

For more information, see the API documentation for the template functions in `mcp_template.h`, `mcp_template_optimized.h`, and `mcp_template_security.h`.
