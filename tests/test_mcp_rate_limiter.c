#include "unity.h"
#include "mcp_rate_limiter.h"
#include <time.h>
#include <stdbool.h>
#include <stdio.h>

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

// --- Test Cases ---

void test_rate_limiter_create_destroy(void) {
    // capacity=100 clients, window=60s, max_req=10
    mcp_rate_limiter_t* limiter = mcp_rate_limiter_create(100, 60, 10);
    TEST_ASSERT_NOT_NULL(limiter);
    mcp_rate_limiter_destroy(limiter);
}

void test_rate_limiter_create_invalid(void) {
    // Invalid capacity
    mcp_rate_limiter_t* limiter_zero_cap = mcp_rate_limiter_create(0, 60, 10);
    TEST_ASSERT_NULL(limiter_zero_cap);

    // Invalid window
    mcp_rate_limiter_t* limiter_zero_win = mcp_rate_limiter_create(100, 0, 10);
    TEST_ASSERT_NULL(limiter_zero_win);

    // Invalid max requests (0 might be allowed depending on implementation, but let's assume not)
    mcp_rate_limiter_t* limiter_zero_req = mcp_rate_limiter_create(100, 60, 0);
    TEST_ASSERT_NULL(limiter_zero_req);
}

void test_rate_limiter_allow_single_client_within_limit(void) {
    // 3 requests per 2 seconds for capacity 10
    mcp_rate_limiter_t* limiter = mcp_rate_limiter_create(10, 2, 3);
    TEST_ASSERT_NOT_NULL(limiter);

    const char* client1 = "client_A";

    // Allow 3 requests quickly for client_A
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client1));
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client1));
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client1));

    mcp_rate_limiter_destroy(limiter);
}

void test_rate_limiter_block_single_client_exceeding_limit(void) {
    // 2 requests per 2 seconds
    mcp_rate_limiter_t* limiter = mcp_rate_limiter_create(10, 2, 2);
    TEST_ASSERT_NOT_NULL(limiter);

    const char* client1 = "client_A";

    // Allow 2 requests
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client1));
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client1));

    // The 3rd request within the same window should be blocked
    TEST_ASSERT_FALSE(mcp_rate_limiter_check(limiter, client1));

    mcp_rate_limiter_destroy(limiter);
}

void test_rate_limiter_multiple_clients(void) {
    // 2 requests per 2 seconds
    mcp_rate_limiter_t* limiter = mcp_rate_limiter_create(10, 2, 2);
    TEST_ASSERT_NOT_NULL(limiter);

    const char* client1 = "client_A";
    const char* client2 = "client_B";

    // Allow 2 requests for client A
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client1));
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client1));
    TEST_ASSERT_FALSE(mcp_rate_limiter_check(limiter, client1)); // Block 3rd for A

    // Allow 2 requests for client B (should be independent)
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client2));
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client2));
    TEST_ASSERT_FALSE(mcp_rate_limiter_check(limiter, client2)); // Block 3rd for B

    mcp_rate_limiter_destroy(limiter);
}

void test_rate_limiter_window_reset(void) {
    // 1 request per 1 second
    mcp_rate_limiter_t* limiter = mcp_rate_limiter_create(10, 1, 1);
    TEST_ASSERT_NOT_NULL(limiter);

    const char* client1 = "client_A";

    // Allow 1 request
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client1));
    // Block 2nd request in the same window
    TEST_ASSERT_FALSE(mcp_rate_limiter_check(limiter, client1));

    // Wait for the window to reset (plus a little buffer)
    platform_sleep_sec(2);

    // Should allow 1 request again in the new window
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client1));
    // Block 2nd request in the new window
    TEST_ASSERT_FALSE(mcp_rate_limiter_check(limiter, client1));

    mcp_rate_limiter_destroy(limiter);
}

void test_rate_limiter_capacity_limit(void) {
    // Capacity 2, 5 req per 10 sec
    mcp_rate_limiter_t* limiter = mcp_rate_limiter_create(2, 10, 5);
    TEST_ASSERT_NOT_NULL(limiter);

    const char* client1 = "client_A";
    const char* client2 = "client_B";
    const char* client3 = "client_C"; // Exceeds capacity

    // Use client 1 and 2
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client1));
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client2));

    // Use client 3 - this should cause eviction (likely client1 if LRU)
    // or be blocked depending on exact eviction/handling strategy.
    // We'll assume it gets allowed by evicting the oldest (client1).
    // A more robust test might need internal state inspection or specific eviction guarantees.
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client3));

    // Now client 1 should potentially be blocked if it was evicted,
    // or allowed if a different strategy is used. Let's assume LRU eviction.
    // This test is somewhat dependent on the internal eviction logic.
    // A simpler check is just that client3 was allowed.

    // Check client 2 and 3 are still allowed within limits
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client2));
    TEST_ASSERT_TRUE(mcp_rate_limiter_check(limiter, client3));

    mcp_rate_limiter_destroy(limiter);
}


// --- Test Group Runner ---
void run_mcp_rate_limiter_tests(void) {
    RUN_TEST(test_rate_limiter_create_destroy);
    RUN_TEST(test_rate_limiter_create_invalid);
    RUN_TEST(test_rate_limiter_allow_single_client_within_limit);
    RUN_TEST(test_rate_limiter_block_single_client_exceeding_limit);
    RUN_TEST(test_rate_limiter_multiple_clients);
    RUN_TEST(test_rate_limiter_window_reset);
    RUN_TEST(test_rate_limiter_capacity_limit); // Basic capacity test
}
