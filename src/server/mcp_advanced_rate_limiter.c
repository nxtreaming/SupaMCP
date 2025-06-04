#include "mcp_advanced_rate_limiter.h"
#include "mcp_types.h"
#include "mcp_string_utils.h"
#include "mcp_rwlock.h"
#include "mcp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

/** @internal Initial capacity factor for the hash table. Capacity = capacity_hint * factor. */
#define RATE_LIMIT_HASH_TABLE_CAPACITY_FACTOR 2
/** @internal Load factor threshold for the hash table. */
#define RATE_LIMIT_HASH_TABLE_MAX_LOAD_FACTOR 0.75
/** @internal Default capacity hint if not specified. */
#define DEFAULT_CAPACITY_HINT 1024
/** @internal Default burst multiplier. */
#define DEFAULT_BURST_MULTIPLIER 2
/** @internal Default burst window in seconds. */
#define DEFAULT_BURST_WINDOW_SECONDS 10
/** @internal Default threshold for tightening rules (90% of max). */
#define DEFAULT_THRESHOLD_TIGHTENING 0.9
/** @internal Default threshold for relaxing rules (30% of max). */
#define DEFAULT_THRESHOLD_RELAXING 0.3
/** @internal Maximum number of rules per key type. */
#define MAX_RULES_PER_KEY_TYPE 32
/** @internal Maximum rule priority. */
#define MAX_RULE_PRIORITY 100

/**
 * @internal
 * @brief Client tracking entry for fixed window algorithm.
 */
typedef struct {
    time_t window_start_time;   /**< Start time of the current window. */
    size_t request_count;       /**< Number of requests in the current window. */
} fixed_window_data_t;

/**
 * @internal
 * @brief Client tracking entry for sliding window algorithm.
 */
typedef struct {
    time_t last_request_time;   /**< Time of the last request. */
    double* request_times;      /**< Circular buffer of request timestamps. */
    size_t buffer_size;         /**< Size of the circular buffer. */
    size_t buffer_pos;          /**< Current position in the circular buffer. */
    size_t request_count;       /**< Number of requests in the current window. */
} sliding_window_data_t;

/**
 * @internal
 * @brief Client tracking entry for token bucket algorithm.
 */
typedef struct {
    double tokens;              /**< Current number of tokens in the bucket. */
    time_t last_refill_time;    /**< Time of the last token refill. */
    double tokens_per_second;   /**< Rate at which tokens are added to the bucket. */
    size_t max_tokens;          /**< Maximum number of tokens the bucket can hold. */
} token_bucket_data_t;

/**
 * @internal
 * @brief Client tracking entry for leaky bucket algorithm.
 */
typedef struct {
    double water_level;         /**< Current water level in the bucket. */
    time_t last_leak_time;      /**< Time of the last leak. */
    double leak_rate_per_second;/**< Rate at which water leaks from the bucket. */
    size_t burst_capacity;      /**< Maximum capacity of the bucket. */
} leaky_bucket_data_t;

/**
 * @internal
 * @brief Union of algorithm-specific data.
 */
typedef union {
    fixed_window_data_t fixed_window;
    sliding_window_data_t sliding_window;
    token_bucket_data_t token_bucket;
    leaky_bucket_data_t leaky_bucket;
} algorithm_data_t;

/**
 * @internal
 * @brief Rate limiting rule with pattern matching.
 */
typedef struct rule_entry {
    mcp_rate_limit_rule_t rule;         /**< The rate limiting rule. */
    struct rule_entry* next;            /**< Next rule in the linked list. */
} rule_entry_t;

/**
 * @internal
 * @brief Client tracking entry for rate limiting.
 */
typedef struct client_entry {
    char* key;                          /**< The client key (IP, user ID, etc.). */
    mcp_rate_limit_key_type_t key_type; /**< The type of key. */
    mcp_rate_limit_algorithm_t algorithm; /**< The algorithm used for this client. */
    algorithm_data_t data;              /**< Algorithm-specific data. */
    struct client_entry* next;          /**< Next entry in the hash table bucket. */
} client_entry_t;

/**
 * @internal
 * @brief Advanced rate limiter structure.
 */
struct mcp_advanced_rate_limiter {
    mcp_rwlock_t* lock;                 /**< Read-write lock for thread safety. */
    client_entry_t** client_buckets;    /**< Hash table buckets for client entries. */
    size_t client_capacity;             /**< Capacity of the client hash table. */
    size_t client_count;                /**< Number of clients being tracked. */
    size_t peak_client_count;           /**< Peak number of clients tracked. */

    rule_entry_t* rules[4];             /**< Rules for each key type (IP, user ID, API key, custom). */
    size_t rule_counts[4];              /**< Number of rules for each key type. */

    bool enable_burst_handling;         /**< Whether burst handling is enabled. */
    size_t burst_multiplier;            /**< Multiplier for burst capacity. */
    size_t burst_window_seconds;        /**< Time window for burst handling. */

    bool enable_dynamic_rules;          /**< Whether dynamic rule adjustment is enabled. */
    double threshold_for_tightening;    /**< Traffic threshold for tightening rules. */
    double threshold_for_relaxing;      /**< Traffic threshold for relaxing rules. */

    size_t total_requests;              /**< Total number of requests processed. */
    size_t allowed_requests;            /**< Number of allowed requests. */
    size_t denied_requests;             /**< Number of denied requests. */
};

// Forward declarations of internal functions
static unsigned long hash_key(const char* str);
static void free_client_entry(client_entry_t* entry);
static void free_rule_entry(rule_entry_t* entry);
static client_entry_t* find_or_create_client_entry(mcp_advanced_rate_limiter_t* limiter,
                                                 const char* key,
                                                 mcp_rate_limit_key_type_t key_type,
                                                 bool* created);
static bool check_fixed_window(client_entry_t* entry, const mcp_rate_limit_rule_t* rule, time_t current_time);
static bool check_sliding_window(client_entry_t* entry, const mcp_rate_limit_rule_t* rule, time_t current_time);
static bool check_token_bucket(client_entry_t* entry, const mcp_rate_limit_rule_t* rule, time_t current_time);
static bool check_leaky_bucket(client_entry_t* entry, const mcp_rate_limit_rule_t* rule, time_t current_time);
static bool initialize_algorithm_data(client_entry_t* entry, const mcp_rate_limit_rule_t* rule);
static void cleanup_algorithm_data(client_entry_t* entry);
static const mcp_rate_limit_rule_t* find_matching_rule(mcp_advanced_rate_limiter_t* limiter,
                                                     mcp_rate_limit_key_type_t key_type,
                                                     const char* key);
static bool resize_client_table(mcp_advanced_rate_limiter_t* limiter, size_t new_capacity);
static bool key_matches_pattern(const char* key, const char* pattern);

/**
 * @internal
 * @brief Simple string hash function (djb2).
 */
static unsigned long hash_key(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

/**
 * @internal
 * @brief Frees a client entry and its associated data.
 */
static void free_client_entry(client_entry_t* entry) {
    if (!entry)
        return;

    // Free the key string
    free(entry->key);

    // Clean up algorithm-specific data
    cleanup_algorithm_data(entry);

    // Free the entry itself
    free(entry);
}

/**
 * @internal
 * @brief Frees a rule entry and its associated data.
 */
static void free_rule_entry(rule_entry_t* entry) {
    if (!entry)
        return;

    // Free the key pattern string
    free(entry->rule.key_pattern);

    // Free the entry itself
    free(entry);
}

/**
 * @internal
 * @brief Cleans up algorithm-specific data in a client entry.
 */
static void cleanup_algorithm_data(client_entry_t* entry) {
    if (!entry)
        return;

    // Clean up based on algorithm type
    switch (entry->algorithm) {
        case MCP_RATE_LIMIT_SLIDING_WINDOW:
            // Free the circular buffer of request times
            free(entry->data.sliding_window.request_times);
            break;

        case MCP_RATE_LIMIT_FIXED_WINDOW:
        case MCP_RATE_LIMIT_TOKEN_BUCKET:
        case MCP_RATE_LIMIT_LEAKY_BUCKET:
            // No dynamic memory to free for these algorithms
            break;
    }
}

/**
 * @internal
 * @brief Initializes algorithm-specific data in a client entry.
 */
static bool initialize_algorithm_data(client_entry_t* entry, const mcp_rate_limit_rule_t* rule) {
    if (!entry || !rule)
        return false;

    // Store the algorithm type
    entry->algorithm = rule->algorithm;

    // Initialize based on algorithm type
    switch (rule->algorithm) {
        case MCP_RATE_LIMIT_FIXED_WINDOW:
            entry->data.fixed_window.window_start_time = time(NULL);
            entry->data.fixed_window.request_count = 0;
            break;

        case MCP_RATE_LIMIT_SLIDING_WINDOW:
            entry->data.sliding_window.last_request_time = time(NULL);
            entry->data.sliding_window.request_count = 0;
            entry->data.sliding_window.buffer_size = rule->max_requests_per_window;
            entry->data.sliding_window.buffer_pos = 0;

            // Allocate the circular buffer
            entry->data.sliding_window.request_times =
                (double*)calloc(rule->max_requests_per_window, sizeof(double));
            if (!entry->data.sliding_window.request_times) {
                return false;
            }
            break;

        case MCP_RATE_LIMIT_TOKEN_BUCKET:
            entry->data.token_bucket.tokens = (double)rule->max_tokens; // Start with a full bucket
            entry->data.token_bucket.last_refill_time = time(NULL);
            entry->data.token_bucket.tokens_per_second = rule->tokens_per_second;
            entry->data.token_bucket.max_tokens = rule->max_tokens;
            break;

        case MCP_RATE_LIMIT_LEAKY_BUCKET:
            entry->data.leaky_bucket.water_level = 0; // Start with an empty bucket
            entry->data.leaky_bucket.last_leak_time = time(NULL);
            entry->data.leaky_bucket.leak_rate_per_second = rule->leak_rate_per_second;
            entry->data.leaky_bucket.burst_capacity = rule->burst_capacity;
            break;
    }

    return true;
}

/**
 * @internal
 * @brief Checks if a key matches a pattern.
 *
 * Simple pattern matching that supports:
 * - Exact match
 * - Prefix match with * at the end (e.g., "192.168.*")
 * - Suffix match with * at the beginning (e.g., "*.example.com")
 * - Contains match with * at both ends (e.g., "*admin*")
 */
static bool key_matches_pattern(const char* key, const char* pattern) {
    if (!key || !pattern)
        return false;

    // Exact match
    if (strcmp(pattern, key) == 0)
        return true;

    size_t pattern_len = strlen(pattern);
    size_t key_len = strlen(key);

    // Check for prefix match: pattern ends with *
    if (pattern_len > 1 && pattern[pattern_len - 1] == '*') {
        // Compare all characters except the trailing *
        return strncmp(key, pattern, pattern_len - 1) == 0;
    }

    // Check for suffix match: pattern starts with *
    if (pattern_len > 1 && pattern[0] == '*') {
        // Compare the suffix
        size_t suffix_len = pattern_len - 1;
        if (key_len >= suffix_len) {
            return strcmp(key + (key_len - suffix_len), pattern + 1) == 0;
        }
    }

    // Check for contains match: pattern starts and ends with *
    if (pattern_len > 2 && pattern[0] == '*' && pattern[pattern_len - 1] == '*') {
        // Extract the substring to search for
        char* substring = (char*)malloc(pattern_len - 1);
        if (!substring)
            return false;

        // Copy the substring (without the * at start and end)
        strncpy(substring, pattern + 1, pattern_len - 2);
        substring[pattern_len - 2] = '\0';

        bool result = strstr(key, substring) != NULL;
        free(substring);
        return result;
    }

    return false;
}

/**
 * @internal
 * @brief Finds the matching rule for a given key and key type.
 */
static const mcp_rate_limit_rule_t* find_matching_rule(mcp_advanced_rate_limiter_t* limiter,
                                                     mcp_rate_limit_key_type_t key_type,
                                                     const char* key) {
    if (!limiter || !key || key_type >= 4)
        return NULL;

    // Get the rule list for this key type
    rule_entry_t* rule = limiter->rules[key_type];
    const mcp_rate_limit_rule_t* best_match = NULL;
    int highest_priority = -1;

    // Iterate through rules to find the best match
    while (rule) {
        // Check if the key matches the rule's pattern
        if (rule->rule.key_pattern == NULL || key_matches_pattern(key, rule->rule.key_pattern)) {
            // If this rule has higher priority than our current best match, use it
            if (rule->rule.priority > highest_priority) {
                best_match = &rule->rule;
                highest_priority = rule->rule.priority;
            }
        }
        rule = rule->next;
    }

    return best_match;
}

/**
 * @internal
 * @brief Resizes the client hash table.
 */
static bool resize_client_table(mcp_advanced_rate_limiter_t* limiter, size_t new_capacity) {
    if (!limiter || new_capacity == 0)
        return false;

    // Allocate new buckets
    client_entry_t** new_buckets = (client_entry_t**)calloc(new_capacity, sizeof(client_entry_t*));
    if (!new_buckets)
        return false;

    // Rehash all entries
    for (size_t i = 0; i < limiter->client_capacity; i++) {
        client_entry_t* entry = limiter->client_buckets[i];
        while (entry) {
            // Save next pointer before we change it
            client_entry_t* next = entry->next;

            // Compute new bucket index
            unsigned long hash = hash_key(entry->key);
            size_t new_index = hash % new_capacity;

            // Insert at the beginning of the new bucket
            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;

            // Move to next entry in old bucket
            entry = next;
        }
    }

    // Free old buckets and update limiter
    free(limiter->client_buckets);
    limiter->client_buckets = new_buckets;
    limiter->client_capacity = new_capacity;

    return true;
}

/**
 * @internal
 * @brief Finds or creates a client entry.
 */
static client_entry_t* find_or_create_client_entry(mcp_advanced_rate_limiter_t* limiter,
                                                 const char* key,
                                                 mcp_rate_limit_key_type_t key_type,
                                                 bool* created) {
    if (!limiter || !key || key_type >= 4)
        return NULL;

    // Compute hash and bucket index
    unsigned long hash = hash_key(key);
    size_t index = hash % limiter->client_capacity;

    // Set created flag to false initially
    if (created)
        *created = false;

    // Look for existing entry
    client_entry_t* entry = limiter->client_buckets[index];
    while (entry) {
        if (entry->key_type == key_type && strcmp(entry->key, key) == 0) {
            // Found existing entry
            return entry;
        }
        entry = entry->next;
    }

    // Find the matching rule for this key
    const mcp_rate_limit_rule_t* rule = find_matching_rule(limiter, key_type, key);
    if (!rule) {
        // No matching rule, can't create entry
        return NULL;
    }

    // Create new entry
    entry = (client_entry_t*)calloc(1, sizeof(client_entry_t));
    if (!entry)
        return NULL;

    // Initialize entry
    entry->key = mcp_strdup(key);
    if (!entry->key) {
        free(entry);
        return NULL;
    }
    entry->key_type = key_type;

    // Initialize algorithm-specific data
    if (!initialize_algorithm_data(entry, rule)) {
        free(entry->key);
        free(entry);
        return NULL;
    }

    // Insert at the beginning of the bucket
    entry->next = limiter->client_buckets[index];
    limiter->client_buckets[index] = entry;

    // Update client count
    limiter->client_count++;
    if (limiter->client_count > limiter->peak_client_count) {
        limiter->peak_client_count = limiter->client_count;
    }

    // Check if we need to resize the hash table
    double load_factor = (double)limiter->client_count / (double)limiter->client_capacity;
    if (load_factor > RATE_LIMIT_HASH_TABLE_MAX_LOAD_FACTOR) {
        // Resize to double the capacity
        resize_client_table(limiter, limiter->client_capacity * 2);
    }

    // Set created flag to true
    if (created)
        *created = true;

    return entry;
}

/**
 * @internal
 * @brief Checks if a request is allowed using the fixed window algorithm.
 */
static bool check_fixed_window(client_entry_t* entry, const mcp_rate_limit_rule_t* rule, time_t current_time) {
    if (!entry || !rule)
        return false;

    fixed_window_data_t* data = &entry->data.fixed_window;

    // Check if we need to start a new window
    if (current_time - data->window_start_time >= (time_t)rule->window_seconds) {
        // Start a new window
        data->window_start_time = current_time;
        data->request_count = 0;
    }

    // Check if the request is allowed
    if (data->request_count < rule->max_requests_per_window) {
        // Allow the request and increment the counter
        data->request_count++;
        return true;
    }

    // Request exceeds the limit
    return false;
}

/**
 * @internal
 * @brief Checks if a request is allowed using the sliding window algorithm.
 */
static bool check_sliding_window(client_entry_t* entry, const mcp_rate_limit_rule_t* rule, time_t current_time) {
    if (!entry || !rule)
        return false;

    sliding_window_data_t* data = &entry->data.sliding_window;

    // Update the last request time
    data->last_request_time = current_time;

    // Remove expired requests from the window
    size_t active_requests = 0;
    for (size_t i = 0; i < data->buffer_size; i++) {
        if (data->request_times[i] > 0 &&
            current_time - (time_t)data->request_times[i] < (time_t)rule->window_seconds) {
            active_requests++;
        } else {
            data->request_times[i] = 0; // Clear expired request
        }
    }

    // Check if the request is allowed
    if (active_requests < rule->max_requests_per_window) {
        // Allow the request and add it to the window
        data->request_times[data->buffer_pos] = (double)current_time;
        data->buffer_pos = (data->buffer_pos + 1) % data->buffer_size;
        data->request_count = active_requests + 1;
        return true;
    }

    // Request exceeds the limit
    return false;
}

/**
 * @internal
 * @brief Checks if a request is allowed using the token bucket algorithm.
 */
static bool check_token_bucket(client_entry_t* entry, const mcp_rate_limit_rule_t* rule, time_t current_time) {
    if (!entry || !rule)
        return false;

    token_bucket_data_t* data = &entry->data.token_bucket;

    // Calculate time since last refill
    double elapsed_seconds = difftime(current_time, data->last_refill_time);

    // Refill tokens based on elapsed time
    if (elapsed_seconds > 0) {
        double new_tokens = elapsed_seconds * data->tokens_per_second;
        data->tokens = fmin(data->tokens + new_tokens, (double)data->max_tokens);
        data->last_refill_time = current_time;
    }

    // Check if we have enough tokens for this request
    if (data->tokens >= 1.0) {
        // Consume one token and allow the request
        data->tokens -= 1.0;
        return true;
    }

    // Not enough tokens, deny the request
    return false;
}

/**
 * @internal
 * @brief Checks if a request is allowed using the leaky bucket algorithm.
 */
static bool check_leaky_bucket(client_entry_t* entry, const mcp_rate_limit_rule_t* rule, time_t current_time) {
    if (!entry || !rule)
        return false;

    leaky_bucket_data_t* data = &entry->data.leaky_bucket;

    // Calculate time since last leak
    double elapsed_seconds = difftime(current_time, data->last_leak_time);

    // Leak water based on elapsed time
    if (elapsed_seconds > 0) {
        double leaked = elapsed_seconds * data->leak_rate_per_second;
        data->water_level = fmax(0.0, data->water_level - leaked);
        data->last_leak_time = current_time;
    }

    // Check if adding this request would overflow the bucket
    if (data->water_level + 1.0 <= (double)data->burst_capacity) {
        // Add water for this request and allow it
        data->water_level += 1.0;
        return true;
    }

    // Bucket would overflow, deny the request
    return false;
}

// Public API implementation

mcp_advanced_rate_limiter_t* mcp_advanced_rate_limiter_create(const mcp_advanced_rate_limiter_config_t* config) {
    // Allocate the rate limiter structure
    mcp_advanced_rate_limiter_t* limiter = (mcp_advanced_rate_limiter_t*)calloc(1, sizeof(mcp_advanced_rate_limiter_t));
    if (!limiter) {
        mcp_log_error("Failed to allocate memory for advanced rate limiter");
        return NULL;
    }

    // Create read-write lock
    limiter->lock = mcp_rwlock_create();
    if (!limiter->lock) {
        mcp_log_error("Failed to create read-write lock for advanced rate limiter");
        free(limiter);
        return NULL;
    }

    // Set default configuration values
    size_t capacity_hint = DEFAULT_CAPACITY_HINT;
    if (config && config->capacity_hint > 0) {
        capacity_hint = config->capacity_hint;
    }

    // Initialize client hash table
    limiter->client_capacity = capacity_hint * RATE_LIMIT_HASH_TABLE_CAPACITY_FACTOR;
    if (limiter->client_capacity < 16)
        limiter->client_capacity = 16; // Minimum capacity

    limiter->client_buckets = (client_entry_t**)calloc(limiter->client_capacity, sizeof(client_entry_t*));
    if (!limiter->client_buckets) {
        mcp_log_error("Failed to allocate memory for client hash table");
        mcp_rwlock_destroy(limiter->lock);
        free(limiter);
        return NULL;
    }

    // Initialize rule lists
    for (int i = 0; i < 4; i++) {
        limiter->rules[i] = NULL;
        limiter->rule_counts[i] = 0;
    }

    // Set burst handling configuration
    if (config) {
        limiter->enable_burst_handling = config->enable_burst_handling;
        limiter->burst_multiplier = config->burst_multiplier > 0 ? config->burst_multiplier : DEFAULT_BURST_MULTIPLIER;
        limiter->burst_window_seconds = config->burst_window_seconds > 0 ? config->burst_window_seconds : DEFAULT_BURST_WINDOW_SECONDS;

        // Set dynamic rules configuration
        limiter->enable_dynamic_rules = config->enable_dynamic_rules;
        limiter->threshold_for_tightening = config->threshold_for_tightening > 0 ?
                                          config->threshold_for_tightening : DEFAULT_THRESHOLD_TIGHTENING;
        limiter->threshold_for_relaxing = config->threshold_for_relaxing > 0 ?
                                        config->threshold_for_relaxing : DEFAULT_THRESHOLD_RELAXING;
    } else {
        // Default values
        limiter->enable_burst_handling = false;
        limiter->burst_multiplier = DEFAULT_BURST_MULTIPLIER;
        limiter->burst_window_seconds = DEFAULT_BURST_WINDOW_SECONDS;
        limiter->enable_dynamic_rules = false;
        limiter->threshold_for_tightening = DEFAULT_THRESHOLD_TIGHTENING;
        limiter->threshold_for_relaxing = DEFAULT_THRESHOLD_RELAXING;
    }

    // Initialize statistics
    limiter->total_requests = 0;
    limiter->allowed_requests = 0;
    limiter->denied_requests = 0;
    limiter->client_count = 0;
    limiter->peak_client_count = 0;

    mcp_log_info("Advanced rate limiter created with capacity %zu", limiter->client_capacity);
    return limiter;
}

void mcp_advanced_rate_limiter_destroy(mcp_advanced_rate_limiter_t* limiter) {
    if (!limiter)
        return;

    // Acquire write lock
    mcp_rwlock_write_lock(limiter->lock);

    // Free all client entries
    for (size_t i = 0; i < limiter->client_capacity; i++) {
        client_entry_t* entry = limiter->client_buckets[i];
        while (entry) {
            client_entry_t* next = entry->next;
            free_client_entry(entry);
            entry = next;
        }
    }

    // Free all rule entries
    for (int i = 0; i < 4; i++) {
        rule_entry_t* rule = limiter->rules[i];
        while (rule) {
            rule_entry_t* next = rule->next;
            free_rule_entry(rule);
            rule = next;
        }
    }

    // Free client buckets
    free(limiter->client_buckets);

    // Release lock and destroy it
    mcp_rwlock_write_unlock(limiter->lock);
    mcp_rwlock_destroy(limiter->lock);

    // Free the limiter itself
    free(limiter);

    mcp_log_info("Advanced rate limiter destroyed");
}

bool mcp_advanced_rate_limiter_add_rule(mcp_advanced_rate_limiter_t* limiter, const mcp_rate_limit_rule_t* rule) {
    if (!limiter || !rule || rule->key_type >= 4) {
        mcp_log_error("Invalid parameters for adding rate limit rule");
        return false;
    }

    // Check if we've reached the maximum number of rules for this key type
    if (limiter->rule_counts[rule->key_type] >= MAX_RULES_PER_KEY_TYPE) {
        mcp_log_error("Maximum number of rules reached for key type %d", rule->key_type);
        return false;
    }

    // Acquire write lock
    mcp_rwlock_write_lock(limiter->lock);

    // Create a new rule entry
    rule_entry_t* entry = (rule_entry_t*)malloc(sizeof(rule_entry_t));
    if (!entry) {
        mcp_log_error("Failed to allocate memory for rule entry");
        mcp_rwlock_write_unlock(limiter->lock);
        return false;
    }

    // Copy the rule
    memcpy(&entry->rule, rule, sizeof(mcp_rate_limit_rule_t));

    // Duplicate the key pattern if it exists
    if (rule->key_pattern) {
        entry->rule.key_pattern = mcp_strdup(rule->key_pattern);
        if (!entry->rule.key_pattern) {
            mcp_log_error("Failed to duplicate key pattern");
            free(entry);
            mcp_rwlock_write_unlock(limiter->lock);
            return false;
        }
    } else {
        entry->rule.key_pattern = NULL;
    }

    // Add the rule to the list for this key type
    entry->next = limiter->rules[rule->key_type];
    limiter->rules[rule->key_type] = entry;
    limiter->rule_counts[rule->key_type]++;

    // Release lock
    mcp_rwlock_write_unlock(limiter->lock);

    mcp_log_info("Added rate limit rule for key type %d with priority %d", rule->key_type, rule->priority);
    return true;
}

bool mcp_advanced_rate_limiter_remove_rule(mcp_advanced_rate_limiter_t* limiter,
                                          mcp_rate_limit_key_type_t key_type,
                                          const char* key_pattern) {
    if (!limiter || key_type >= 4) {
        mcp_log_error("Invalid parameters for removing rate limit rule");
        return false;
    }

    // Acquire write lock
    mcp_rwlock_write_lock(limiter->lock);

    rule_entry_t* rule = limiter->rules[key_type];
    rule_entry_t* prev = NULL;
    bool found = false;

    // Find the rule to remove
    while (rule) {
        bool match = false;

        // Match based on key pattern
        if (key_pattern == NULL && rule->rule.key_pattern == NULL) {
            match = true;
        } else if (key_pattern != NULL && rule->rule.key_pattern != NULL) {
            match = (strcmp(key_pattern, rule->rule.key_pattern) == 0);
        }

        if (match) {
            // Remove the rule from the list
            if (prev) {
                prev->next = rule->next;
            } else {
                limiter->rules[key_type] = rule->next;
            }

            // Free the rule
            free_rule_entry(rule);
            limiter->rule_counts[key_type]--;
            found = true;
            break;
        }

        prev = rule;
        rule = rule->next;
    }

    // Release lock
    mcp_rwlock_write_unlock(limiter->lock);

    if (found) {
        mcp_log_info("Removed rate limit rule for key type %d", key_type);
    } else {
        mcp_log_warn("Rate limit rule not found for key type %d", key_type);
    }

    return found;
}

bool mcp_advanced_rate_limiter_check(mcp_advanced_rate_limiter_t* limiter,
                                    const char* ip_address,
                                    const char* user_id,
                                    const char* api_key,
                                    const char* custom_key) {
    if (!limiter || (!ip_address && !user_id && !api_key && !custom_key)) {
        mcp_log_error("Invalid parameters for rate limit check");
        return false;
    }

    // Increment total requests counter
    limiter->total_requests++;

    // Get current time
    time_t current_time = time(NULL);
    bool allowed = false;

    // Acquire read lock
    mcp_rwlock_read_lock(limiter->lock);

    // Check each key type in order of priority
    if (api_key) {
        // Check API key first (highest priority)
        client_entry_t* entry = find_or_create_client_entry(limiter, api_key, MCP_RATE_LIMIT_KEY_API_KEY, NULL);
        if (entry) {
            const mcp_rate_limit_rule_t* rule = find_matching_rule(limiter, MCP_RATE_LIMIT_KEY_API_KEY, api_key);
            if (rule) {
                // Check using the appropriate algorithm
                switch (rule->algorithm) {
                    case MCP_RATE_LIMIT_FIXED_WINDOW:
                        allowed = check_fixed_window(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_SLIDING_WINDOW:
                        allowed = check_sliding_window(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_TOKEN_BUCKET:
                        allowed = check_token_bucket(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_LEAKY_BUCKET:
                        allowed = check_leaky_bucket(entry, rule, current_time);
                        break;
                }

                // If allowed, we're done
                if (allowed) {
                    mcp_rwlock_read_unlock(limiter->lock);
                    limiter->allowed_requests++;
                    return true;
                }
            }
        }
    }

    if (user_id) {
        // Check user ID next
        client_entry_t* entry = find_or_create_client_entry(limiter, user_id, MCP_RATE_LIMIT_KEY_USER_ID, NULL);
        if (entry) {
            const mcp_rate_limit_rule_t* rule = find_matching_rule(limiter, MCP_RATE_LIMIT_KEY_USER_ID, user_id);
            if (rule) {
                // Check using the appropriate algorithm
                switch (rule->algorithm) {
                    case MCP_RATE_LIMIT_FIXED_WINDOW:
                        allowed = check_fixed_window(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_SLIDING_WINDOW:
                        allowed = check_sliding_window(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_TOKEN_BUCKET:
                        allowed = check_token_bucket(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_LEAKY_BUCKET:
                        allowed = check_leaky_bucket(entry, rule, current_time);
                        break;
                }

                // If allowed, we're done
                if (allowed) {
                    mcp_rwlock_read_unlock(limiter->lock);
                    limiter->allowed_requests++;
                    return true;
                }
            }
        }
    }

    if (ip_address) {
        // Check IP address next
        client_entry_t* entry = find_or_create_client_entry(limiter, ip_address, MCP_RATE_LIMIT_KEY_IP, NULL);
        if (entry) {
            const mcp_rate_limit_rule_t* rule = find_matching_rule(limiter, MCP_RATE_LIMIT_KEY_IP, ip_address);
            if (rule) {
                // Check using the appropriate algorithm
                switch (rule->algorithm) {
                    case MCP_RATE_LIMIT_FIXED_WINDOW:
                        allowed = check_fixed_window(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_SLIDING_WINDOW:
                        allowed = check_sliding_window(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_TOKEN_BUCKET:
                        allowed = check_token_bucket(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_LEAKY_BUCKET:
                        allowed = check_leaky_bucket(entry, rule, current_time);
                        break;
                }

                // If allowed, we're done
                if (allowed) {
                    mcp_rwlock_read_unlock(limiter->lock);
                    limiter->allowed_requests++;
                    return true;
                }
            }
        }
    }

    if (custom_key) {
        // Check custom key last
        client_entry_t* entry = find_or_create_client_entry(limiter, custom_key, MCP_RATE_LIMIT_KEY_CUSTOM, NULL);
        if (entry) {
            const mcp_rate_limit_rule_t* rule = find_matching_rule(limiter, MCP_RATE_LIMIT_KEY_CUSTOM, custom_key);
            if (rule) {
                // Check using the appropriate algorithm
                switch (rule->algorithm) {
                    case MCP_RATE_LIMIT_FIXED_WINDOW:
                        allowed = check_fixed_window(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_SLIDING_WINDOW:
                        allowed = check_sliding_window(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_TOKEN_BUCKET:
                        allowed = check_token_bucket(entry, rule, current_time);
                        break;
                    case MCP_RATE_LIMIT_LEAKY_BUCKET:
                        allowed = check_leaky_bucket(entry, rule, current_time);
                        break;
                }

                // If allowed, we're done
                if (allowed) {
                    mcp_rwlock_read_unlock(limiter->lock);
                    limiter->allowed_requests++;
                    return true;
                }
            }
        }
    }

    // Release lock
    mcp_rwlock_read_unlock(limiter->lock);

    // If we get here, the request is denied
    limiter->denied_requests++;
    return false;
}

bool mcp_advanced_rate_limiter_get_stats(mcp_advanced_rate_limiter_t* limiter,
                                        mcp_advanced_rate_limiter_stats_t* stats) {
    if (!limiter || !stats) {
        mcp_log_error("Invalid parameters for getting rate limiter stats");
        return false;
    }

    // Acquire read lock
    mcp_rwlock_read_lock(limiter->lock);

    // Fill in the statistics
    stats->total_requests = limiter->total_requests;
    stats->allowed_requests = limiter->allowed_requests;
    stats->denied_requests = limiter->denied_requests;
    stats->active_clients = limiter->client_count;
    stats->peak_clients = limiter->peak_client_count;

    // Calculate rule count
    stats->rule_count = 0;
    for (int i = 0; i < 4; i++) {
        stats->rule_count += limiter->rule_counts[i];
    }

    // Calculate denial rate
    if (limiter->total_requests > 0) {
        stats->denial_rate = (double)limiter->denied_requests / (double)limiter->total_requests;
    } else {
        stats->denial_rate = 0.0;
    }

    // Release lock
    mcp_rwlock_read_unlock(limiter->lock);

    return true;
}

bool mcp_advanced_rate_limiter_clear_data(mcp_advanced_rate_limiter_t* limiter) {
    if (!limiter) {
        mcp_log_error("Invalid parameter for clearing rate limiter data");
        return false;
    }

    // Acquire write lock
    mcp_rwlock_write_lock(limiter->lock);

    // Free all client entries
    for (size_t i = 0; i < limiter->client_capacity; i++) {
        client_entry_t* entry = limiter->client_buckets[i];
        while (entry) {
            client_entry_t* next = entry->next;
            free_client_entry(entry);
            entry = next;
        }
        limiter->client_buckets[i] = NULL;
    }

    // Reset client count
    limiter->client_count = 0;

    // Reset statistics
    limiter->total_requests = 0;
    limiter->allowed_requests = 0;
    limiter->denied_requests = 0;

    // Release lock
    mcp_rwlock_write_unlock(limiter->lock);

    mcp_log_info("Advanced rate limiter data cleared");
    return true;
}

mcp_rate_limit_rule_t mcp_advanced_rate_limiter_create_default_rule(
    mcp_rate_limit_key_type_t key_type,
    mcp_rate_limit_algorithm_t algorithm,
    size_t window_seconds,
    size_t max_requests_per_window) {

    mcp_rate_limit_rule_t rule;
    memset(&rule, 0, sizeof(mcp_rate_limit_rule_t));

    rule.key_type = key_type;
    rule.algorithm = algorithm;
    rule.window_seconds = window_seconds;
    rule.max_requests_per_window = max_requests_per_window;
    rule.key_pattern = NULL;
    rule.priority = 0;

    return rule;
}

mcp_rate_limit_rule_t mcp_advanced_rate_limiter_create_token_bucket_rule(
    mcp_rate_limit_key_type_t key_type,
    double tokens_per_second,
    size_t max_tokens) {

    mcp_rate_limit_rule_t rule;
    memset(&rule, 0, sizeof(mcp_rate_limit_rule_t));

    rule.key_type = key_type;
    rule.algorithm = MCP_RATE_LIMIT_TOKEN_BUCKET;
    rule.tokens_per_second = tokens_per_second;
    rule.max_tokens = max_tokens;
    rule.key_pattern = NULL;
    rule.priority = 0;

    return rule;
}

mcp_rate_limit_rule_t mcp_advanced_rate_limiter_create_leaky_bucket_rule(
    mcp_rate_limit_key_type_t key_type,
    double leak_rate_per_second,
    size_t burst_capacity) {
    mcp_rate_limit_rule_t rule;
    memset(&rule, 0, sizeof(mcp_rate_limit_rule_t));

    rule.key_type = key_type;
    rule.algorithm = MCP_RATE_LIMIT_LEAKY_BUCKET;
    rule.leak_rate_per_second = leak_rate_per_second;
    rule.burst_capacity = burst_capacity;
    rule.key_pattern = NULL;
    rule.priority = 0;

    return rule;
}
