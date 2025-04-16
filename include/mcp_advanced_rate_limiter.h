#ifndef MCP_ADVANCED_RATE_LIMITER_H
#define MCP_ADVANCED_RATE_LIMITER_H

#include <stddef.h>
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Rate limiting algorithm types
 */
typedef enum {
    MCP_RATE_LIMIT_FIXED_WINDOW = 0,  /**< Fixed window rate limiting */
    MCP_RATE_LIMIT_SLIDING_WINDOW,    /**< Sliding window rate limiting */
    MCP_RATE_LIMIT_TOKEN_BUCKET,      /**< Token bucket rate limiting */
    MCP_RATE_LIMIT_LEAKY_BUCKET       /**< Leaky bucket rate limiting */
} mcp_rate_limit_algorithm_t;

/**
 * @brief Rate limiting key types
 */
typedef enum {
    MCP_RATE_LIMIT_KEY_IP = 0,        /**< Limit based on IP address */
    MCP_RATE_LIMIT_KEY_USER_ID,       /**< Limit based on user ID */
    MCP_RATE_LIMIT_KEY_API_KEY,       /**< Limit based on API key */
    MCP_RATE_LIMIT_KEY_CUSTOM         /**< Limit based on custom key */
} mcp_rate_limit_key_type_t;

/**
 * @brief Rate limiting rule
 */
typedef struct {
    mcp_rate_limit_key_type_t key_type;   /**< Type of key to use for rate limiting */
    mcp_rate_limit_algorithm_t algorithm; /**< Rate limiting algorithm to use */
    size_t window_seconds;                /**< Time window in seconds (for fixed/sliding window) */
    size_t max_requests_per_window;       /**< Max requests per window (for fixed/sliding window) */
    double tokens_per_second;             /**< Token refill rate (for token bucket) */
    size_t max_tokens;                    /**< Maximum token capacity (for token bucket) */
    double leak_rate_per_second;          /**< Leak rate (for leaky bucket) */
    size_t burst_capacity;                /**< Burst capacity (for leaky bucket) */
    char* key_pattern;                    /**< Pattern to match for this rule (e.g., IP prefix, user group) */
    int priority;                         /**< Rule priority (higher number = higher priority) */
} mcp_rate_limit_rule_t;

/**
 * @brief Advanced rate limiter configuration
 */
typedef struct {
    size_t capacity_hint;                 /**< Approximate number of clients to track */
    bool enable_burst_handling;           /**< Whether to enable burst handling */
    size_t burst_multiplier;              /**< Multiplier for burst capacity */
    size_t burst_window_seconds;          /**< Time window for burst handling */
    bool enable_dynamic_rules;            /**< Whether to enable dynamic rule adjustment */
    double threshold_for_tightening;      /**< Traffic threshold for tightening rules (0.0-1.0) */
    double threshold_for_relaxing;        /**< Traffic threshold for relaxing rules (0.0-1.0) */
} mcp_advanced_rate_limiter_config_t;

/**
 * @brief Advanced rate limiter statistics
 */
typedef struct {
    size_t total_requests;                /**< Total number of requests processed */
    size_t allowed_requests;              /**< Number of allowed requests */
    size_t denied_requests;               /**< Number of denied requests */
    size_t active_clients;                /**< Number of active clients being tracked */
    size_t peak_clients;                  /**< Peak number of clients tracked */
    size_t rule_count;                    /**< Number of active rules */
    double denial_rate;                   /**< Percentage of requests denied (0.0-1.0) */
} mcp_advanced_rate_limiter_stats_t;

// Forward declaration
typedef struct mcp_advanced_rate_limiter mcp_advanced_rate_limiter_t;

/**
 * @brief Creates a new advanced rate limiter instance.
 *
 * @param config Configuration for the rate limiter
 * @return A pointer to the newly created rate limiter, or NULL on failure.
 *         The caller is responsible for destroying the limiter using mcp_advanced_rate_limiter_destroy().
 */
mcp_advanced_rate_limiter_t* mcp_advanced_rate_limiter_create(const mcp_advanced_rate_limiter_config_t* config);

/**
 * @brief Destroys the advanced rate limiter and frees all associated memory.
 *
 * @param limiter The rate limiter instance to destroy.
 */
void mcp_advanced_rate_limiter_destroy(mcp_advanced_rate_limiter_t* limiter);

/**
 * @brief Adds a rate limiting rule to the limiter.
 *
 * @param limiter The rate limiter instance.
 * @param rule The rule to add.
 * @return True if the rule was added successfully, false otherwise.
 */
bool mcp_advanced_rate_limiter_add_rule(mcp_advanced_rate_limiter_t* limiter, const mcp_rate_limit_rule_t* rule);

/**
 * @brief Removes a rate limiting rule from the limiter.
 *
 * @param limiter The rate limiter instance.
 * @param key_type The key type of the rule to remove.
 * @param key_pattern The key pattern of the rule to remove.
 * @return True if the rule was removed successfully, false otherwise.
 */
bool mcp_advanced_rate_limiter_remove_rule(mcp_advanced_rate_limiter_t* limiter, 
                                          mcp_rate_limit_key_type_t key_type, 
                                          const char* key_pattern);

/**
 * @brief Checks if a request is allowed based on the rate limiting rules.
 *
 * @param limiter The rate limiter instance.
 * @param ip_address The IP address of the client.
 * @param user_id The user ID of the client (can be NULL).
 * @param api_key The API key of the client (can be NULL).
 * @param custom_key A custom key for rate limiting (can be NULL).
 * @return True if the request is allowed, false if it should be denied.
 */
bool mcp_advanced_rate_limiter_check(mcp_advanced_rate_limiter_t* limiter, 
                                    const char* ip_address, 
                                    const char* user_id, 
                                    const char* api_key, 
                                    const char* custom_key);

/**
 * @brief Gets statistics from the rate limiter.
 *
 * @param limiter The rate limiter instance.
 * @param stats Pointer to a statistics structure to fill.
 * @return True if statistics were retrieved successfully, false otherwise.
 */
bool mcp_advanced_rate_limiter_get_stats(mcp_advanced_rate_limiter_t* limiter, 
                                        mcp_advanced_rate_limiter_stats_t* stats);

/**
 * @brief Clears all rate limiting data but keeps the rules.
 *
 * @param limiter The rate limiter instance.
 * @return True if the data was cleared successfully, false otherwise.
 */
bool mcp_advanced_rate_limiter_clear_data(mcp_advanced_rate_limiter_t* limiter);

/**
 * @brief Creates a default rate limiting rule.
 *
 * @param key_type The key type for the rule.
 * @param algorithm The algorithm to use.
 * @param window_seconds The time window in seconds.
 * @param max_requests_per_window The maximum requests per window.
 * @return A new rate limiting rule with default values.
 */
mcp_rate_limit_rule_t mcp_advanced_rate_limiter_create_default_rule(
    mcp_rate_limit_key_type_t key_type,
    mcp_rate_limit_algorithm_t algorithm,
    size_t window_seconds,
    size_t max_requests_per_window);

/**
 * @brief Creates a token bucket rate limiting rule.
 *
 * @param key_type The key type for the rule.
 * @param tokens_per_second The token refill rate.
 * @param max_tokens The maximum token capacity.
 * @return A new token bucket rate limiting rule.
 */
mcp_rate_limit_rule_t mcp_advanced_rate_limiter_create_token_bucket_rule(
    mcp_rate_limit_key_type_t key_type,
    double tokens_per_second,
    size_t max_tokens);

/**
 * @brief Creates a leaky bucket rate limiting rule.
 *
 * @param key_type The key type for the rule.
 * @param leak_rate_per_second The leak rate.
 * @param burst_capacity The burst capacity.
 * @return A new leaky bucket rate limiting rule.
 */
mcp_rate_limit_rule_t mcp_advanced_rate_limiter_create_leaky_bucket_rule(
    mcp_rate_limit_key_type_t key_type,
    double leak_rate_per_second,
    size_t burst_capacity);

#ifdef __cplusplus
}
#endif

#endif // MCP_ADVANCED_RATE_LIMITER_H
