#include "unity.h"
#include "internal/server_internal.h"
#include "mcp_server.h"
#include "mcp_types.h"
#include "mcp_json.h"
#include "mcp_auth.h"
#include "mcp_arena.h"
#include <stdlib.h>
#include <string.h>

// Forward declarations
static mcp_server_t* create_mock_server(const char* api_key);

// --- Test Globals / Mocks ---
static mcp_server_t* mock_server = NULL;
static mcp_arena_t test_arena; // Arena for request parameters if needed by handlers

// Initialize test resources before each test
#ifdef _MSC_VER
#pragma comment(linker, "/alternatename:setUp=setUp_test_mcp_server_handlers")
#pragma comment(linker, "/alternatename:tearDown=tearDown_test_mcp_server_handlers")
#endif

void setUp_test_mcp_server_handlers(void) {
    // Initialize test arena
    mcp_arena_init(&test_arena, 4096); // 4KB initial size
    
    // Create mock server for each test
    mock_server = create_mock_server("test_key");
    TEST_ASSERT_NOT_NULL(mock_server);
}

// Clean up test resources after each test
void tearDown_test_mcp_server_handlers(void) {
    // Clean up mock server
    if (mock_server) {
        mcp_server_destroy(mock_server);
        mock_server = NULL;
    }
    
    // Clean up test arena
    mcp_arena_cleanup(&test_arena);
}

// --- Helper Functions ---
// Helper to create a basic mock server for testing
static mcp_server_t* create_mock_server(const char* api_key) {
    mcp_server_config_t cfg = {
        .name = "test-server",
        .version = "1.0",
        .description = "Mock server for testing",
        .api_key = api_key // Use provided key
    };
    mcp_server_capabilities_t caps = {
        .resources_supported = true,
        .tools_supported = true
    };
    // Note: We don't initialize transport, thread pool, cache etc. for handler tests
    mcp_server_t* server = mcp_server_create(&cfg, &caps);
    // Add some default resources/tools if needed for specific tests later
    return server;
}

// Helper to create a mock auth context
static mcp_auth_context_t* create_mock_auth_context(bool allow_all) {
     mcp_auth_context_t* context = (mcp_auth_context_t*)calloc(1, sizeof(mcp_auth_context_t));
     if (!context) return NULL;
     context->type = MCP_AUTH_API_KEY; // Assume API key for simplicity
     context->identifier = mcp_strdup("test_user");

     if (allow_all) {
         context->allowed_resources_count = 1;
         context->allowed_resources = (char**)malloc(sizeof(char*));
         context->allowed_resources[0] = mcp_strdup("*");

         context->allowed_tools_count = 1;
         context->allowed_tools = (char**)malloc(sizeof(char*));
         context->allowed_tools[0] = mcp_strdup("*");
     } else {
         // Example: Restricted permissions
         context->allowed_resources_count = 1;
         context->allowed_resources = (char**)malloc(sizeof(char*));
         context->allowed_resources[0] = mcp_strdup("example://hello");

         context->allowed_tools_count = 1;
         context->allowed_tools = (char**)malloc(sizeof(char*));
         context->allowed_tools[0] = mcp_strdup("echo");
     }
     // Basic check
     if (!context->identifier || !context->allowed_resources || !context->allowed_resources[0] ||
         !context->allowed_tools || !context->allowed_tools[0]) {
          mcp_auth_context_free(context);
          return NULL;
     }
     return context;
 }

// --- Test Cases ---

// Test case for server initialization
void test_server_init(void) {
    // Test with valid configuration
    mcp_server_config_t cfg = {
        .name = "test-server",
        .version = "1.0",
        .description = "Test Server",
        .api_key = "test_key"
    };
    mcp_server_capabilities_t caps = {
        .resources_supported = true,
        .tools_supported = true
    };
    
    mcp_server_t* server = mcp_server_create(&cfg, &caps);
    TEST_ASSERT_NOT_NULL(server);
    
    // Verify server configuration
    TEST_ASSERT_EQUAL_STRING("test-server", server->config.name);
    TEST_ASSERT_EQUAL_STRING("1.0", server->config.version);
    TEST_ASSERT_EQUAL_STRING("Test Server", server->config.description);
    TEST_ASSERT_EQUAL_STRING("test_key", server->config.api_key);
    
    // Verify capabilities
    TEST_ASSERT_TRUE(server->capabilities.resources_supported);
    TEST_ASSERT_TRUE(server->capabilities.tools_supported);
    
    mcp_server_destroy(server);
}

// Test case for server capabilities
void test_server_capabilities(void) {
    // Test with all capabilities enabled
    mcp_server_config_t cfg = {
        .name = "test-server",
        .version = "1.0",
        .description = "Test Server",
        .api_key = "test_key"
    };
    mcp_server_capabilities_t caps = {
        .resources_supported = true,
        .tools_supported = true
    };
    
    mcp_server_t* server = mcp_server_create(&cfg, &caps);
    TEST_ASSERT_NOT_NULL(server);
    
    // Verify capabilities
    TEST_ASSERT_TRUE(server->capabilities.resources_supported);
    TEST_ASSERT_TRUE(server->capabilities.tools_supported);
    
    // Test resource operations with resources enabled
    mcp_resource_t* r1 = mcp_resource_create("test://resource", "Test Resource", NULL, NULL);
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(server, r1));
    mcp_resource_free(r1);
    
    // Test tool operations with tools enabled
    mcp_tool_t* t1 = mcp_tool_create("test", "Test Tool");
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_tool(server, t1));
    mcp_tool_free(t1);
    
    mcp_server_destroy(server);
    
    // Test with capabilities disabled
    caps.resources_supported = false;
    caps.tools_supported = false;
    
    server = mcp_server_create(&cfg, &caps);
    TEST_ASSERT_NOT_NULL(server);
    
    // Verify capabilities are disabled
    TEST_ASSERT_FALSE(server->capabilities.resources_supported);
    TEST_ASSERT_FALSE(server->capabilities.tools_supported);
    
    // Test that resource operations fail when disabled
    r1 = mcp_resource_create("test://resource", "Test Resource", NULL, NULL);
    TEST_ASSERT_NOT_EQUAL(0, mcp_server_add_resource(server, r1));
    mcp_resource_free(r1);
    
    // Test that tool operations fail when disabled
    t1 = mcp_tool_create("test", "Test Tool");
    TEST_ASSERT_NOT_EQUAL(0, mcp_server_add_tool(server, t1));
    mcp_tool_free(t1);
    
    mcp_server_destroy(server);
}

// Test case for server configuration validation
void test_server_config_validation(void) {
    mcp_server_config_t cfg = {
        .name = "test-server",
        .version = "1.0",
        .description = "Test Server",
        .api_key = "test_key"
    };
    mcp_server_capabilities_t caps = {
        .resources_supported = true,
        .tools_supported = true
    };
    
    // Test with valid config
    mcp_server_t* server = mcp_server_create(&cfg, &caps);
    TEST_ASSERT_NOT_NULL(server);
    mcp_server_destroy(server);
    
    // Test with invalid version format
    cfg.version = "invalid";
    TEST_ASSERT_NULL(mcp_server_create(&cfg, &caps));
    
    // Test with empty description
    cfg.version = "1.0";
    cfg.description = "";
    TEST_ASSERT_NULL(mcp_server_create(&cfg, &caps));
    
    // Test with NULL API key
    cfg.description = "Test Server";
    cfg.api_key = NULL;
    TEST_ASSERT_NULL(mcp_server_create(&cfg, &caps));
    
    // Test with empty API key
    cfg.api_key = "";
    TEST_ASSERT_NULL(mcp_server_create(&cfg, &caps));
}

// Test case for server resource management
void test_server_resource_management(void) {
    mcp_server_config_t cfg = {
        .name = "test-server",
        .version = "1.0",
        .description = "Test Server",
        .api_key = "test_key"
    };
    mcp_server_capabilities_t caps = {
        .resources_supported = true,
        .tools_supported = true
    };
    
    mcp_server_t* server = mcp_server_create(&cfg, &caps);
    TEST_ASSERT_NOT_NULL(server);
    
    // Test adding resources
    mcp_resource_t* r1 = mcp_resource_create("test://resource1", "Resource 1", "text/plain", "Description 1");
    mcp_resource_t* r2 = mcp_resource_create("test://resource2", "Resource 2", NULL, NULL);
    TEST_ASSERT_NOT_NULL(r1);
    TEST_ASSERT_NOT_NULL(r2);
    
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(server, r1));
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(server, r2));
    
    // Test finding resources using hashtable API
    void* found_ptr = NULL;
    TEST_ASSERT_EQUAL_INT(0, mcp_hashtable_get(server->resources_table, "test://resource1", &found_ptr));
    const mcp_resource_t* found = (const mcp_resource_t*)found_ptr;
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Resource 1", found->name);
    TEST_ASSERT_EQUAL_STRING("text/plain", found->mime_type);
    TEST_ASSERT_EQUAL_STRING("Description 1", found->description);
    
    found_ptr = NULL;
    TEST_ASSERT_EQUAL_INT(0, mcp_hashtable_get(server->resources_table, "test://resource2", &found_ptr));
    found = (const mcp_resource_t*)found_ptr;
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Resource 2", found->name);
    TEST_ASSERT_NULL(found->mime_type);
    TEST_ASSERT_NULL(found->description);
    
    // Test duplicate resource
    // Test duplicate resource (mcp_hashtable_put should handle update, return 0)
    // We need to create a new resource object to put, as the old one was freed by add_resource
    mcp_resource_t* r1_dup = mcp_resource_create("test://resource1", "Resource 1 Dup", NULL, NULL);
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(server, r1_dup)); // Should update existing
    mcp_resource_free(r1_dup); // Free the temporary duplicate
    // Verify update (optional)
    found_ptr = NULL;
    TEST_ASSERT_EQUAL_INT(0, mcp_hashtable_get(server->resources_table, "test://resource1", &found_ptr));
    found = (const mcp_resource_t*)found_ptr;
    TEST_ASSERT_EQUAL_STRING("Resource 1 Dup", found->name);


    // Test removing resources using hashtable API
    TEST_ASSERT_EQUAL_INT(0, mcp_hashtable_remove(server->resources_table, "test://resource1"));
    found_ptr = NULL;
    TEST_ASSERT_NOT_EQUAL(0, mcp_hashtable_get(server->resources_table, "test://resource1", &found_ptr)); // Should not be found
    TEST_ASSERT_NULL(found_ptr);

    // Test removing nonexistent resource
    TEST_ASSERT_NOT_EQUAL(0, mcp_hashtable_remove(server->resources_table, "test://nonexistent"));
    
    mcp_resource_free(r1);
    mcp_resource_free(r2);
    mcp_server_destroy(server);
}

// Test case for server tool management
void test_server_tool_management(void) {
    mcp_server_config_t cfg = {
        .name = "test-server",
        .version = "1.0",
        .description = "Test Server",
        .api_key = "test_key"
    };
    mcp_server_capabilities_t caps = {
        .resources_supported = true,
        .tools_supported = true
    };
    
    mcp_server_t* server = mcp_server_create(&cfg, &caps);
    TEST_ASSERT_NOT_NULL(server);
    
    // Test adding tools
    mcp_tool_t* t1 = mcp_tool_create("echo", "Echo Tool");
    mcp_tool_add_param(t1, "text", "string", "Text to echo", true);
    
    mcp_tool_t* t2 = mcp_tool_create("reverse", "Reverse Tool");
    mcp_tool_add_param(t2, "text", "string", "Text to reverse", true);
    mcp_tool_add_param(t2, "uppercase", "boolean", "Convert to uppercase", false);
    
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_tool(server, t1));
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_tool(server, t2));
    
    // Test finding tools using hashtable API
    void* found_ptr = NULL;
    TEST_ASSERT_EQUAL_INT(0, mcp_hashtable_get(server->tools_table, "echo", &found_ptr));
    const mcp_tool_t* found = (const mcp_tool_t*)found_ptr;
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Echo Tool", found->description);
    // Verify tool has one parameter
    TEST_ASSERT_NOT_NULL(found->input_schema);
    TEST_ASSERT_EQUAL_UINT(1, found->input_schema_count);
    
    found_ptr = NULL;
    TEST_ASSERT_EQUAL_INT(0, mcp_hashtable_get(server->tools_table, "reverse", &found_ptr));
    found = (const mcp_tool_t*)found_ptr;
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Reverse Tool", found->description);
    // Verify tool has two parameters
    TEST_ASSERT_NOT_NULL(found->input_schema);
    TEST_ASSERT_EQUAL_UINT(2, found->input_schema_count);
    
    // Test duplicate tool (mcp_hashtable_put should handle update, return 0)
    mcp_tool_t* t1_dup = mcp_tool_create("echo", "Echo Tool Updated");
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_tool(server, t1_dup)); // Should update
    mcp_tool_free(t1_dup);
    // Verify update (optional)
    found_ptr = NULL;
    TEST_ASSERT_EQUAL_INT(0, mcp_hashtable_get(server->tools_table, "echo", &found_ptr));
    found = (const mcp_tool_t*)found_ptr;
    TEST_ASSERT_EQUAL_STRING("Echo Tool Updated", found->description);

    // Test removing tools using hashtable API
    TEST_ASSERT_EQUAL_INT(0, mcp_hashtable_remove(server->tools_table, "echo"));
    found_ptr = NULL;
    TEST_ASSERT_NOT_EQUAL(0, mcp_hashtable_get(server->tools_table, "echo", &found_ptr)); // Should not be found
    TEST_ASSERT_NULL(found_ptr);

    // Test removing nonexistent tool
    TEST_ASSERT_NOT_EQUAL(0, mcp_hashtable_remove(server->tools_table, "nonexistent"));
    
    mcp_tool_free(t1);
    mcp_tool_free(t2);
    mcp_server_destroy(server);
}

// Test case for server initialization with invalid config
void test_server_init_invalid_config(void) {
    // Test with NULL config
    TEST_ASSERT_NULL(mcp_server_create(NULL, NULL));
    
    // Test with missing required fields
    mcp_server_config_t cfg = {
        .name = NULL, // Required field
        .version = "1.0",
        .description = "Test Server",
        .api_key = "test_key"
    };
    mcp_server_capabilities_t caps = {
        .resources_supported = true,
        .tools_supported = true
    };
    
    TEST_ASSERT_NULL(mcp_server_create(&cfg, &caps));
    
    // Test with empty name
    cfg.name = "";
    TEST_ASSERT_NULL(mcp_server_create(&cfg, &caps));
}

// Test case for handle_ping_request
void test_handle_ping_request_success(void) {
    mcp_request_t request = { .id = 1, .method = "ping", .params = "{}" };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true); // Full permissions
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_ping_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and check content
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* result_node = mcp_json_object_get_property(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_OBJECT, mcp_json_get_type(result_node));
    mcp_json_t* msg_node = mcp_json_object_get_property(result_node, "message");
    TEST_ASSERT_NOT_NULL(msg_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_STRING, mcp_json_get_type(msg_node));
    const char* msg_val = NULL;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_string(msg_node, &msg_val));
    TEST_ASSERT_EQUAL_STRING("pong", msg_val);

    mcp_json_destroy(resp_json); // Frees TLS arena nodes
    free(response_str); // Free the malloc'd string
    mcp_auth_context_free(auth_context);
}

// Test case for handle_list_resources_request with no resources
void test_handle_list_resources_empty(void) {
    mcp_request_t request = { .id = 2, .method = "list_resources", .params = "{}" };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_list_resources_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and check content
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* result_node = mcp_json_object_get_property(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    mcp_json_t* resources_node = mcp_json_object_get_property(result_node, "resources");
    TEST_ASSERT_NOT_NULL(resources_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_ARRAY, mcp_json_get_type(resources_node));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_get_size(resources_node)); // Expect empty array

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_read_resource_request with missing required fields
void test_handle_read_resource_missing_fields(void) {
    mcp_request_t request = { 
        .id = 17, 
        .method = "read_resource",
        .params = "{}" // Missing required 'uri' field
    };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_read_resource_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_INVALID_PARAMS, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and verify error
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* error_node = mcp_json_object_get_property(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error_node);
    mcp_json_t* code_node = mcp_json_object_get_property(error_node, "code");
    TEST_ASSERT_NOT_NULL(code_node);
    double error_code_val = 0;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(code_node, &error_code_val));
    TEST_ASSERT_EQUAL_INT(MCP_ERROR_INVALID_PARAMS, (int)error_code_val);

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_read_resource_request with invalid JSON parameters
void test_handle_read_resource_invalid_json(void) {
    mcp_request_t request = { 
        .id = 16, 
        .method = "read_resource",
        .params = "{invalid json" // Malformed JSON
    };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_read_resource_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_INVALID_PARAMS, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and verify error
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* error_node = mcp_json_object_get_property(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error_node);
    mcp_json_t* code_node = mcp_json_object_get_property(error_node, "code");
    TEST_ASSERT_NOT_NULL(code_node);
    double error_code_val = 0;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(code_node, &error_code_val));
    TEST_ASSERT_EQUAL_INT(MCP_ERROR_INVALID_PARAMS, (int)error_code_val);

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for invalid method name
void test_handle_invalid_method(void) {
    mcp_request_t request = { 
        .id = 15, 
        .method = "nonexistent_method",
        .params = "{}"
    };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_METHOD_NOT_FOUND, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and verify error
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* error_node = mcp_json_object_get_property(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error_node);
    mcp_json_t* code_node = mcp_json_object_get_property(error_node, "code");
    TEST_ASSERT_NOT_NULL(code_node);
    double error_code_val = 0;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(code_node, &error_code_val));
    TEST_ASSERT_EQUAL_INT(MCP_ERROR_METHOD_NOT_FOUND, (int)error_code_val);

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_call_tool_request with nonexistent tool
void test_handle_call_tool_not_found(void) {
    // Create request with nonexistent tool name
    mcp_request_t request = { 
        .id = 14, 
        .method = "call_tool",
        .params = "{\"name\":\"nonexistent\",\"arguments\":{}}"
    };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_call_tool_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_TOOL_NOT_FOUND, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and verify error
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* error_node = mcp_json_object_get_property(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error_node);
    mcp_json_t* code_node = mcp_json_object_get_property(error_node, "code");
    TEST_ASSERT_NOT_NULL(code_node);
    double error_code_val = 0;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(code_node, &error_code_val));
    TEST_ASSERT_EQUAL_INT(MCP_ERROR_TOOL_NOT_FOUND, (int)error_code_val);

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_call_tool_request with invalid JSON parameters
void test_handle_call_tool_invalid_json(void) {
    // Add a test tool
    mcp_tool_t* t1 = mcp_tool_create("echo", "Echo Tool");
    mcp_tool_add_param(t1, "text", "string", "Text to echo", true);
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_tool(mock_server, t1));
    mcp_tool_free(t1);

    // Create request with invalid JSON in params
    mcp_request_t request = { 
        .id = 13, 
        .method = "call_tool",
        .params = "{invalid json" // Malformed JSON
    };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_call_tool_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_INVALID_PARAMS, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and verify error
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* error_node = mcp_json_object_get_property(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error_node);
    mcp_json_t* code_node = mcp_json_object_get_property(error_node, "code");
    TEST_ASSERT_NOT_NULL(code_node);
    double error_code_val = 0;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(code_node, &error_code_val));
    TEST_ASSERT_EQUAL_INT(MCP_ERROR_INVALID_PARAMS, (int)error_code_val);

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_call_tool_request with invalid parameters
void test_handle_call_tool_invalid_params(void) {
    // Add a test tool that requires parameters
    mcp_tool_t* t1 = mcp_tool_create("echo", "Echo Tool");
    mcp_tool_add_param(t1, "text", "string", "Text to echo", true); // Required parameter
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_tool(mock_server, t1));
    mcp_tool_free(t1);

    // Create request with missing required parameter
    mcp_request_t request = { 
        .id = 12, 
        .method = "call_tool",
        .params = "{\"name\":\"echo\",\"arguments\":{}}"
    };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_call_tool_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_INVALID_PARAMS, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and verify error
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* error_node = mcp_json_object_get_property(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error_node);
    mcp_json_t* code_node = mcp_json_object_get_property(error_node, "code");
    TEST_ASSERT_NOT_NULL(code_node);
    double error_code_val = 0;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(code_node, &error_code_val));
    TEST_ASSERT_EQUAL_INT(MCP_ERROR_INVALID_PARAMS, (int)error_code_val);

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_list_resources_request with resources
void test_handle_list_resources_with_data(void) {
    // Add some resources to the mock server
    mcp_resource_t* r1 = mcp_resource_create("res://one", "Resource One", "text/plain", "Desc 1");
    mcp_resource_t* r2 = mcp_resource_create("res://two", "Resource Two", NULL, NULL);
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(mock_server, r1));
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(mock_server, r2));
    mcp_resource_free(r1); // Server makes copies
    mcp_resource_free(r2);

    mcp_request_t request = { .id = 3, .method = "list_resources", .params = "{}" };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_list_resources_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and check content
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* result_node = mcp_json_object_get_property(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    mcp_json_t* resources_node = mcp_json_object_get_property(result_node, "resources");
    TEST_ASSERT_NOT_NULL(resources_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_ARRAY, mcp_json_get_type(resources_node));
    TEST_ASSERT_EQUAL_INT(2, mcp_json_array_get_size(resources_node)); // Expect 2 resources

    // Check first resource
    mcp_json_t* res1_obj = mcp_json_array_get_item(resources_node, 0);
    TEST_ASSERT_NOT_NULL(res1_obj);
    const char* uri1 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res1_obj, "uri"), &uri1);
    TEST_ASSERT_EQUAL_STRING("res://one", uri1);
    const char* name1 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res1_obj, "name"), &name1);
    TEST_ASSERT_EQUAL_STRING("Resource One", name1);
    const char* mime1 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res1_obj, "mimeType"), &mime1);
    TEST_ASSERT_EQUAL_STRING("text/plain", mime1);
    const char* desc1 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res1_obj, "description"), &desc1);
    TEST_ASSERT_EQUAL_STRING("Desc 1", desc1);

    // Check second resource (some fields NULL)
    mcp_json_t* res2_obj = mcp_json_array_get_item(resources_node, 1);
    TEST_ASSERT_NOT_NULL(res2_obj);
    const char* uri2 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res2_obj, "uri"), &uri2);
    TEST_ASSERT_EQUAL_STRING("res://two", uri2);
    const char* name2 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res2_obj, "name"), &name2);
    TEST_ASSERT_EQUAL_STRING("Resource Two", name2);
    TEST_ASSERT_NULL(mcp_json_object_get_property(res2_obj, "mimeType"));
    TEST_ASSERT_NULL(mcp_json_object_get_property(res2_obj, "description"));


    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}


// Test case for handle_list_resources_request with restricted permissions
void test_handle_list_resources_restricted(void) {
    // Add some resources to the mock server
    mcp_resource_t* r1 = mcp_resource_create("example://hello", "Hello Resource", "text/plain", NULL);
    mcp_resource_t* r2 = mcp_resource_create("example://world", "World Resource", NULL, NULL);
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(mock_server, r1));
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(mock_server, r2));
    mcp_resource_free(r1);
    mcp_resource_free(r2);

    mcp_request_t request = { .id = 4, .method = "list_resources", .params = "{}" };
    mcp_auth_context_t* auth_context = create_mock_auth_context(false); // Restricted permissions
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_list_resources_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and check content - should only see example://hello
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* result_node = mcp_json_object_get_property(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    mcp_json_t* resources_node = mcp_json_object_get_property(result_node, "resources");
    TEST_ASSERT_NOT_NULL(resources_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_ARRAY, mcp_json_get_type(resources_node));
    TEST_ASSERT_EQUAL_INT(1, mcp_json_array_get_size(resources_node)); // Only allowed resource

    // Check the one visible resource
    mcp_json_t* res_obj = mcp_json_array_get_item(resources_node, 0);
    TEST_ASSERT_NOT_NULL(res_obj);
    const char* uri = NULL;
    mcp_json_get_string(mcp_json_object_get_property(res_obj, "uri"), &uri);
    TEST_ASSERT_EQUAL_STRING("example://hello", uri);

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_read_resource_request success
void test_handle_read_resource_success(void) {
    // Add a test resource
    mcp_resource_t* r1 = mcp_resource_create("example://hello", "Hello Resource", "text/plain", NULL);
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(mock_server, r1));
    mcp_resource_free(r1);

    // Create request with URI parameter
    mcp_request_t request = { 
        .id = 5, 
        .method = "read_resource",
        .params = "{\"uri\":\"example://hello\"}"
    };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true); // Full permissions
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_read_resource_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and check content
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* result_node = mcp_json_object_get_property(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    mcp_json_t* content_node = mcp_json_object_get_property(result_node, "content");
    TEST_ASSERT_NOT_NULL(content_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_ARRAY, mcp_json_get_type(content_node));
    TEST_ASSERT_GREATER_THAN_INT(0, mcp_json_array_get_size(content_node));

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_read_resource_request with invalid URI
void test_handle_read_resource_invalid_uri(void) {
    mcp_request_t request = { 
        .id = 6, 
        .method = "read_resource",
        .params = "{\"uri\":\"nonexistent://resource\"}"
    };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_read_resource_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_RESOURCE_NOT_FOUND, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and verify error
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* error_node = mcp_json_object_get_property(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error_node);
    mcp_json_t* code_node = mcp_json_object_get_property(error_node, "code");
    TEST_ASSERT_NOT_NULL(code_node);
    double error_code_val = 0;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(code_node, &error_code_val));
    TEST_ASSERT_EQUAL_INT(MCP_ERROR_RESOURCE_NOT_FOUND, (int)error_code_val);

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_read_resource_request with permission denied
void test_handle_read_resource_permission_denied(void) {
    // Add a resource that's not in the restricted permission set
    mcp_resource_t* r1 = mcp_resource_create("example://world", "World Resource", "text/plain", NULL);
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_resource(mock_server, r1));
    mcp_resource_free(r1);

    mcp_request_t request = { 
        .id = 7, 
        .method = "read_resource",
        .params = "{\"uri\":\"example://world\"}"
    };
    mcp_auth_context_t* auth_context = create_mock_auth_context(false); // Restricted to example://hello
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_read_resource_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_FORBIDDEN, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and verify forbidden error
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* error_node = mcp_json_object_get_property(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error_node);
    mcp_json_t* code_node = mcp_json_object_get_property(error_node, "code");
    TEST_ASSERT_NOT_NULL(code_node);
    double error_code_val = 0;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(code_node, &error_code_val));
    TEST_ASSERT_EQUAL_INT(MCP_ERROR_FORBIDDEN, (int)error_code_val);

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_list_tools_request with no tools
void test_handle_list_tools_empty(void) {
    mcp_request_t request = { .id = 8, .method = "list_tools", .params = "{}" };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_list_tools_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and check content
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* result_node = mcp_json_object_get_property(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    mcp_json_t* tools_node = mcp_json_object_get_property(result_node, "tools");
    TEST_ASSERT_NOT_NULL(tools_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_ARRAY, mcp_json_get_type(tools_node));
    TEST_ASSERT_EQUAL_INT(0, mcp_json_array_get_size(tools_node)); // Expect empty array

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_list_tools_request with tools
void test_handle_list_tools_with_data(void) {
    // Add some tools to the mock server
    mcp_tool_t* t1 = mcp_tool_create("echo", "Echo Tool");
    mcp_tool_add_param(t1, "text", "string", "Text to echo", true);
    mcp_tool_t* t2 = mcp_tool_create("reverse", "Reverse Tool");
    mcp_tool_add_param(t2, "text", "string", "Text to reverse", true);
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_tool(mock_server, t1));
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_tool(mock_server, t2));
    mcp_tool_free(t1);
    mcp_tool_free(t2);

    mcp_request_t request = { .id = 9, .method = "list_tools", .params = "{}" };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_list_tools_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and check content
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* result_node = mcp_json_object_get_property(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    mcp_json_t* tools_node = mcp_json_object_get_property(result_node, "tools");
    TEST_ASSERT_NOT_NULL(tools_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_ARRAY, mcp_json_get_type(tools_node));
    TEST_ASSERT_EQUAL_INT(2, mcp_json_array_get_size(tools_node)); // Expect 2 tools

    // Check first tool
    mcp_json_t* tool1_obj = mcp_json_array_get_item(tools_node, 0);
    TEST_ASSERT_NOT_NULL(tool1_obj);
    const char* name1 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(tool1_obj, "name"), &name1);
    TEST_ASSERT_EQUAL_STRING("echo", name1);

    // Check second tool
    mcp_json_t* tool2_obj = mcp_json_array_get_item(tools_node, 1);
    TEST_ASSERT_NOT_NULL(tool2_obj);
    const char* name2 = NULL;
    mcp_json_get_string(mcp_json_object_get_property(tool2_obj, "name"), &name2);
    TEST_ASSERT_EQUAL_STRING("reverse", name2);

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_call_tool_request success
void test_handle_call_tool_success(void) {
    // Add a test tool
    mcp_tool_t* t1 = mcp_tool_create("echo", "Echo Tool");
    mcp_tool_add_param(t1, "text", "string", "Text to echo", true);
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_tool(mock_server, t1));
    mcp_tool_free(t1);

    // Create request with tool name and parameters
    mcp_request_t request = { 
        .id = 10, 
        .method = "call_tool",
        .params = "{\"name\":\"echo\",\"arguments\":{\"text\":\"Hello World\"}}"
    };
    mcp_auth_context_t* auth_context = create_mock_auth_context(true);
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_call_tool_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_NONE, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and check content
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* result_node = mcp_json_object_get_property(resp_json, "result");
    TEST_ASSERT_NOT_NULL(result_node);
    mcp_json_t* content_node = mcp_json_object_get_property(result_node, "content");
    TEST_ASSERT_NOT_NULL(content_node);
    TEST_ASSERT_EQUAL_INT(MCP_JSON_ARRAY, mcp_json_get_type(content_node));
    TEST_ASSERT_GREATER_THAN_INT(0, mcp_json_array_get_size(content_node));

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}

// Test case for handle_call_tool_request with permission denied
void test_handle_call_tool_permission_denied(void) {
    // Add a tool that's not in the restricted permission set
    mcp_tool_t* t1 = mcp_tool_create("reverse", "Reverse Tool");
    mcp_tool_add_param(t1, "text", "string", "Text to reverse", true);
    TEST_ASSERT_EQUAL_INT(0, mcp_server_add_tool(mock_server, t1));
    mcp_tool_free(t1);

    mcp_request_t request = { 
        .id = 11, 
        .method = "call_tool",
        .params = "{\"name\":\"reverse\",\"arguments\":{\"text\":\"test\"}}"
    };
    mcp_auth_context_t* auth_context = create_mock_auth_context(false); // Restricted to 'echo' tool
    TEST_ASSERT_NOT_NULL(auth_context);
    int error_code = 0;

    char* response_str = handle_call_tool_request(mock_server, &test_arena, &request, auth_context, &error_code);

    TEST_ASSERT_EQUAL_INT(MCP_ERROR_FORBIDDEN, error_code);
    TEST_ASSERT_NOT_NULL(response_str);

    // Parse response and verify forbidden error
    mcp_json_t* resp_json = mcp_json_parse(response_str);
    TEST_ASSERT_NOT_NULL(resp_json);
    mcp_json_t* error_node = mcp_json_object_get_property(resp_json, "error");
    TEST_ASSERT_NOT_NULL(error_node);
    mcp_json_t* code_node = mcp_json_object_get_property(error_node, "code");
    TEST_ASSERT_NOT_NULL(code_node);
    double error_code_val = 0;
    TEST_ASSERT_EQUAL_INT(0, mcp_json_get_number(code_node, &error_code_val));
    TEST_ASSERT_EQUAL_INT(MCP_ERROR_FORBIDDEN, (int)error_code_val);

    mcp_json_destroy(resp_json);
    free(response_str);
    mcp_auth_context_free(auth_context);
}
