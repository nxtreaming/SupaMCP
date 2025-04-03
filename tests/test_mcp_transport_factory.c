#include "unity.h"
#include "mcp_transport_factory.h"
#include "mcp_stdio_transport.h"
#include "mcp_tcp_transport.h"
#include "mcp_tcp_client_transport.h"
#include "transport/internal/transport_internal.h"

// Helper function to safely inspect the transport's function pointers
// Returns 1 if the pointers match expected values for the transport type
static int validate_transport_vtable(mcp_transport_t* transport, mcp_transport_type_t expected_type) {
    (void)expected_type; // Silence unused parameter warning
    
    if (!transport) {
        return 0;
    }

    // Check that function pointers are not NULL
    if (!transport->start || !transport->stop || !transport->send || !transport->destroy) {
        return 0;
    }
    
    // For this test, we just verify the transport has the required function pointers
    // In a real implementation, we might verify the exact function address matches
    // the known implementation for the specific transport type
    
    return 1;
}

// Test creating a stdio transport
void test_create_stdio_transport(void) {
    mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_STDIO, NULL);
    
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_EQUAL(1, validate_transport_vtable(transport, MCP_TRANSPORT_STDIO));
    
    mcp_transport_destroy(transport);
}

// Test creating a TCP server transport
void test_create_tcp_transport(void) {
    mcp_transport_config_t config = {0};
    config.tcp.host = "127.0.0.1";
    config.tcp.port = 8080;
    config.tcp.idle_timeout_ms = 5000;
    
    mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_TCP, &config);
    
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_EQUAL(1, validate_transport_vtable(transport, MCP_TRANSPORT_TCP));
    
    mcp_transport_destroy(transport);
}

// Test creating a TCP client transport
void test_create_tcp_client_transport(void) {
    mcp_transport_config_t config = {0};
    config.tcp.host = "127.0.0.1";
    config.tcp.port = 8080;
    
    mcp_transport_t* transport = mcp_transport_factory_create(MCP_TRANSPORT_TCP_CLIENT, &config);
    
    TEST_ASSERT_NOT_NULL(transport);
    TEST_ASSERT_EQUAL(1, validate_transport_vtable(transport, MCP_TRANSPORT_TCP_CLIENT));
    
    mcp_transport_destroy(transport);
}

// Test error handling for invalid input
void test_invalid_transport_type(void) {
    mcp_transport_config_t config = {0};
    config.tcp.host = "127.0.0.1";
    config.tcp.port = 8080;
    
    // Test with an invalid transport type (beyond enum range)
    mcp_transport_t* transport = mcp_transport_factory_create(99, &config);
    TEST_ASSERT_NULL(transport);
    
    // Test TCP transport with NULL config
    transport = mcp_transport_factory_create(MCP_TRANSPORT_TCP, NULL);
    TEST_ASSERT_NULL(transport);
    
    // Test TCP client with NULL config
    transport = mcp_transport_factory_create(MCP_TRANSPORT_TCP_CLIENT, NULL);
    TEST_ASSERT_NULL(transport);
}

// Function to run all transport factory tests from the main test runner
void run_mcp_transport_factory_tests(void) {
    RUN_TEST(test_create_stdio_transport);
    RUN_TEST(test_create_tcp_transport);
    RUN_TEST(test_create_tcp_client_transport);
    RUN_TEST(test_invalid_transport_type);
}
