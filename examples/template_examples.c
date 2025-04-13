#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mcp_template.h>
#include <mcp_template_optimized.h>
#include <mcp_json.h>
#include <mcp_json_utils.h>
#include <mcp_log.h>

// Example functions
void example_simple_template(void);
void example_optional_parameters(void);
void example_default_values(void);
void example_typed_parameters(void);
void example_pattern_matching(void);
void example_complex_template(void);
void example_template_matching(void);
void example_parameter_extraction(void);
void example_optimized_functions(void);

int main(int argc, char** argv) {
    // Suppress unused parameter warnings
    (void)argc;
    (void)argv;

    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_INFO);

    printf("MCP Template Examples\n");
    printf("====================\n\n");

    // Run examples
    example_simple_template();
    example_optional_parameters();
    example_default_values();
    example_typed_parameters();
    example_pattern_matching();
    example_complex_template();
    example_template_matching();
    example_parameter_extraction();
    example_optimized_functions();

    return 0;
}

// Example 1: Simple Template
void example_simple_template(void) {
    printf("Example 1: Simple Template\n");
    printf("--------------------------\n");

    const char* template_uri = "example://{name}/profile";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "name", mcp_json_string_create("john"));

    char* expanded = mcp_template_expand(template_uri, params);
    printf("Template: %s\n", template_uri);
    printf("Parameters: {\"name\": \"john\"}\n");
    printf("Expanded: %s\n\n", expanded);

    free(expanded);
    mcp_json_destroy(params);
}

// Example 2: Optional Parameters
void example_optional_parameters(void) {
    printf("Example 2: Optional Parameters\n");
    printf("-----------------------------\n");

    const char* template_uri = "example://{user}/settings/{theme?}";

    // With optional parameter
    mcp_json_t* params1 = mcp_json_object_create();
    mcp_json_object_set_property(params1, "user", mcp_json_string_create("john"));
    mcp_json_object_set_property(params1, "theme", mcp_json_string_create("dark"));

    char* expanded1 = mcp_template_expand(template_uri, params1);
    printf("Template: %s\n", template_uri);
    printf("Parameters: {\"user\": \"john\", \"theme\": \"dark\"}\n");
    printf("Expanded: %s\n\n", expanded1);

    // Without optional parameter
    mcp_json_t* params2 = mcp_json_object_create();
    mcp_json_object_set_property(params2, "user", mcp_json_string_create("john"));

    char* expanded2 = mcp_template_expand(template_uri, params2);
    printf("Template: %s\n", template_uri);
    printf("Parameters: {\"user\": \"john\"}\n");
    printf("Expanded: %s\n\n", expanded2);

    free(expanded1);
    free(expanded2);
    mcp_json_destroy(params1);
    mcp_json_destroy(params2);
}

// Example 3: Default Values
void example_default_values(void) {
    printf("Example 3: Default Values\n");
    printf("-----------------------\n");

    const char* template_uri = "example://{user}/settings/{theme=light}";

    // With parameter
    mcp_json_t* params1 = mcp_json_object_create();
    mcp_json_object_set_property(params1, "user", mcp_json_string_create("john"));
    mcp_json_object_set_property(params1, "theme", mcp_json_string_create("dark"));

    char* expanded1 = mcp_template_expand(template_uri, params1);
    printf("Template: %s\n", template_uri);
    printf("Parameters: {\"user\": \"john\", \"theme\": \"dark\"}\n");
    printf("Expanded: %s\n\n", expanded1);

    // With default value
    mcp_json_t* params2 = mcp_json_object_create();
    mcp_json_object_set_property(params2, "user", mcp_json_string_create("john"));

    char* expanded2 = mcp_template_expand(template_uri, params2);
    printf("Template: %s\n", template_uri);
    printf("Parameters: {\"user\": \"john\"}\n");
    printf("Expanded: %s\n\n", expanded2);

    free(expanded1);
    free(expanded2);
    mcp_json_destroy(params1);
    mcp_json_destroy(params2);
}

// Example 4: Typed Parameters
void example_typed_parameters(void) {
    printf("Example 4: Typed Parameters\n");
    printf("-------------------------\n");

    const char* template_uri = "example://{user}/posts/{post_id:int}";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "user", mcp_json_string_create("john"));
    mcp_json_object_set_property(params, "post_id", mcp_json_number_create(42));

    char* expanded = mcp_template_expand(template_uri, params);
    printf("Template: %s\n", template_uri);
    printf("Parameters: {\"user\": \"john\", \"post_id\": 42}\n");
    printf("Expanded: %s\n\n", expanded);

    free(expanded);
    mcp_json_destroy(params);
}

// Example 5: Pattern Matching
void example_pattern_matching(void) {
    printf("Example 5: Pattern Matching\n");
    printf("-------------------------\n");

    const char* template_uri = "example://{user}/settings/{theme:pattern:dark*}";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "user", mcp_json_string_create("john"));
    mcp_json_object_set_property(params, "theme", mcp_json_string_create("dark-mode"));

    char* expanded = mcp_template_expand(template_uri, params);
    printf("Template: %s\n", template_uri);
    printf("Parameters: {\"user\": \"john\", \"theme\": \"dark-mode\"}\n");
    printf("Expanded: %s\n\n", expanded);

    free(expanded);
    mcp_json_destroy(params);
}

// Example 6: Complex Template
void example_complex_template(void) {
    printf("Example 6: Complex Template\n");
    printf("--------------------------\n");

    const char* template_uri = "example://{user}/posts/{post_id:int}/comments/{comment_id:int}/{reply_id:int?}/{sort:pattern:date*}/{filter:pattern:all*}/{page:int=1}/{limit:int=10}";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "user", mcp_json_string_create("john"));
    mcp_json_object_set_property(params, "post_id", mcp_json_number_create(42));
    mcp_json_object_set_property(params, "comment_id", mcp_json_number_create(123));
    mcp_json_object_set_property(params, "reply_id", mcp_json_number_create(456));
    mcp_json_object_set_property(params, "sort", mcp_json_string_create("date-desc"));
    mcp_json_object_set_property(params, "filter", mcp_json_string_create("all-active"));
    mcp_json_object_set_property(params, "page", mcp_json_number_create(2));
    mcp_json_object_set_property(params, "limit", mcp_json_number_create(20));

    char* expanded = mcp_template_expand(template_uri, params);
    printf("Template: %s\n", template_uri);
    printf("Parameters: Complex JSON object with 8 parameters\n");
    printf("Expanded: %s\n\n", expanded);

    free(expanded);
    mcp_json_destroy(params);
}

// Example 7: Template Matching
void example_template_matching(void) {
    printf("Example 7: Template Matching\n");
    printf("--------------------------\n");

    const char* template_uri = "example://{user}/posts/{post_id:int}";
    const char* uri1 = "example://john/posts/42";
    const char* uri2 = "example://john/posts/abc";
    const char* uri3 = "example://john/comments/42";

    int matches1 = mcp_template_matches(uri1, template_uri);
    int matches2 = mcp_template_matches(uri2, template_uri);
    int matches3 = mcp_template_matches(uri3, template_uri);

    printf("Template: %s\n", template_uri);
    printf("URI 1: %s, Matches: %s\n", uri1, matches1 ? "Yes" : "No");
    printf("URI 2: %s, Matches: %s\n", uri2, matches2 ? "Yes" : "No");
    printf("URI 3: %s, Matches: %s\n\n", uri3, matches3 ? "Yes" : "No");
}

// Example 8: Parameter Extraction
void example_parameter_extraction(void) {
    printf("Example 8: Parameter Extraction\n");
    printf("-----------------------------\n");

    const char* template_uri = "example://{user}/posts/{post_id:int}";
    const char* uri = "example://john/posts/42";

    mcp_json_t* params = mcp_template_extract_params(uri, template_uri);

    printf("Template: %s\n", template_uri);
    printf("URI: %s\n", uri);
    printf("Extracted Parameters:\n");

    mcp_json_t* user = mcp_json_object_get_property(params, "user");
    mcp_json_t* post_id = mcp_json_object_get_property(params, "post_id");

    const char* user_str = mcp_json_is_string(user) ? mcp_json_string_value(user) : "<not a string>";
    double post_id_num = mcp_json_is_number(post_id) ? mcp_json_number_value(post_id) : 0;
    printf("  user: %s\n", user_str);
    printf("  post_id: %d\n\n", (int)post_id_num);

    mcp_json_destroy(params);
}

// Example 9: Optimized Functions
void example_optimized_functions(void) {
    printf("Example 9: Optimized Functions\n");
    printf("----------------------------\n");

    const char* template_uri = "example://{user}/posts/{post_id:int}";
    const char* uri = "example://john/posts/42";

    // First call (cache miss)
    printf("First call (cache miss):\n");
    mcp_json_t* params1 = mcp_template_extract_params_optimized(uri, template_uri);

    mcp_json_t* user1 = mcp_json_object_get_property(params1, "user");
    mcp_json_t* post_id1 = mcp_json_object_get_property(params1, "post_id");

    const char* user1_str = mcp_json_is_string(user1) ? mcp_json_string_value(user1) : "<not a string>";
    double post_id1_num = mcp_json_is_number(post_id1) ? mcp_json_number_value(post_id1) : 0;
    printf("  user: %s\n", user1_str);
    printf("  post_id: %d\n\n", (int)post_id1_num);

    // Second call (cache hit)
    printf("Second call (cache hit):\n");
    mcp_json_t* params2 = mcp_template_extract_params_optimized(uri, template_uri);

    mcp_json_t* user2 = mcp_json_object_get_property(params2, "user");
    mcp_json_t* post_id2 = mcp_json_object_get_property(params2, "post_id");

    const char* user2_str = mcp_json_is_string(user2) ? mcp_json_string_value(user2) : "<not a string>";
    double post_id2_num = mcp_json_is_number(post_id2) ? mcp_json_number_value(post_id2) : 0;
    printf("  user: %s\n", user2_str);
    printf("  post_id: %d\n\n", (int)post_id2_num);

    mcp_json_destroy(params1);
    mcp_json_destroy(params2);

    // Clean up the cache
    mcp_template_cache_cleanup();
}
