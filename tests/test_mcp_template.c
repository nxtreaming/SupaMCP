#include "unity.h"
#include "mcp_template.h"
#include "mcp_template_optimized.h"
#include "mcp_json.h"
#include "mcp_json_utils.h"
#include "mcp_string_utils.h"
#include <string.h>
#include <stdlib.h>

// --- Test Cases ---

// Test template expansion with simple parameters
void test_template_expand_simple(void) {
    const char* template = "example://{name}/profile";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "name", mcp_json_string_create("john"));

    char* expanded = mcp_template_expand(template, params);
    TEST_ASSERT_NOT_NULL(expanded);
    TEST_ASSERT_EQUAL_STRING("example://john/profile", expanded);

    free(expanded);
    mcp_json_destroy(params);
}

// Test template expansion with multiple parameters
void test_template_expand_multiple_params(void) {
    const char* template = "example://{user}/posts/{post_id}";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "user", mcp_json_string_create("john"));
    mcp_json_object_set_property(params, "post_id", mcp_json_number_create(42));

    char* expanded = mcp_template_expand(template, params);
    TEST_ASSERT_NOT_NULL(expanded);
    TEST_ASSERT_EQUAL_STRING("example://john/posts/42", expanded);

    free(expanded);
    mcp_json_destroy(params);
}

// Test template expansion with optional parameters (included)
void test_template_expand_optional_included(void) {
    const char* template = "example://{user}/settings/{theme?}";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "user", mcp_json_string_create("john"));
    mcp_json_object_set_property(params, "theme", mcp_json_string_create("dark"));

    char* expanded = mcp_template_expand(template, params);
    TEST_ASSERT_NOT_NULL(expanded);
    TEST_ASSERT_EQUAL_STRING("example://john/settings/dark", expanded);

    free(expanded);
    mcp_json_destroy(params);
}

// Test template expansion with optional parameters (omitted)
void test_template_expand_optional_omitted(void) {
    const char* template = "example://{user}/settings/{theme?}";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "user", mcp_json_string_create("john"));

    char* expanded = mcp_template_expand(template, params);
    TEST_ASSERT_NOT_NULL(expanded);
    TEST_ASSERT_EQUAL_STRING("example://john/settings/", expanded);

    free(expanded);
    mcp_json_destroy(params);
}

// Test template expansion with default values
void test_template_expand_default_values(void) {
    const char* template = "example://{user}/settings/{theme=light}";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "user", mcp_json_string_create("john"));

    char* expanded = mcp_template_expand(template, params);
    TEST_ASSERT_NOT_NULL(expanded);
    TEST_ASSERT_EQUAL_STRING("example://john/settings/light", expanded);

    free(expanded);
    mcp_json_destroy(params);
}

// Test template expansion with typed parameters
void test_template_expand_typed_params(void) {
    const char* template = "example://{user}/posts/{post_id:int}";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "user", mcp_json_string_create("john"));
    mcp_json_object_set_property(params, "post_id", mcp_json_number_create(42));

    char* expanded = mcp_template_expand(template, params);
    TEST_ASSERT_NOT_NULL(expanded);
    TEST_ASSERT_EQUAL_STRING("example://john/posts/42", expanded);

    free(expanded);
    mcp_json_destroy(params);
}

// Test template expansion with pattern matching
void test_template_expand_pattern_matching(void) {
    const char* template = "example://{user}/settings/{theme:pattern:dark*}";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "user", mcp_json_string_create("john"));
    mcp_json_object_set_property(params, "theme", mcp_json_string_create("dark-mode"));

    char* expanded = mcp_template_expand(template, params);
    TEST_ASSERT_NOT_NULL(expanded);
    TEST_ASSERT_EQUAL_STRING("example://john/settings/dark-mode", expanded);

    free(expanded);
    mcp_json_destroy(params);
}

// Test template expansion with missing required parameter
void test_template_expand_missing_required(void) {
    const char* template = "example://{user}/profile";
    mcp_json_t* params = mcp_json_object_create();

    char* expanded = mcp_template_expand(template, params);
    TEST_ASSERT_NULL(expanded);

    mcp_json_destroy(params);
}

// Test template expansion with invalid parameter type
void test_template_expand_invalid_type(void) {
    const char* template = "example://{user}/posts/{post_id:int}";
    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "user", mcp_json_string_create("john"));
    mcp_json_object_set_property(params, "post_id", mcp_json_string_create("not-a-number"));

    char* expanded = mcp_template_expand(template, params);
    TEST_ASSERT_NULL(expanded);

    mcp_json_destroy(params);
}

// Test template matching with simple parameters
void test_template_matches_simple(void) {
    const char* template = "example://{name}/profile";
    const char* uri = "example://john/profile";

    int result = mcp_template_matches(uri, template);
    TEST_ASSERT_EQUAL(1, result);
}

// Test template matching with multiple parameters
void test_template_matches_multiple_params(void) {
    const char* template = "example://{user}/posts/{post_id}";
    const char* uri = "example://john/posts/42";

    int result = mcp_template_matches(uri, template);
    TEST_ASSERT_EQUAL(1, result);
}

// Test template matching with typed parameters
void test_template_matches_typed_params(void) {
    const char* template = "example://{user}/posts/{post_id:int}";
    const char* uri = "example://john/posts/42";

    int result = mcp_template_matches(uri, template);
    TEST_ASSERT_EQUAL(1, result);
}

// Test template matching with pattern matching
void test_template_matches_pattern_matching(void) {
    const char* template = "example://{user}/settings/{theme:pattern:dark*}";
    const char* uri = "example://john/settings/dark-mode";

    int result = mcp_template_matches(uri, template);
    TEST_ASSERT_EQUAL(1, result);
}

// Test template matching with non-matching URI
void test_template_matches_non_matching(void) {
    const char* template = "example://{user}/profile";
    const char* uri = "example://john/settings";

    int result = mcp_template_matches(uri, template);
    TEST_ASSERT_EQUAL(0, result);
}

// Test template matching with invalid parameter type
void test_template_matches_invalid_type(void) {
    const char* template = "example://{user}/posts/{post_id:int}";
    const char* uri = "example://john/posts/not-a-number";

    int result = mcp_template_matches(uri, template);
    TEST_ASSERT_EQUAL(0, result);
}

// Test parameter extraction with simple parameters
void test_template_extract_params_simple(void) {
    const char* template = "example://{name}/profile";
    const char* uri = "example://john/profile";

    mcp_json_t* params = mcp_template_extract_params(uri, template);
    TEST_ASSERT_NOT_NULL(params);

    mcp_json_t* name = mcp_json_object_get_property(params, "name");
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(mcp_json_is_string(name));
    TEST_ASSERT_EQUAL_STRING("john", mcp_json_string_value(name));

    mcp_json_destroy(params);
}

// Test parameter extraction with multiple parameters
void test_template_extract_params_multiple(void) {
    const char* template = "example://{user}/posts/{post_id}";
    const char* uri = "example://john/posts/42";

    mcp_json_t* params = mcp_template_extract_params(uri, template);
    TEST_ASSERT_NOT_NULL(params);

    mcp_json_t* user = mcp_json_object_get_property(params, "user");
    TEST_ASSERT_NOT_NULL(user);
    TEST_ASSERT_TRUE(mcp_json_is_string(user));
    TEST_ASSERT_EQUAL_STRING("john", mcp_json_string_value(user));

    mcp_json_t* post_id = mcp_json_object_get_property(params, "post_id");
    TEST_ASSERT_NOT_NULL(post_id);
    TEST_ASSERT_TRUE(mcp_json_is_string(post_id));
    TEST_ASSERT_EQUAL_STRING("42", mcp_json_string_value(post_id));

    mcp_json_destroy(params);
}

// Test parameter extraction with typed parameters
void test_template_extract_params_typed(void) {
    const char* template = "example://{user}/posts/{post_id:int}";
    const char* uri = "example://john/posts/42";

    mcp_json_t* params = mcp_template_extract_params(uri, template);
    TEST_ASSERT_NOT_NULL(params);

    mcp_json_t* user = mcp_json_object_get_property(params, "user");
    TEST_ASSERT_NOT_NULL(user);
    TEST_ASSERT_TRUE(mcp_json_is_string(user));
    TEST_ASSERT_EQUAL_STRING("john", mcp_json_string_value(user));

    mcp_json_t* post_id = mcp_json_object_get_property(params, "post_id");
    TEST_ASSERT_NOT_NULL(post_id);
    TEST_ASSERT_TRUE(mcp_json_is_number(post_id));
    TEST_ASSERT_EQUAL_INT(42, (int)mcp_json_number_value(post_id));

    mcp_json_destroy(params);
}

// Test parameter extraction with pattern matching
void test_template_extract_params_pattern(void) {
    const char* template = "example://{user}/settings/{theme:pattern:dark*}";
    const char* uri = "example://john/settings/dark-mode";

    mcp_json_t* params = mcp_template_extract_params(uri, template);
    TEST_ASSERT_NOT_NULL(params);

    mcp_json_t* user = mcp_json_object_get_property(params, "user");
    TEST_ASSERT_NOT_NULL(user);
    TEST_ASSERT_TRUE(mcp_json_is_string(user));
    TEST_ASSERT_EQUAL_STRING("john", mcp_json_string_value(user));

    mcp_json_t* theme = mcp_json_object_get_property(params, "theme");
    TEST_ASSERT_NOT_NULL(theme);
    TEST_ASSERT_TRUE(mcp_json_is_string(theme));
    TEST_ASSERT_EQUAL_STRING("dark-mode", mcp_json_string_value(theme));

    mcp_json_destroy(params);
}

// Test parameter extraction with non-matching URI
void test_template_extract_params_non_matching(void) {
    const char* template = "example://{user}/profile";
    const char* uri = "example://john/settings";

    mcp_json_t* params = mcp_template_extract_params(uri, template);
    TEST_ASSERT_NULL(params);
}

// Test optimized template matching
void test_template_matches_optimized(void) {
    const char* template = "example://{user}/posts/{post_id}";
    const char* uri = "example://john/posts/42";

    int result = mcp_template_matches_optimized(uri, template);
    TEST_ASSERT_EQUAL(1, result);
}

// Test optimized parameter extraction
void test_template_extract_params_optimized(void) {
    const char* template = "example://{user}/posts/{post_id:int}";
    const char* uri = "example://john/posts/42";

    mcp_json_t* params = mcp_template_extract_params_optimized(uri, template);
    TEST_ASSERT_NOT_NULL(params);

    mcp_json_t* user = mcp_json_object_get_property(params, "user");
    TEST_ASSERT_NOT_NULL(user);
    TEST_ASSERT_TRUE(mcp_json_is_string(user));
    TEST_ASSERT_EQUAL_STRING("john", mcp_json_string_value(user));

    mcp_json_t* post_id = mcp_json_object_get_property(params, "post_id");
    TEST_ASSERT_NOT_NULL(post_id);
    TEST_ASSERT_TRUE(mcp_json_is_number(post_id));
    TEST_ASSERT_EQUAL_INT(42, (int)mcp_json_number_value(post_id));

    mcp_json_destroy(params);
}

// Test template cache performance
void test_template_cache_performance(void) {
    const char* template = "example://{user}/posts/{post_id:int}/{comment_id:int}/{reply_id:int}";
    const char* uri = "example://john/posts/42/123/456";

    // First call should be slower (cache miss)
    mcp_json_t* params1 = mcp_template_extract_params_optimized(uri, template);
    TEST_ASSERT_NOT_NULL(params1);
    mcp_json_destroy(params1);

    // Second call should be faster (cache hit)
    mcp_json_t* params2 = mcp_template_extract_params_optimized(uri, template);
    TEST_ASSERT_NOT_NULL(params2);
    mcp_json_destroy(params2);

    // Clean up the cache
    mcp_template_cache_cleanup();
}

// Test template cache with multiple templates
void test_template_cache_multiple(void) {
    const char* template1 = "example://{user}/profile";
    const char* template2 = "example://{user}/posts/{post_id}";
    const char* uri1 = "example://john/profile";
    const char* uri2 = "example://john/posts/42";

    // Cache both templates
    int result1 = mcp_template_matches_optimized(uri1, template1);
    int result2 = mcp_template_matches_optimized(uri2, template2);

    TEST_ASSERT_EQUAL(1, result1);
    TEST_ASSERT_EQUAL(1, result2);

    // Clean up the cache
    mcp_template_cache_cleanup();
}

// --- Test Group Runner ---

void run_mcp_template_tests(void) {
    RUN_TEST(test_template_expand_simple);
    RUN_TEST(test_template_expand_multiple_params);
    RUN_TEST(test_template_expand_optional_included);
    RUN_TEST(test_template_expand_optional_omitted);
    RUN_TEST(test_template_expand_default_values);
    RUN_TEST(test_template_expand_typed_params);
    RUN_TEST(test_template_expand_pattern_matching);
    RUN_TEST(test_template_expand_missing_required);
    RUN_TEST(test_template_expand_invalid_type);

    RUN_TEST(test_template_matches_simple);
    RUN_TEST(test_template_matches_multiple_params);
    RUN_TEST(test_template_matches_typed_params);
    RUN_TEST(test_template_matches_pattern_matching);
    RUN_TEST(test_template_matches_non_matching);
    RUN_TEST(test_template_matches_invalid_type);

    RUN_TEST(test_template_extract_params_simple);
    RUN_TEST(test_template_extract_params_multiple);
    RUN_TEST(test_template_extract_params_typed);
    RUN_TEST(test_template_extract_params_pattern);
    RUN_TEST(test_template_extract_params_non_matching);

    RUN_TEST(test_template_matches_optimized);
    RUN_TEST(test_template_extract_params_optimized);
    RUN_TEST(test_template_cache_performance);
    RUN_TEST(test_template_cache_multiple);
}
