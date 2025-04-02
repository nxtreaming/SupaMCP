#include "mcp_connection_pool.h"
#include "mcp_log.h"
#include "mcp_profiler.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h> 
#include <stdbool.h>
#include <time.h>

// Platform-specific includes for sockets and threads
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib") 
// Define pthread types for Windows compatibility if using a wrapper library,
// or use Windows native synchronization primitives (CRITICAL_SECTION, CONDITION_VARIABLE).
// Using Windows native for this example.
typedef CRITICAL_SECTION pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
#else // Linux/macOS
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h> // For close()
#include <pthread.h>
#include <errno.h> // Include errno.h
#include <fcntl.h> // For non-blocking sockets, fcntl
#include <poll.h>  // For poll()
#include <sys/time.h> // For time calculations, gettimeofday
#endif

// Define SOCKET_ERROR and INVALID_SOCKET for non-Windows for consistency
#ifndef _WIN32
#define SOCKET_ERROR (-1)
#define INVALID_SOCKET (-1)
typedef int SOCKET; // Use int for socket descriptor on POSIX
#else
// On Windows, SOCKET is already defined in winsock2.h
#endif


/**
 * @internal
 * @brief Represents a single connection stored within the pool.
 */
typedef struct mcp_pooled_connection {
    SOCKET socket_fd;                   // Use SOCKET type consistently
    time_t last_used_time;              // Timestamp when the connection was last returned to the pool
    struct mcp_pooled_connection* next; // Link for idle list
} mcp_pooled_connection_t;

/**
 * @internal
 * @brief Internal structure for the connection pool.
 */
struct mcp_connection_pool {
    char* host;                         // Target host
    int port;                           // Target port
    size_t min_connections;             // Min number of connections
    size_t max_connections;             // Max number of connections
    int idle_timeout_ms;                // Idle connection timeout
    int connect_timeout_ms;             // Timeout for establishing connections

    pthread_mutex_t mutex;              // Mutex for thread safety
    pthread_cond_t cond_var;            // Condition variable for waiting clients

    mcp_pooled_connection_t* idle_list; // Linked list of idle connections
    size_t idle_count;                  // Number of idle connections
    size_t active_count;                // Number of connections currently in use
    size_t total_count;                 // Total connections created (idle + active)

    bool shutting_down;                 // Flag indicating pool destruction is in progress

    // Optional: Thread for managing idle timeouts/min connections
    // pthread_t maintenance_thread;
};

// --- Helper Functions (Declarations) ---

// Internal function to create a new socket connection with timeout
static SOCKET create_new_connection(const char* host, int port, int connect_timeout_ms);
// Internal function to close a socket connection
static void close_connection(SOCKET socket_fd);
// Internal function to initialize mutex/cond
static int init_sync_primitives(mcp_connection_pool_t* pool);
// Internal function to destroy mutex/cond
static void destroy_sync_primitives(mcp_connection_pool_t* pool);
// Helper for strdup if not available elsewhere
#ifndef mcp_strdup
static char* mcp_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* new_s = (char*)malloc(len);
    if (new_s) {
        memcpy(new_s, s, len);
    }
    return new_s;
}
#endif


/**
 * @brief Creates a connection pool.
 */
mcp_connection_pool_t* mcp_connection_pool_create(
    const char* host, 
    int port, 
    size_t min_connections, 
    size_t max_connections,
    int idle_timeout_ms,
    int connect_timeout_ms) 
{
    // Use fprintf for initial checks as logging might not be ready
    if (!host || port <= 0 || max_connections == 0 || min_connections > max_connections) {
        fprintf(stderr, "Error: mcp_connection_pool_create invalid arguments.\n");
        return NULL;
    }

    mcp_connection_pool_t* pool = (mcp_connection_pool_t*)calloc(1, sizeof(mcp_connection_pool_t));
    if (!pool) {
        fprintf(stderr, "Error: mcp_connection_pool_create failed to allocate pool structure.\n");
        return NULL;
    }

    pool->host = mcp_strdup(host);
    if (!pool->host) {
        fprintf(stderr, "Error: mcp_connection_pool_create failed to duplicate host string.\n");
        free(pool);
        return NULL;
    }

    pool->port = port;
    pool->min_connections = min_connections;
    pool->max_connections = max_connections;
    pool->idle_timeout_ms = idle_timeout_ms;
    pool->connect_timeout_ms = connect_timeout_ms;
    pool->shutting_down = false;
    pool->idle_list = NULL;
    pool->idle_count = 0;
    pool->active_count = 0;
    pool->total_count = 0;

    if (init_sync_primitives(pool) != 0) {
        fprintf(stderr, "Error: mcp_connection_pool_create failed to initialize synchronization primitives.\n");
        free(pool->host);
        free(pool);
        return NULL;
    }

    // TODO: Pre-populate pool with min_connections (potentially in a background thread)
    // This is complex and might be better done lazily or in a separate maintenance thread.
    // For now, connections are created on demand by mcp_connection_pool_get.
    log_message(LOG_LEVEL_INFO, "Connection pool created for %s:%d (min:%zu, max:%zu).", 
            pool->host, pool->port, pool->min_connections, pool->max_connections);

    // TODO: Start maintenance thread if idle_timeout_ms > 0 or min_connections > 0

    return pool;
}

// Helper function to get current time in milliseconds
static long long get_current_time_ms() {
#ifdef _WIN32
    // GetTickCount64 is simpler and often sufficient for intervals
    return (long long)GetTickCount64(); 
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long long)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
#endif
}

// Helper function to calculate deadline for timed wait (POSIX only)
#ifndef _WIN32
static void calculate_deadline(int timeout_ms, struct timespec* deadline) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long nsec = tv.tv_usec * 1000 + (long long)(timeout_ms % 1000) * 1000000;
    deadline->tv_sec = tv.tv_sec + (timeout_ms / 1000) + (nsec / 1000000000);
    deadline->tv_nsec = nsec % 1000000000;
}
#endif

// Helper function to lock the pool mutex
static void pool_lock(mcp_connection_pool_t* pool) {
#ifdef _WIN32
    EnterCriticalSection(&pool->mutex);
#else
    pthread_mutex_lock(&pool->mutex);
#endif
}

// Helper function to unlock the pool mutex
static void pool_unlock(mcp_connection_pool_t* pool) {
#ifdef _WIN32
    LeaveCriticalSection(&pool->mutex);
#else
    pthread_mutex_unlock(&pool->mutex);
#endif
}

// Helper function to signal one waiting thread
static void pool_signal(mcp_connection_pool_t* pool) {
#ifdef _WIN32
    WakeConditionVariable(&pool->cond_var);
#else
    pthread_cond_signal(&pool->cond_var);
#endif
}

// Helper function to signal all waiting threads
static void pool_broadcast(mcp_connection_pool_t* pool) {
#ifdef _WIN32
    WakeAllConditionVariable(&pool->cond_var);
#else
    pthread_cond_broadcast(&pool->cond_var);
#endif
}

// Helper function to wait on the condition variable
// Returns 0 on success (signaled), 1 on timeout, -1 on error
static int pool_wait(mcp_connection_pool_t* pool, int timeout_ms) {
#ifdef _WIN32
    // SleepConditionVariableCS takes a relative timeout in milliseconds
    DWORD win_timeout = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    if (!SleepConditionVariableCS(&pool->cond_var, &pool->mutex, win_timeout)) {
        if (GetLastError() == ERROR_TIMEOUT) {
            return 1; // Timeout
        } else {
             log_message(LOG_LEVEL_ERROR, "SleepConditionVariableCS failed: %lu", GetLastError());
            return -1; // Error
        }
    }
    return 0; // Signaled (or spurious wakeup)
#else // POSIX
    int rc = 0;
    if (timeout_ms < 0) { // Wait indefinitely
        rc = pthread_cond_wait(&pool->cond_var, &pool->mutex);
    } else { // Wait with timeout
        struct timespec deadline;
        calculate_deadline(timeout_ms, &deadline);
        rc = pthread_cond_timedwait(&pool->cond_var, &pool->mutex, &deadline);
    }

    if (rc == ETIMEDOUT) {
        return 1; // Timeout
    } else if (rc != 0) {
        log_message(LOG_LEVEL_ERROR, "pthread_cond_timedwait/wait failed: %s", strerror(rc));
        return -1; // Error
    }
    return 0; // Signaled (or spurious wakeup)
#endif
}

/**
 * @brief Retrieves a connection handle from the pool.
 */
SOCKET mcp_connection_pool_get(mcp_connection_pool_t* pool, int timeout_ms) { // Return type is SOCKET
    if (!pool) {
        log_message(LOG_LEVEL_ERROR, "mcp_connection_pool_get: Pool is NULL.");
        return INVALID_SOCKET;
    }

    SOCKET sock = INVALID_SOCKET;
    long long start_time_ms = 0; // For tracking overall timeout
    if (timeout_ms > 0) {
        start_time_ms = get_current_time_ms();
    }

    pool_lock(pool);

    while (sock == INVALID_SOCKET) {
        if (pool->shutting_down) {
            log_message(LOG_LEVEL_WARN, "mcp_connection_pool_get: Pool is shutting down.");
            pool_unlock(pool);
            return INVALID_SOCKET;
        }

        // 1. Try to get an idle connection
        if (pool->idle_list) {
            mcp_pooled_connection_t* pooled_conn = pool->idle_list;
            pool->idle_list = pooled_conn->next;
            pool->idle_count--;
            pool->active_count++;
            sock = pooled_conn->socket_fd;
            
            // TODO: Implement idle timeout check more robustly if needed
            // time_t now = time(NULL);
            // if (pool->idle_timeout_ms > 0 && difftime(now, pooled_conn->last_used_time) * 1000 > pool->idle_timeout_ms) {
            //    log_message(LOG_LEVEL_INFO, "Closing idle connection %d due to timeout.", (int)sock);
            //    close_connection(sock);
            //    sock = INVALID_SOCKET; // Mark as invalid, loop will try again or create new
            //    pool->active_count--; // Was incremented above
            //    pool->total_count--; 
            //    free(pooled_conn);
            //    continue; // Try again immediately
            // }

            free(pooled_conn); // Free the list node structure
            log_message(LOG_LEVEL_DEBUG, "Reusing idle connection %d.", (int)sock);
            pool_unlock(pool);
            return sock;
        }

        // 2. If no idle connections, try to create a new one if allowed
        if (pool->total_count < pool->max_connections) {
            size_t current_total = pool->total_count; // Read before unlocking
            pool->total_count++; // Optimistically increment total count
            pool_unlock(pool);   // Unlock while creating connection

            log_message(LOG_LEVEL_DEBUG, "Attempting to create new connection (%zu/%zu).", current_total + 1, pool->max_connections);
            SOCKET new_sock = create_new_connection(pool->host, pool->port, pool->connect_timeout_ms);

            pool_lock(pool); // Re-lock before checking result and updating state
            if (new_sock != INVALID_SOCKET) {
                pool->active_count++;
                sock = new_sock; // Success! Loop will terminate.
                log_message(LOG_LEVEL_DEBUG, "Created new connection %d.", (int)sock);
            } else {
                pool->total_count--; // Creation failed, decrement total count
                log_message(LOG_LEVEL_WARN, "Failed to create new connection.");
                // If creation fails, we might need to wait if timeout allows
                if (timeout_ms == 0) { // Don't wait if timeout is 0
                    pool_unlock(pool);
                    return INVALID_SOCKET;
                }
                // Fall through to wait below
            }
        }

        // 3. If pool is full or creation failed, wait if timeout allows
        if (sock == INVALID_SOCKET) {
             if (timeout_ms == 0) { // Don't wait
                 log_message(LOG_LEVEL_WARN, "mcp_connection_pool_get: Pool full and timeout is 0.");
                 pool_unlock(pool);
                 return INVALID_SOCKET;
             }
             
             int wait_timeout = timeout_ms;
             if (timeout_ms > 0) {
                 long long elapsed_ms = get_current_time_ms() - start_time_ms;
                 wait_timeout = timeout_ms - (int)elapsed_ms;
                 if (wait_timeout <= 0) {
                     log_message(LOG_LEVEL_WARN, "mcp_connection_pool_get: Timed out waiting for connection.");
                     pool_unlock(pool);
                     return INVALID_SOCKET; // Overall timeout expired
                 }
             }

             log_message(LOG_LEVEL_DEBUG, "Waiting for connection (timeout: %d ms)...", wait_timeout);
             int wait_result = pool_wait(pool, wait_timeout);

             if (wait_result == 1) { // Timeout occurred during wait
                 log_message(LOG_LEVEL_WARN, "mcp_connection_pool_get: Timed out waiting for condition.");
                 pool_unlock(pool);
                 return INVALID_SOCKET;
             } else if (wait_result == -1) { // Error during wait
                 log_message(LOG_LEVEL_ERROR, "mcp_connection_pool_get: Error waiting for condition.");
                 pool_unlock(pool);
                 return INVALID_SOCKET;
             }
             // If wait_result is 0, we were signaled or had a spurious wakeup, loop continues
             log_message(LOG_LEVEL_DEBUG, "Woke up from wait, retrying get.");
        }
    } // End while loop

    pool_unlock(pool);
    return sock; // Return SOCKET directly
}

/**
 * @brief Returns a connection handle back to the pool.
 */
int mcp_connection_pool_release(mcp_connection_pool_t* pool, SOCKET connection, bool is_valid) { // connection type is SOCKET
     if (!pool || connection == INVALID_SOCKET) { // Check against INVALID_SOCKET
        log_message(LOG_LEVEL_ERROR, "mcp_connection_pool_release: Invalid arguments (pool=%p, connection=%d).", (void*)pool, (int)connection);
        return -1;
    }

    pool_lock(pool);

    // Find the connection in the active list - this requires tracking active connections,
    // which the current simple implementation doesn't do explicitly. 
    // For now, we just decrement the active count assuming the caller provides a valid active handle.
    // A more robust implementation would track active handles.
    if (pool->active_count == 0) {
         log_message(LOG_LEVEL_WARN, "mcp_connection_pool_release: Releasing connection %d but active count is zero.", (int)connection);
         // Proceeding anyway, but indicates a potential issue in usage or tracking.
    } else {
        pool->active_count--; 
    }


    if (pool->shutting_down) {
        log_message(LOG_LEVEL_INFO, "Pool shutting down, closing connection %d.", (int)connection);
        close_connection(connection); // No cast needed
        pool->total_count--;
        // No signal needed, broadcast happens in destroy
    } else if (!is_valid) {
        log_message(LOG_LEVEL_WARN, "Closing invalid connection %d.", (int)connection);
        close_connection(connection); // No cast needed
        pool->total_count--;
        // Signal potentially waiting getters that a slot might be free for creation
        pool_signal(pool); 
    } else {
        // Add valid connection back to idle list
        mcp_pooled_connection_t* pooled_conn = (mcp_pooled_connection_t*)malloc(sizeof(mcp_pooled_connection_t));
        if (pooled_conn) {
            pooled_conn->socket_fd = connection; // Store SOCKET directly
            pooled_conn->last_used_time = time(NULL); // Record return time
            pooled_conn->next = pool->idle_list;
            pool->idle_list = pooled_conn;
            pool->idle_count++;
            log_message(LOG_LEVEL_DEBUG, "Returned connection %d to idle pool.", (int)connection);
            // Signal one waiting getter that a connection is available
            pool_signal(pool); 
        } else {
            // Failed to allocate node, just close the connection
             log_message(LOG_LEVEL_ERROR, "Failed to allocate node for idle connection %d, closing.", (int)connection);
             close_connection(connection); // No cast needed
             pool->total_count--;
             // Signal potentially waiting getters that a slot might be free for creation
             pool_signal(pool);
        }
    }

    pool_unlock(pool);
    return 0;
}

/**
 * @brief Destroys the connection pool.
 */
void mcp_connection_pool_destroy(mcp_connection_pool_t* pool) {
    if (!pool) {
        return;
    }

    log_message(LOG_LEVEL_INFO, "Destroying connection pool for %s:%d.", pool->host, pool->port);

    // 1. Signal shutdown and wake waiters
    pool_lock(pool);
    if (pool->shutting_down) { // Avoid double destroy
        pool_unlock(pool);
        return;
    }
    pool->shutting_down = true;
    pool_broadcast(pool); // Wake all waiting threads
    pool_unlock(pool);

    // 2. Optional: Join maintenance thread if it exists
    // TODO: Implement maintenance thread join logic if added

    // 3. Close idle connections and free resources
    pool_lock(pool);
    log_message(LOG_LEVEL_INFO, "Closing %zu idle connections.", pool->idle_count);
    mcp_pooled_connection_t* current = pool->idle_list;
    while(current) {
        mcp_pooled_connection_t* next = current->next;
        close_connection(current->socket_fd);
        free(current);
        current = next;
    }
    pool->idle_list = NULL;
    pool->idle_count = 0;
    
    // Note: Active connections are not explicitly waited for here. 
    // They will fail on next use if the pool is destroyed.
    // A more robust shutdown might wait for active_count to reach zero,
    // but requires careful design to avoid deadlocks if connections aren't released.
    log_message(LOG_LEVEL_INFO, "%zu connections were active during shutdown.", pool->active_count);
    pool->total_count = 0; // Reset counts
    pool->active_count = 0;

    pool_unlock(pool);

    // 4. Destroy sync primitives and free memory
    destroy_sync_primitives(pool);
    free(pool->host);
    free(pool);

    log_message(LOG_LEVEL_INFO, "Connection pool destroyed.");
}


// --- Internal Helper Implementations ---

// Creates a TCP socket, connects to host:port with a timeout.
// Returns connected socket descriptor or INVALID_SOCKET on failure.
static SOCKET create_new_connection(const char* host, int port, int connect_timeout_ms) {
    SOCKET sock = INVALID_SOCKET;
    struct addrinfo hints, *servinfo = NULL, *p = NULL;
    char port_str[6]; // Max port number is 65535
    int rv;
    int err = 0; // Initialize err

    // Initialize Winsock if on Windows
#ifdef _WIN32
    WSADATA wsaData;
    // Only call WSAStartup once per process if possible, but for simplicity here...
    // A better approach uses a global counter or initialization flag.
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { 
        log_message(LOG_LEVEL_ERROR, "WSAStartup failed.");
        return INVALID_SOCKET;
    }
#endif

    snprintf(port_str, sizeof(port_str), "%d", port);

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    if ((rv = getaddrinfo(host, port_str, &hints, &servinfo)) != 0) {
        log_message(LOG_LEVEL_ERROR, "getaddrinfo failed for %s:%s : %s", host, port_str, gai_strerror(rv));
        #ifdef _WIN32
            WSACleanup(); // Cleanup if getaddrinfo failed
        #endif
        return INVALID_SOCKET;
    }

    // Loop through all the results and connect to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == INVALID_SOCKET) {
            #ifdef _WIN32
                log_message(LOG_LEVEL_WARN, "socket() failed: %d", WSAGetLastError());
            #else
                log_message(LOG_LEVEL_WARN, "socket() failed: %s", strerror(errno));
            #endif
            continue;
        }

        // Set non-blocking for timeout connect
#ifdef _WIN32
        u_long mode = 1; // 1 to enable non-blocking socket
        if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
            log_message(LOG_LEVEL_ERROR, "ioctlsocket(FIONBIO) failed: %d", WSAGetLastError());
            closesocket(sock);
            sock = INVALID_SOCKET;
            continue;
        }
#else
        int flags = fcntl(sock, F_GETFL, 0);
        if (flags == -1) {
             log_message(LOG_LEVEL_ERROR, "fcntl(F_GETFL) failed: %s", strerror(errno));
             close(sock);
             sock = INVALID_SOCKET;
             continue;
        }
        if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
            log_message(LOG_LEVEL_ERROR, "fcntl(F_SETFL, O_NONBLOCK) failed: %s", strerror(errno));
            close(sock);
            sock = INVALID_SOCKET;
            continue;
        }
#endif

        // Initiate non-blocking connect
        rv = connect(sock, p->ai_addr, (int)p->ai_addrlen); // Cast addrlen

#ifdef _WIN32
        if (rv == SOCKET_ERROR) {
            err = WSAGetLastError();
            // WSAEINPROGRESS is not typically returned on Windows for non-blocking connect,
            // WSAEWOULDBLOCK indicates the operation is in progress.
            if (err != WSAEWOULDBLOCK) { 
                log_message(LOG_LEVEL_WARN, "connect() failed immediately: %d", err);
                closesocket(sock);
                sock = INVALID_SOCKET;
                continue;
            }
            // Connection is in progress (WSAEWOULDBLOCK), use poll/select to wait
            err = WSAEWOULDBLOCK; // Set err for unified handling below
        } 
        // else rv == 0 means immediate success (less common for non-blocking)
#else // POSIX
        if (rv == -1) {
            err = errno;
            if (err != EINPROGRESS) {
                log_message(LOG_LEVEL_WARN, "connect() failed immediately: %s", strerror(err));
                close(sock);
                sock = INVALID_SOCKET;
                continue;
            }
             // Connection is in progress (EINPROGRESS), use poll/select to wait
        }
        // else rv == 0 means immediate success
#endif
        
        // If connect returned 0 (immediate success) or EINPROGRESS/WSAEWOULDBLOCK (in progress)
        if (rv == 0 || err == EINPROGRESS || err == WSAEWOULDBLOCK) {
            // If connect succeeded immediately (rv == 0), skip waiting
            if (rv != 0) { 
                struct pollfd pfd;
                pfd.fd = sock;
                pfd.events = POLLOUT; // Check for writability

#ifdef _WIN32
                rv = WSAPoll(&pfd, 1, connect_timeout_ms); 
#else
                rv = poll(&pfd, 1, connect_timeout_ms);
#endif

                if (rv <= 0) { // Timeout (rv==0) or error (rv<0)
                    if (rv == 0) {
                         log_message(LOG_LEVEL_WARN, "connect() timed out after %d ms.", connect_timeout_ms);
                    } else {
                         #ifdef _WIN32
                            log_message(LOG_LEVEL_ERROR, "WSAPoll() failed during connect: %d", WSAGetLastError());
                         #else
                            log_message(LOG_LEVEL_ERROR, "poll() failed during connect: %s", strerror(errno));
                         #endif
                    }
                    close_connection(sock);
                    sock = INVALID_SOCKET;
                    continue; // Try next address
                }
                // rv > 0, socket is ready, check for errors
            }

            // Check SO_ERROR to confirm connection success after waiting or immediate success
            int optval = 0; 
            socklen_t optlen = sizeof(optval);
            if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&optval, &optlen) == SOCKET_ERROR) {
                 #ifdef _WIN32
                    log_message(LOG_LEVEL_ERROR, "getsockopt(SO_ERROR) failed: %d", WSAGetLastError());
                 #else
                    log_message(LOG_LEVEL_ERROR, "getsockopt(SO_ERROR) failed: %s", strerror(errno));
                 #endif
                 close_connection(sock);
                 sock = INVALID_SOCKET;
                 continue; // Try next address
            }
            
            if (optval != 0) { // Connect failed
                #ifdef _WIN32
                    log_message(LOG_LEVEL_WARN, "connect() failed after wait: SO_ERROR=%d (WSA: %d)", optval, optval); // Use optval as error code
                #else
                     log_message(LOG_LEVEL_WARN, "connect() failed after wait: %s", strerror(optval)); // Use optval as errno
                #endif
                close_connection(sock);
                sock = INVALID_SOCKET;
                continue; // Try next address
            }
            // Connection successful!
        } else { 
             // This case should not be reached if connect returned other errors handled above
             log_message(LOG_LEVEL_ERROR, "Unexpected state after connect() call (rv=%d, err=%d)", rv, err);
             close_connection(sock);
             sock = INVALID_SOCKET;
             continue;
        }


        // If we get here with a valid socket, connection succeeded.
        // Optionally, switch back to blocking mode if desired.
        // For simplicity, leave it non-blocking for now.

        break; // If we get here, we must have connected successfully
    }

    freeaddrinfo(servinfo); // All done with this structure

    if (sock == INVALID_SOCKET) {
        log_message(LOG_LEVEL_ERROR, "Failed to connect to %s:%d after trying all addresses.", host, port);
        #ifdef _WIN32
            WSACleanup(); // Cleanup if we never succeeded
        #endif
    } else {
         log_message(LOG_LEVEL_DEBUG, "Successfully connected socket %d to %s:%d.", (int)sock, host, port);
    }

    return sock;
}


static void close_connection(SOCKET socket_fd) {
    if (socket_fd != INVALID_SOCKET) {
        #ifdef _WIN32
            closesocket(socket_fd);
            // WSACleanup(); // Cleanup should happen once per application, not per socket
        #else
            close(socket_fd);
        #endif
    }
}

static int init_sync_primitives(mcp_connection_pool_t* pool) {
    #ifdef _WIN32
        InitializeCriticalSection(&pool->mutex);
        InitializeConditionVariable(&pool->cond_var);
        return 0; // No error code defined for these in basic usage
    #else
        if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
            return -1;
        }
        if (pthread_cond_init(&pool->cond_var, NULL) != 0) {
            pthread_mutex_destroy(&pool->mutex); // Clean up mutex
            return -1;
        }
        return 0;
    #endif
}

static void destroy_sync_primitives(mcp_connection_pool_t* pool) {
     #ifdef _WIN32
        DeleteCriticalSection(&pool->mutex);
        // Condition variables don't need explicit destruction on Windows
    #else
        pthread_mutex_destroy(&pool->mutex);
        pthread_cond_destroy(&pool->cond_var);
    #endif
}

/**
 * @brief Gets statistics about the connection pool.
 */
int mcp_connection_pool_get_stats(mcp_connection_pool_t* pool, size_t* total_connections, size_t* idle_connections, size_t* active_connections) {
    if (!pool || !total_connections || !idle_connections || !active_connections) {
        log_message(LOG_LEVEL_ERROR, "mcp_connection_pool_get_stats: Received NULL pointer argument.");
        return -1;
    }

    pool_lock(pool);
    
    *total_connections = pool->total_count;
    *idle_connections = pool->idle_count;
    *active_connections = pool->active_count;
    
    pool_unlock(pool);

    return 0;
}
