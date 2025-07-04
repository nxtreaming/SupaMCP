#include "internal/connection_pool_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

// Global DNS cache instance
MCP_CACHE_ALIGNED dns_cache_t g_dns_cache = {0};

/**
 * @brief Initializes the DNS cache.
 *
 * This function initializes the global DNS cache, creating the mutex and
 * initializing all cache entries.
 */
void dns_cache_init() {
    // Check if already initialized
    if (g_dns_cache.initialized) {
        return;
    }

    // Create mutex for thread-safe access
    g_dns_cache.mutex = mcp_mutex_create();
    if (!g_dns_cache.mutex) {
        mcp_log_error("Failed to create DNS cache mutex");
        return;
    }

    // Create read-write lock for better concurrency
    g_dns_cache.rwlock = mcp_rwlock_create();
    if (!g_dns_cache.rwlock) {
        mcp_log_error("Failed to create DNS cache read-write lock");
        mcp_mutex_destroy(g_dns_cache.mutex);
        return;
    }

    // Initialize statistics
    g_dns_cache.hits = 0;
    g_dns_cache.misses = 0;
    g_dns_cache.evictions = 0;

    // Initialize all cache entries
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        g_dns_cache.entries[i].hostname[0] = '\0';
        g_dns_cache.entries[i].addr_info = NULL;
        g_dns_cache.entries[i].timestamp = 0;
        g_dns_cache.entries[i].ref_count = 0;
        g_dns_cache.entries[i].hit_count = 0;
        g_dns_cache.entries[i].mutex = mcp_mutex_create();
        if (!g_dns_cache.entries[i].mutex) {
            mcp_log_error("Failed to create DNS cache entry mutex");
            // Clean up previously created mutexes
            for (int j = 0; j < i; j++) {
                mcp_mutex_destroy(g_dns_cache.entries[j].mutex);
            }
            mcp_rwlock_destroy(g_dns_cache.rwlock);
            mcp_mutex_destroy(g_dns_cache.mutex);
            return;
        }
    }

    g_dns_cache.initialized = true;
    mcp_log_info("DNS cache initialized with %d entries", DNS_CACHE_SIZE);
}

/**
 * @brief Cleans up the DNS cache.
 *
 * This function frees all resources used by the DNS cache, including
 * all cached DNS entries and mutexes.
 */
void dns_cache_cleanup() {
    if (!g_dns_cache.initialized) {
        return;
    }

    mcp_mutex_lock(g_dns_cache.mutex);

    // Clean up all cache entries
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        mcp_mutex_lock(g_dns_cache.entries[i].mutex);

        g_dns_cache.entries[i].hostname[0] = '\0';
        if (g_dns_cache.entries[i].addr_info) {
            freeaddrinfo(g_dns_cache.entries[i].addr_info);
            g_dns_cache.entries[i].addr_info = NULL;
        }

        g_dns_cache.entries[i].timestamp = 0;
        g_dns_cache.entries[i].ref_count = 0;
        g_dns_cache.entries[i].hit_count = 0;

        mcp_mutex_unlock(g_dns_cache.entries[i].mutex);
        mcp_mutex_destroy(g_dns_cache.entries[i].mutex);
    }

    mcp_mutex_unlock(g_dns_cache.mutex);
    mcp_mutex_destroy(g_dns_cache.mutex);
    mcp_rwlock_destroy(g_dns_cache.rwlock);

    g_dns_cache.initialized = false;
    mcp_log_info("DNS cache cleaned up");
}

/**
 * @brief Clears all entries from the DNS cache.
 *
 * This function removes all entries from the DNS cache, freeing the
 * associated resources but keeping the cache structure intact.
 */
void dns_cache_clear() {
    if (!g_dns_cache.initialized) {
        return;
    }

    // Use write lock for better concurrency
    mcp_rwlock_write_lock(g_dns_cache.rwlock);

    // Clear all cache entries
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        mcp_mutex_lock(g_dns_cache.entries[i].mutex);

        // Only clear entries with no references
        if (g_dns_cache.entries[i].hostname[0] != '\0' && g_dns_cache.entries[i].ref_count == 0) {
            g_dns_cache.entries[i].hostname[0] = '\0';
            if (g_dns_cache.entries[i].addr_info) {
                freeaddrinfo(g_dns_cache.entries[i].addr_info);
                g_dns_cache.entries[i].addr_info = NULL;
            }

            g_dns_cache.entries[i].timestamp = 0;
            g_dns_cache.entries[i].hit_count = 0;

            // Increment eviction counter
            g_dns_cache.evictions++;
        }

        mcp_mutex_unlock(g_dns_cache.entries[i].mutex);
    }

    mcp_rwlock_write_unlock(g_dns_cache.rwlock);
    mcp_log_info("DNS cache cleared (%u evictions total)", g_dns_cache.evictions);
}

/**
 * @brief Gets a cached DNS entry or creates a new one.
 *
 * This function looks up a hostname in the DNS cache. If found and not expired,
 * it returns the cached entry. Otherwise, it performs a DNS lookup and caches
 * the result.
 *
 * @param hostname The hostname to look up.
 * @param port The port number (used for cache key).
 * @param hints Address info hints for getaddrinfo.
 * @return A pointer to the address info, or NULL on failure.
 * @note The caller must call dns_cache_release when done with the result.
 */
struct addrinfo* dns_cache_get(const char* hostname, int port, const struct addrinfo* hints) {
    if (!g_dns_cache.initialized || !hostname) {
        return NULL;
    }

    char cache_key[DNS_CACHE_MAX_HOSTNAME];
    snprintf(cache_key, sizeof(cache_key), "%s:%d", hostname, port);

    time_t current_time = time(NULL);
    struct addrinfo* result = NULL;
    int empty_slot = -1;
    int oldest_slot = 0;
    int least_used_slot = 0;
    time_t oldest_time = current_time;
    uint32_t least_hits = UINT32_MAX;

    mcp_rwlock_read_lock(g_dns_cache.rwlock);

    // Look for the hostname in the cache
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        mcp_mutex_lock(g_dns_cache.entries[i].mutex);

        // Track empty slots and replacement candidates
        if (g_dns_cache.entries[i].hostname[0] == '\0') {
            if (empty_slot == -1) {
                empty_slot = i;
            }
        } else if (g_dns_cache.entries[i].ref_count == 0) {
            // Track oldest entry
            if (g_dns_cache.entries[i].timestamp < oldest_time) {
                oldest_time = g_dns_cache.entries[i].timestamp;
                oldest_slot = i;
            }

            // Track least frequently used entry
            if (g_dns_cache.entries[i].hit_count < least_hits) {
                least_hits = g_dns_cache.entries[i].hit_count;
                least_used_slot = i;
            }
        }

        // Check if this entry matches our hostname and port
        if (g_dns_cache.entries[i].hostname[0] != '\0' &&
            strcmp(g_dns_cache.entries[i].hostname, cache_key) == 0) {

            // Check if the entry is expired
            if (current_time - g_dns_cache.entries[i].timestamp > DNS_CACHE_EXPIRY) {
                // Entry is expired, will be replaced
                mcp_log_debug("DNS cache entry for %s is expired", cache_key);

                // Only clear if no references
                if (g_dns_cache.entries[i].ref_count == 0) {
                    // Clear hostname (now a fixed buffer, just set to empty string)
                    g_dns_cache.entries[i].hostname[0] = '\0';

                    if (g_dns_cache.entries[i].addr_info) {
                        freeaddrinfo(g_dns_cache.entries[i].addr_info);
                        g_dns_cache.entries[i].addr_info = NULL;
                    }

                    g_dns_cache.entries[i].timestamp = 0;
                    g_dns_cache.entries[i].hit_count = 0;
                } else {
                    // Mark as expired but don't free yet
                    g_dns_cache.entries[i].timestamp = 0;
                }
            } else {
                // Entry is valid, increment reference count and return
                g_dns_cache.entries[i].ref_count++;
                g_dns_cache.entries[i].hit_count++;
                result = g_dns_cache.entries[i].addr_info;

                // Update statistics
                g_dns_cache.hits++;

                mcp_log_debug("DNS cache hit for %s (ref_count=%d, hits=%u)",
                             cache_key, g_dns_cache.entries[i].ref_count,
                             g_dns_cache.entries[i].hit_count);
            }

            mcp_mutex_unlock(g_dns_cache.entries[i].mutex);
            break;
        }

        mcp_mutex_unlock(g_dns_cache.entries[i].mutex);
    }

    // Unlock read lock before potentially upgrading to write lock
    mcp_rwlock_read_unlock(g_dns_cache.rwlock);

    // If not found in cache, perform DNS lookup
    if (!result) {
        struct addrinfo* new_addr_info = NULL;
        char port_str[6];
        snprintf(port_str, sizeof(port_str), "%d", port);

        // Perform DNS lookup (outside of any locks)
        int rv = getaddrinfo(hostname, port_str, hints, &new_addr_info);

        if (rv != 0) {
            mcp_log_error("DNS lookup failed for %s: %s", hostname, gai_strerror(rv));
            return NULL;
        }

        // Now acquire write lock to update the cache
        mcp_rwlock_write_lock(g_dns_cache.rwlock);

        // Update statistics
        g_dns_cache.misses++;

        // Choose a slot for the new entry - prefer empty, then least used, then oldest
        int slot;
        if (empty_slot != -1) {
            slot = empty_slot;
        } else if (least_used_slot != oldest_slot) {
            // If we have a least used slot that's different from oldest, prefer it
            slot = least_used_slot;
        } else {
            slot = oldest_slot;
        }

        mcp_mutex_lock(g_dns_cache.entries[slot].mutex);

        // Free the old entry if necessary
        if (g_dns_cache.entries[slot].hostname[0] != '\0') {
            // If we're evicting an entry, increment the counter
            g_dns_cache.evictions++;
        }

        if (g_dns_cache.entries[slot].addr_info) {
            freeaddrinfo(g_dns_cache.entries[slot].addr_info);
        }

        // Store the new entry
        strncpy(g_dns_cache.entries[slot].hostname, cache_key, DNS_CACHE_MAX_HOSTNAME - 1);
        g_dns_cache.entries[slot].hostname[DNS_CACHE_MAX_HOSTNAME - 1] = '\0'; // Ensure null termination
        g_dns_cache.entries[slot].addr_info = new_addr_info;
        g_dns_cache.entries[slot].timestamp = current_time;
        g_dns_cache.entries[slot].ref_count = 1;
        g_dns_cache.entries[slot].hit_count = 1;

        result = new_addr_info;

        mcp_log_debug("DNS cache miss for %s, added to slot %d (misses=%u, evictions=%u)",
                     cache_key, slot, g_dns_cache.misses, g_dns_cache.evictions);

        mcp_mutex_unlock(g_dns_cache.entries[slot].mutex);
        mcp_rwlock_write_unlock(g_dns_cache.rwlock);
    }

    return result;
}

/**
 * @brief Releases a reference to a cached DNS entry.
 *
 * This function decrements the reference count for a cached DNS entry.
 * When the reference count reaches zero, the entry becomes eligible for
 * replacement if needed.
 *
 * @param addr_info The address info to release.
 */
void dns_cache_release(struct addrinfo* addr_info) {
    if (!g_dns_cache.initialized || !addr_info) {
        return;
    }

    // Use read lock for better concurrency
    mcp_rwlock_read_lock(g_dns_cache.rwlock);

    // Find the entry containing this addr_info
    for (int i = 0; i < DNS_CACHE_SIZE; i++) {
        mcp_mutex_lock(g_dns_cache.entries[i].mutex);

        if (g_dns_cache.entries[i].addr_info == addr_info) {
            // Found it, decrement reference count
            if (g_dns_cache.entries[i].ref_count > 0) {
                g_dns_cache.entries[i].ref_count--;
                mcp_log_debug("Released DNS cache entry for %s (ref_count=%d, hits=%u)",
                             g_dns_cache.entries[i].hostname,
                             g_dns_cache.entries[i].ref_count,
                             g_dns_cache.entries[i].hit_count);
            }

            mcp_mutex_unlock(g_dns_cache.entries[i].mutex);
            break;
        }

        mcp_mutex_unlock(g_dns_cache.entries[i].mutex);
    }

    mcp_rwlock_read_unlock(g_dns_cache.rwlock);
}
