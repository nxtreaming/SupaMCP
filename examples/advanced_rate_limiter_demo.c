#include "mcp_advanced_rate_limiter.h"
#include "mcp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Helper function for cross-platform sleep (seconds)
void platform_sleep_sec(unsigned int seconds) {
#ifdef _WIN32
    Sleep(seconds * 1000);
#else
    sleep(seconds);
#endif
}

// Helper function to print rate limiter statistics
void print_rate_limiter_stats(mcp_advanced_rate_limiter_t* limiter) {
    mcp_advanced_rate_limiter_stats_t stats;
    if (mcp_advanced_rate_limiter_get_stats(limiter, &stats)) {
        printf("Rate Limiter Statistics:\n");
        printf("  Total requests: %zu\n", stats.total_requests);
        printf("  Allowed requests: %zu\n", stats.allowed_requests);
        printf("  Denied requests: %zu\n", stats.denied_requests);
        printf("  Active clients: %zu\n", stats.active_clients);
        printf("  Peak clients: %zu\n", stats.peak_clients);
        printf("  Rule count: %zu\n", stats.rule_count);
        printf("  Denial rate: %.2f%%\n", stats.denial_rate * 100.0);
    } else {
        printf("Failed to get rate limiter statistics\n");
    }
}

// Helper function to simulate requests
void simulate_requests(mcp_advanced_rate_limiter_t* limiter, const char* ip, const char* user_id, const char* api_key, int count) {
    int allowed = 0;
    int denied = 0;
    
    printf("Simulating %d requests for IP=%s, User=%s, API Key=%s\n", 
           count, ip ? ip : "NULL", user_id ? user_id : "NULL", api_key ? api_key : "NULL");
    
    for (int i = 0; i < count; i++) {
        if (mcp_advanced_rate_limiter_check(limiter, ip, user_id, api_key, NULL)) {
            allowed++;
        } else {
            denied++;
        }
    }
    
    printf("Results: %d allowed, %d denied\n", allowed, denied);
}

int main() {
    // Initialize logging
    mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
    mcp_log_info("Advanced Rate Limiter Demo started");
    
    // Create rate limiter with configuration
    mcp_advanced_rate_limiter_config_t config = {
        .capacity_hint = 100,
        .enable_burst_handling = true,
        .burst_multiplier = 2,
        .burst_window_seconds = 10,
        .enable_dynamic_rules = false,
        .threshold_for_tightening = 0.9,
        .threshold_for_relaxing = 0.3
    };
    
    mcp_advanced_rate_limiter_t* limiter = mcp_advanced_rate_limiter_create(&config);
    if (!limiter) {
        mcp_log_error("Failed to create advanced rate limiter");
        return 1;
    }
    
    printf("Advanced Rate Limiter Demo\n");
    printf("==========================\n\n");
    
    // Add rules for different key types
    
    // 1. IP-based rules
    mcp_rate_limit_rule_t ip_rule = mcp_advanced_rate_limiter_create_default_rule(
        MCP_RATE_LIMIT_KEY_IP,
        MCP_RATE_LIMIT_FIXED_WINDOW,
        60,  // 60 second window
        10   // 10 requests per minute
    );
    ip_rule.priority = 10;
    mcp_advanced_rate_limiter_add_rule(limiter, &ip_rule);
    
    // Special rule for specific IP range
    mcp_rate_limit_rule_t ip_range_rule = mcp_advanced_rate_limiter_create_default_rule(
        MCP_RATE_LIMIT_KEY_IP,
        MCP_RATE_LIMIT_FIXED_WINDOW,
        60,  // 60 second window
        5    // 5 requests per minute (more strict)
    );
    ip_range_rule.key_pattern = "192.168.*";
    ip_range_rule.priority = 20; // Higher priority
    mcp_advanced_rate_limiter_add_rule(limiter, &ip_range_rule);
    
    // 2. User ID-based rules
    mcp_rate_limit_rule_t user_rule = mcp_advanced_rate_limiter_create_token_bucket_rule(
        MCP_RATE_LIMIT_KEY_USER_ID,
        0.5,  // 0.5 tokens per second (30 per minute)
        10    // Max 10 tokens (burst capacity)
    );
    user_rule.priority = 30; // Higher priority than IP
    mcp_advanced_rate_limiter_add_rule(limiter, &user_rule);
    
    // 3. API key-based rules
    mcp_rate_limit_rule_t api_rule = mcp_advanced_rate_limiter_create_leaky_bucket_rule(
        MCP_RATE_LIMIT_KEY_API_KEY,
        1.0,  // Leak 1 request per second
        20    // Burst capacity of 20
    );
    api_rule.priority = 40; // Highest priority
    mcp_advanced_rate_limiter_add_rule(limiter, &api_rule);
    
    printf("Added rate limiting rules:\n");
    printf("1. IP-based fixed window: 10 requests per minute\n");
    printf("2. IP range (192.168.*) fixed window: 5 requests per minute\n");
    printf("3. User ID-based token bucket: 0.5 tokens/sec, max 10 tokens\n");
    printf("4. API key-based leaky bucket: 1 req/sec leak rate, 20 burst capacity\n\n");
    
    // Test scenarios
    printf("Running test scenarios...\n\n");
    
    // Scenario 1: Regular IP
    printf("Scenario 1: Regular IP (10.0.0.1) - limit 10 req/min\n");
    simulate_requests(limiter, "10.0.0.1", NULL, NULL, 15);
    print_rate_limiter_stats(limiter);
    printf("\n");
    
    // Scenario 2: IP in the special range
    printf("Scenario 2: Special IP range (192.168.1.1) - limit 5 req/min\n");
    simulate_requests(limiter, "192.168.1.1", NULL, NULL, 10);
    print_rate_limiter_stats(limiter);
    printf("\n");
    
    // Scenario 3: User ID with token bucket
    printf("Scenario 3: User ID with token bucket - 0.5 tokens/sec, max 10\n");
    printf("First burst of 12 requests (should allow 10, deny 2):\n");
    simulate_requests(limiter, NULL, "user123", NULL, 12);
    
    printf("Waiting 10 seconds for tokens to refill...\n");
    platform_sleep_sec(10);
    
    printf("After waiting, should have ~5 more tokens:\n");
    simulate_requests(limiter, NULL, "user123", NULL, 6);
    print_rate_limiter_stats(limiter);
    printf("\n");
    
    // Scenario 4: API key with leaky bucket
    printf("Scenario 4: API key with leaky bucket - 1 req/sec leak, 20 burst\n");
    printf("First burst of 25 requests (should allow 20, deny 5):\n");
    simulate_requests(limiter, NULL, NULL, "api456", 25);
    
    printf("Waiting 5 seconds for bucket to leak...\n");
    platform_sleep_sec(5);
    
    printf("After waiting, should have ~5 more capacity:\n");
    simulate_requests(limiter, NULL, NULL, "api456", 7);
    print_rate_limiter_stats(limiter);
    printf("\n");
    
    // Scenario 5: Multiple identifiers (should use highest priority - API key)
    printf("Scenario 5: Request with multiple identifiers\n");
    printf("Using IP=10.0.0.1 (limit 10), User=user123 (limit varies), API=api456 (at limit)\n");
    simulate_requests(limiter, "10.0.0.1", "user123", "api456", 5);
    print_rate_limiter_stats(limiter);
    printf("\n");
    
    // Clear data and show stats
    printf("Clearing rate limiter data...\n");
    mcp_advanced_rate_limiter_clear_data(limiter);
    print_rate_limiter_stats(limiter);
    
    // Clean up
    mcp_advanced_rate_limiter_destroy(limiter);
    mcp_log_info("Advanced Rate Limiter Demo completed");
    mcp_log_close();
    
    return 0;
}
