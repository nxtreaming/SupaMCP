#include "unity.h"
#include "mcp_advanced_rate_limiter.h"
#include <time.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Use the platform_sleep_sec function from test_mcp_rate_limiter.c
extern void platform_sleep_sec(unsigned int seconds);

// --- Test Cases ---

void test_advanced_rate_limiter_create_destroy(void) {
    // Create a default configuration
    mcp_advanced_rate_limiter_config_t config = {
        .capacity_hint = 100,
        .enable_burst_handling = true,
        .burst_multiplier = 2,
        .burst_window_seconds = 10,
        .enable_dynamic_rules = false,
        .threshold_for_tightening = 0.9,
        .threshold_for_relaxing = 0.3
    };

    // Create the rate limiter
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(&config);
    TEST_ASSERT_NOT_NULL(limiter);

    // Destroy the rate limiter
    mcp_advanced_rate_limiter_destroy(limiter);
}

void test_advanced_rate_limiter_create_null_config(void) {
    // Create with NULL config (should use defaults)
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(NULL);
    TEST_ASSERT_NOT_NULL(limiter);

    // Destroy the rate limiter
    mcp_advanced_rate_limiter_destroy(limiter);
}

void test_advanced_rate_limiter_add_rule(void) {
    // Create the rate limiter
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(NULL);
    TEST_ASSERT_NOT_NULL(limiter);

    // Create a fixed window rule
    mcp_rate_limit_rule_t rule = mcp_advanced_rate_limiter_create_default_rule(
        MCP_RATE_LIMIT_KEY_IP,
        MCP_RATE_LIMIT_FIXED_WINDOW,
        60,  // 60 second window
        10   // 10 requests per window
    );

    // Add the rule
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_add_rule(limiter, &rule));

    // Destroy the rate limiter
    mcp_advanced_rate_limiter_destroy(limiter);
}

void test_advanced_rate_limiter_add_rule_with_pattern(void) {
    // Create the rate limiter
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(NULL);
    TEST_ASSERT_NOT_NULL(limiter);

    // Create a fixed window rule with pattern
    mcp_rate_limit_rule_t rule = mcp_advanced_rate_limiter_create_default_rule(
        MCP_RATE_LIMIT_KEY_IP,
        MCP_RATE_LIMIT_FIXED_WINDOW,
        60,  // 60 second window
        10   // 10 requests per window
    );
    rule.key_pattern = "192.168.*";
    rule.priority = 10;

    // Add the rule
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_add_rule(limiter, &rule));

    // Destroy the rate limiter
    mcp_advanced_rate_limiter_destroy(limiter);
}

void test_advanced_rate_limiter_remove_rule(void) {
    // Create the rate limiter
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(NULL);
    TEST_ASSERT_NOT_NULL(limiter);

    // Create a fixed window rule with pattern
    mcp_rate_limit_rule_t rule = mcp_advanced_rate_limiter_create_default_rule(
        MCP_RATE_LIMIT_KEY_IP,
        MCP_RATE_LIMIT_FIXED_WINDOW,
        60,  // 60 second window
        10   // 10 requests per window
    );
    rule.key_pattern = "192.168.*";

    // Add the rule
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_add_rule(limiter, &rule));

    // Remove the rule
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_remove_rule(limiter, MCP_RATE_LIMIT_KEY_IP, "192.168.*"));

    // Try to remove a non-existent rule
    TEST_ASSERT_FALSE(mcp_advanced_rate_limiter_remove_rule(limiter, MCP_RATE_LIMIT_KEY_IP, "10.0.*"));

    // Destroy the rate limiter
    mcp_advanced_rate_limiter_destroy(limiter);
}

void test_advanced_rate_limiter_fixed_window(void) {
    // Create the rate limiter
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(NULL);
    TEST_ASSERT_NOT_NULL(limiter);

    // Create a fixed window rule (3 requests per 2 seconds)
    mcp_rate_limit_rule_t rule = mcp_advanced_rate_limiter_create_default_rule(
        MCP_RATE_LIMIT_KEY_IP,
        MCP_RATE_LIMIT_FIXED_WINDOW,
        2,   // 2 second window
        3    // 3 requests per window
    );

    // Add the rule
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_add_rule(limiter, &rule));

    // Test with IP address
    const char* ip = "192.168.1.1";

    // First 3 requests should be allowed
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // 4th request should be denied
    TEST_ASSERT_FALSE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // Wait for the window to expire
    platform_sleep_sec(3);

    // Should be allowed again
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // Destroy the rate limiter
    mcp_advanced_rate_limiter_destroy(limiter);
}

void test_advanced_rate_limiter_token_bucket(void) {
    // Create the rate limiter
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(NULL);
    TEST_ASSERT_NOT_NULL(limiter);

    // Create a token bucket rule (1 token per second, max 3 tokens)
    mcp_rate_limit_rule_t rule = mcp_advanced_rate_limiter_create_token_bucket_rule(
        MCP_RATE_LIMIT_KEY_IP,
        1.0,  // 1 token per second
        3     // max 3 tokens
    );

    // Add the rule
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_add_rule(limiter, &rule));

    // Test with IP address
    const char* ip = "192.168.1.2";

    // First 3 requests should be allowed (initial tokens)
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // 4th request should be denied (no tokens left)
    TEST_ASSERT_FALSE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // Wait for a token to be added
    platform_sleep_sec(2);

    // Should be allowed again (1-2 tokens refilled)
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // Destroy the rate limiter
    mcp_advanced_rate_limiter_destroy(limiter);
}

void test_advanced_rate_limiter_leaky_bucket(void) {
    // Create the rate limiter
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(NULL);
    TEST_ASSERT_NOT_NULL(limiter);

    // Create a leaky bucket rule (1 request per second, burst capacity 3)
    mcp_rate_limit_rule_t rule = mcp_advanced_rate_limiter_create_leaky_bucket_rule(
        MCP_RATE_LIMIT_KEY_IP,
        1.0,  // leak 1 unit per second
        3     // burst capacity of 3
    );

    // Add the rule
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_add_rule(limiter, &rule));

    // Test with IP address
    const char* ip = "192.168.1.3";

    // First 3 requests should be allowed (up to burst capacity)
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // 4th request should be denied (bucket full)
    TEST_ASSERT_FALSE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // Wait for some water to leak
    platform_sleep_sec(2);

    // Should be allowed again (1-2 units leaked)
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // Destroy the rate limiter
    mcp_advanced_rate_limiter_destroy(limiter);
}

void test_advanced_rate_limiter_multiple_key_types(void) {
    // Create the rate limiter
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(NULL);
    TEST_ASSERT_NOT_NULL(limiter);

    // Create rules for different key types
    mcp_rate_limit_rule_t ip_rule = mcp_advanced_rate_limiter_create_default_rule(
        MCP_RATE_LIMIT_KEY_IP,
        MCP_RATE_LIMIT_FIXED_WINDOW,
        60,  // 60 second window
        5    // 5 requests per window
    );

    mcp_rate_limit_rule_t user_rule = mcp_advanced_rate_limiter_create_default_rule(
        MCP_RATE_LIMIT_KEY_USER_ID,
        MCP_RATE_LIMIT_FIXED_WINDOW,
        60,  // 60 second window
        10   // 10 requests per window
    );

    mcp_rate_limit_rule_t api_rule = mcp_advanced_rate_limiter_create_default_rule(
        MCP_RATE_LIMIT_KEY_API_KEY,
        MCP_RATE_LIMIT_FIXED_WINDOW,
        60,  // 60 second window
        20   // 20 requests per window
    );

    // Add the rules
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_add_rule(limiter, &ip_rule));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_add_rule(limiter, &user_rule));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_add_rule(limiter, &api_rule));

    // Test with different key types
    const char* ip = "192.168.1.4";
    const char* user_id = "user123";
    const char* api_key = "api456";

    // Check with IP only (limit 5)
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    }
    TEST_ASSERT_FALSE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // Check with user ID only (limit 10)
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, NULL, user_id, NULL, NULL));
    }
    TEST_ASSERT_FALSE(mcp_advanced_rate_limiter_check(limiter, NULL, user_id, NULL, NULL));

    // Check with API key only (limit 20)
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, NULL, NULL, api_key, NULL));
    }
    TEST_ASSERT_FALSE(mcp_advanced_rate_limiter_check(limiter, NULL, NULL, api_key, NULL));

    // Check with multiple keys (should use API key first, which is already at limit)
    TEST_ASSERT_FALSE(mcp_advanced_rate_limiter_check(limiter, ip, user_id, api_key, NULL));

    // Destroy the rate limiter
    mcp_advanced_rate_limiter_destroy(limiter);
}

void test_advanced_rate_limiter_get_stats(void) {
    // Create the rate limiter
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(NULL);
    TEST_ASSERT_NOT_NULL(limiter);

    // Create a rule
    mcp_rate_limit_rule_t rule = mcp_advanced_rate_limiter_create_default_rule(
        MCP_RATE_LIMIT_KEY_IP,
        MCP_RATE_LIMIT_FIXED_WINDOW,
        60,  // 60 second window
        3    // 3 requests per window
    );

    // Add the rule
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_add_rule(limiter, &rule));

    // Test with IP address
    const char* ip = "192.168.1.5";

    // Make some requests (3 allowed, 2 denied)
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_FALSE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_FALSE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // Get statistics
    mcp_advanced_rate_limiter_stats_t stats;
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_get_stats(limiter, &stats));

    // Check statistics
    TEST_ASSERT_EQUAL_UINT(5, stats.total_requests);
    TEST_ASSERT_EQUAL_UINT(3, stats.allowed_requests);
    TEST_ASSERT_EQUAL_UINT(2, stats.denied_requests);
    TEST_ASSERT_EQUAL_UINT(1, stats.active_clients);
    TEST_ASSERT_EQUAL_UINT(1, stats.rule_count);
    TEST_ASSERT_EQUAL_FLOAT(0.4, stats.denial_rate);

    // Destroy the rate limiter
    mcp_advanced_rate_limiter_destroy(limiter);
}

void test_advanced_rate_limiter_clear_data(void) {
    // Create the rate limiter
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(NULL);
    TEST_ASSERT_NOT_NULL(limiter);

    // Create a rule
    mcp_rate_limit_rule_t rule = mcp_advanced_rate_limiter_create_default_rule(
        MCP_RATE_LIMIT_KEY_IP,
        MCP_RATE_LIMIT_FIXED_WINDOW,
        60,  // 60 second window
        3    // 3 requests per window
    );

    // Add the rule
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_add_rule(limiter, &rule));

    // Test with IP address
    const char* ip = "192.168.1.6";

    // Make some requests (3 allowed, 1 denied)
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));
    TEST_ASSERT_FALSE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // Clear data
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_clear_data(limiter));

    // Get statistics
    mcp_advanced_rate_limiter_stats_t stats;
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_get_stats(limiter, &stats));

    // Check statistics are reset
    TEST_ASSERT_EQUAL_UINT(0, stats.total_requests);
    TEST_ASSERT_EQUAL_UINT(0, stats.allowed_requests);
    TEST_ASSERT_EQUAL_UINT(0, stats.denied_requests);
    TEST_ASSERT_EQUAL_UINT(0, stats.active_clients);
    TEST_ASSERT_EQUAL_UINT(1, stats.rule_count); // Rules are not cleared

    // Should be allowed again
    TEST_ASSERT_TRUE(mcp_advanced_rate_limiter_check(limiter, ip, NULL, NULL, NULL));

    // Destroy the rate limiter
    mcp_advanced_rate_limiter_destroy(limiter);
}

// Function to run all advanced rate limiter tests
void run_all_advanced_rate_limiter_tests(void) {
    RUN_TEST(test_advanced_rate_limiter_create_destroy);
    RUN_TEST(test_advanced_rate_limiter_create_null_config);
    RUN_TEST(test_advanced_rate_limiter_add_rule);
    RUN_TEST(test_advanced_rate_limiter_add_rule_with_pattern);
    RUN_TEST(test_advanced_rate_limiter_remove_rule);
    RUN_TEST(test_advanced_rate_limiter_fixed_window);
    RUN_TEST(test_advanced_rate_limiter_token_bucket);
    RUN_TEST(test_advanced_rate_limiter_leaky_bucket);
    RUN_TEST(test_advanced_rate_limiter_multiple_key_types);
    RUN_TEST(test_advanced_rate_limiter_get_stats);
    RUN_TEST(test_advanced_rate_limiter_clear_data);
}
