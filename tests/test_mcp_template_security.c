#include "unity.h"
#include "mcp_template_security.h"
#include "mcp_template.h"
#include "mcp_json.h"
#include "mcp_json_utils.h"
#include "mcp_server.h"
#include "internal/mcp_template_security.h"
#include <string.h>
#include <stdlib.h>

// Mock server for testing
static mcp_server_t* mock_server = NULL;

// Sample validator functions
static bool test_validator_always_true(
    const char* template_uri,
    const mcp_json_t* params,
    void* user_data
) {
    (void)template_uri; // Unused
    (void)params; // Unused
    (void)user_data; // Unused
    return true;
}

static bool test_validator_always_false(
    const char* template_uri,
    const mcp_json_t* params,
    void* user_data
) {
    (void)template_uri; // Unused
    (void)params; // Unused
    (void)user_data; // Unused
    return false;
}

static bool test_validator_check_param(
    const char* template_uri,
    const mcp_json_t* params,
    void* user_data
) {
    (void)template_uri; // Unused
    (void)user_data; // Unused

    // Check if the 'name' parameter exists and is not 'admin'
    mcp_json_t* name_param = mcp_json_object_get_property(params, "name");
    if (name_param == NULL || !mcp_json_is_string(name_param)) {
        return false;
    }

    const char* name = mcp_json_string_value(name_param);
    if (name == NULL || strcmp(name, "admin") == 0) {
        return false;
    }

    return true;
}

// --- Test Cases ---

// Test creating and destroying a template security context
void test_template_security_create_destroy(void) {
    mcp_template_security_t* security = mcp_template_security_create();
    TEST_ASSERT_NOT_NULL(security);

    mcp_template_security_destroy(security);
}

// Test adding an ACL entry
void test_template_security_add_acl(void) {
    mcp_template_security_t* security = mcp_template_security_create();
    TEST_ASSERT_NOT_NULL(security);

    const char* roles[] = {"user", "admin"};
    int result = mcp_template_security_add_acl(security, "example://{name}", roles, 2);
    TEST_ASSERT_EQUAL(0, result);

    mcp_template_security_destroy(security);
}

// Test setting a validator
void test_template_security_set_validator(void) {
    mcp_template_security_t* security = mcp_template_security_create();
    TEST_ASSERT_NOT_NULL(security);

    int result = mcp_template_security_set_validator(
        security,
        "example://{name}",
        test_validator_always_true,
        NULL
    );
    TEST_ASSERT_EQUAL(0, result);

    mcp_template_security_destroy(security);
}

// Test setting a default validator
void test_template_security_set_default_validator(void) {
    mcp_template_security_t* security = mcp_template_security_create();
    TEST_ASSERT_NOT_NULL(security);

    int result = mcp_template_security_set_default_validator(
        security,
        test_validator_always_true,
        NULL
    );
    TEST_ASSERT_EQUAL(0, result);

    mcp_template_security_destroy(security);
}

// Test checking access with a matching role
void test_template_security_check_access_matching_role(void) {
    mcp_template_security_t* security = mcp_template_security_create();
    TEST_ASSERT_NOT_NULL(security);

    const char* roles[] = {"user", "admin"};
    mcp_template_security_add_acl(security, "example://{name}", roles, 2);

    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "name", mcp_json_string_create("john"));

    bool result = mcp_template_security_check_access(
        security,
        "example://{name}",
        "user",
        params
    );
    TEST_ASSERT_TRUE(result);

    mcp_json_destroy(params);
    mcp_template_security_destroy(security);
}

// Test checking access with a non-matching role
void test_template_security_check_access_non_matching_role(void) {
    mcp_template_security_t* security = mcp_template_security_create();
    TEST_ASSERT_NOT_NULL(security);

    const char* roles[] = {"admin"};
    mcp_template_security_add_acl(security, "example://{name}", roles, 1);

    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "name", mcp_json_string_create("john"));

    bool result = mcp_template_security_check_access(
        security,
        "example://{name}",
        "user",
        params
    );
    TEST_ASSERT_FALSE(result);

    mcp_json_destroy(params);
    mcp_template_security_destroy(security);
}

// Test checking access with a validator that returns true
void test_template_security_check_access_validator_true(void) {
    mcp_template_security_t* security = mcp_template_security_create();
    TEST_ASSERT_NOT_NULL(security);

    const char* roles[] = {"user", "admin"};
    mcp_template_security_add_acl(security, "example://{name}", roles, 2);
    mcp_template_security_set_validator(
        security,
        "example://{name}",
        test_validator_always_true,
        NULL
    );

    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "name", mcp_json_string_create("john"));

    bool result = mcp_template_security_check_access(
        security,
        "example://{name}",
        "user",
        params
    );
    TEST_ASSERT_TRUE(result);

    mcp_json_destroy(params);
    mcp_template_security_destroy(security);
}

// Test checking access with a validator that returns false
void test_template_security_check_access_validator_false(void) {
    mcp_template_security_t* security = mcp_template_security_create();
    TEST_ASSERT_NOT_NULL(security);

    const char* roles[] = {"user", "admin"};
    mcp_template_security_add_acl(security, "example://{name}", roles, 2);
    mcp_template_security_set_validator(
        security,
        "example://{name}",
        test_validator_always_false,
        NULL
    );

    mcp_json_t* params = mcp_json_object_create();
    mcp_json_object_set_property(params, "name", mcp_json_string_create("john"));

    bool result = mcp_template_security_check_access(
        security,
        "example://{name}",
        "user",
        params
    );
    TEST_ASSERT_FALSE(result);

    mcp_json_destroy(params);
    mcp_template_security_destroy(security);
}

// Test validating parameters with a validator that checks parameter values
void test_template_security_validate_params(void) {
    mcp_template_security_t* security = mcp_template_security_create();
    TEST_ASSERT_NOT_NULL(security);

    mcp_template_security_set_validator(
        security,
        "example://{name}",
        test_validator_check_param,
        NULL
    );

    // Valid parameter
    mcp_json_t* params1 = mcp_json_object_create();
    mcp_json_object_set_property(params1, "name", mcp_json_string_create("john"));

    bool result1 = mcp_template_security_validate_params(
        security,
        "example://{name}",
        params1
    );
    TEST_ASSERT_TRUE(result1);

    // Invalid parameter (admin is not allowed)
    mcp_json_t* params2 = mcp_json_object_create();
    mcp_json_object_set_property(params2, "name", mcp_json_string_create("admin"));

    bool result2 = mcp_template_security_validate_params(
        security,
        "example://{name}",
        params2
    );
    TEST_ASSERT_FALSE(result2);

    mcp_json_destroy(params1);
    mcp_json_destroy(params2);
    mcp_template_security_destroy(security);
}

// Test server integration with template security
void test_server_template_security_integration(void) {
    // Create a mock server configuration
    mcp_server_config_t config = {0};
    config.name = "test-server";
    config.version = "1.0.0";
    config.description = "Test server for template security";

    // Set server capabilities
    mcp_server_capabilities_t capabilities = {0};
    capabilities.resources_supported = true;
    capabilities.tools_supported = false;

    // Create the server
    mock_server = mcp_server_create(&config, &capabilities);
    TEST_ASSERT_NOT_NULL(mock_server);

    // Add template ACL
    const char* roles[] = {"user", "admin"};
    int result1 = mcp_server_add_template_acl(
        mock_server,
        "example://{name}",
        roles,
        2
    );
    TEST_ASSERT_EQUAL(0, result1);

    // Set template validator
    int result2 = mcp_server_set_template_validator(
        mock_server,
        "example://{name}",
        test_validator_check_param,
        NULL
    );
    TEST_ASSERT_EQUAL(0, result2);

    // Set default template validator
    int result3 = mcp_server_set_default_template_validator(
        mock_server,
        test_validator_always_true,
        NULL
    );
    TEST_ASSERT_EQUAL(0, result3);

    // Clean up
    mcp_server_destroy(mock_server);
    mock_server = NULL;
}

// --- Test Group Runner ---

void run_mcp_template_security_tests(void) {
    RUN_TEST(test_template_security_create_destroy);
    RUN_TEST(test_template_security_add_acl);
    RUN_TEST(test_template_security_set_validator);
    RUN_TEST(test_template_security_set_default_validator);
    RUN_TEST(test_template_security_check_access_matching_role);
    RUN_TEST(test_template_security_check_access_non_matching_role);
    RUN_TEST(test_template_security_check_access_validator_true);
    RUN_TEST(test_template_security_check_access_validator_false);
    RUN_TEST(test_template_security_validate_params);
    RUN_TEST(test_server_template_security_integration);
}
