#ifndef MCP_RATE_LIMITER_H
#define MCP_RATE_LIMITER_H

#include <stddef.h>
#include <time.h>
#include <stdbool.h>

#ifdef _WIN32
// Forward declare HANDLE type without including windows.h in the header
typedef void* HANDLE;
#else
#include <pthread.h>
#endif

// Forward declaration
typedef struct mcp_rate_limiter mcp_rate_limiter_t;

/**
 * @brief Creates a new rate limiter instance.
 *
 * @param capacity The approximate maximum number of unique client identifiers to track.
 * @param window_seconds The time window duration in seconds for rate limiting.
 * @param max_requests_per_window The maximum number of requests allowed per client within the window.
 * @return A pointer to the newly created rate limiter, or NULL on failure.
 *         The caller is responsible for destroying the limiter using mcp_rate_limiter_destroy().
 */
mcp_rate_limiter_t* mcp_rate_limiter_create(size_t capacity, size_t window_seconds, size_t max_requests_per_window);

/**
 * @brief Destroys the rate limiter and frees all associated memory.
 *
 * @param limiter The rate limiter instance to destroy.
 */
void mcp_rate_limiter_destroy(mcp_rate_limiter_t* limiter);

/**
 * @brief Checks if a request from a given client identifier is allowed based on the rate limit.
 *
 * This function is thread-safe. It increments the request count for the client
 * if the request is allowed within the current time window.
 *
 * @param limiter The rate limiter instance.
 * @param client_id A string uniquely identifying the client (e.g., IP address).
 * @return True if the request is allowed, false if the client has exceeded the rate limit.
 */
bool mcp_rate_limiter_check(mcp_rate_limiter_t* limiter, const char* client_id);

#endif // MCP_RATE_LIMITER_H
