#ifndef MCP_SOCKET_UTILS_H
#define MCP_SOCKET_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// --- Platform-Specific Socket Definitions ---
#ifdef _WIN32
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <winsock2.h>
#   include <ws2tcpip.h>
#   pragma comment(lib, "Ws2_32.lib")
    typedef SOCKET socket_t;
    typedef WSABUF mcp_iovec_t;
    #define MCP_INVALID_SOCKET INVALID_SOCKET
    #define MCP_SOCKET_ERROR   SOCKET_ERROR
    #define MCP_SEND_FLAGS     0
#else // POSIX
#   include <sys/types.h>
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <arpa/inet.h>
#   include <unistd.h>
#   include <fcntl.h>
#   include <netdb.h>
#   include <sys/uio.h>
#   include <poll.h>
#   include <errno.h>
    typedef int socket_t;
    typedef struct iovec mcp_iovec_t;
    #define MCP_INVALID_SOCKET (-1)
    #define MCP_SOCKET_ERROR   (-1)
    // MSG_NOSIGNAL prevents SIGPIPE on send() if the peer has closed the connection (Linux specific)
    #ifdef MSG_NOSIGNAL
        #define MCP_SEND_FLAGS MSG_NOSIGNAL
    #else
        #define MCP_SEND_FLAGS 0
    #endif
#endif

// Platform-specific sleep function is now implemented in the .c file

#ifdef __cplusplus
extern "C" {
#endif

// --- Platform Utilities ---

/**
 * @brief Pauses execution for the specified number of milliseconds.
 * @param milliseconds The duration to sleep in milliseconds.
 */
void mcp_sleep_ms(uint32_t milliseconds);

// --- Initialization and Cleanup ---

/**
 * @brief Initializes the socket library (required on Windows).
 * Call once at application startup.
 * @return 0 on success, -1 on failure.
 */
int mcp_socket_init(void);

/**
 * @brief Cleans up the socket library (required on Windows).
 * Call once at application shutdown.
 */
void mcp_socket_cleanup(void);

// --- Socket Operations ---

/**
 * @brief Closes a socket descriptor.
 * @param sock The socket to close.
 * @return 0 on success, MCP_SOCKET_ERROR on failure.
 */
int mcp_socket_close(socket_t sock);

/**
 * @brief Gets the last socket error code for the calling thread.
 * @return The platform-specific error code (e.g., WSAGetLastError() or errno).
 */
int mcp_socket_get_last_error(void);

/**
 * @brief Sets a socket to non-blocking mode.
 * @param sock The socket descriptor.
 * @return 0 on success, -1 on failure.
 */
int mcp_socket_set_non_blocking(socket_t sock);

/**
 * @brief Sets the TCP_NODELAY option on a socket to disable Nagle's algorithm.
 *
 * This function disables Nagle's algorithm, which buffers small packets and
 * waits for more data before sending, to reduce latency for small packets.
 *
 * @param sock The socket to set the option on.
 * @return 0 on success, -1 on error.
 */
int mcp_socket_set_nodelay(socket_t sock);

/**
 * @brief Connects to a server address. Handles non-blocking connect internally.
 * This is a blocking call from the perspective of the caller, but uses non-blocking internally.
 * @param host The hostname or IP address of the server.
 * @param port The port number of the server.
 * @param timeout_ms Timeout for the connection attempt in milliseconds.
 * @return The connected socket descriptor on success, MCP_INVALID_SOCKET on failure or timeout.
 */
socket_t mcp_socket_connect(const char* host, uint16_t port, uint32_t timeout_ms);

/**
 * @brief Sends exactly 'len' bytes over the socket. Handles partial sends.
 * @param sock The socket descriptor.
 * @param buf The buffer containing data to send.
 * @param len The number of bytes to send.
 * @param stop_flag Optional pointer to a boolean flag. If not NULL and becomes true, the operation aborts early.
 * @return 0 on success, -1 on error or if aborted by stop_flag.
 */
int mcp_socket_send_exact(socket_t sock, const char* buf, size_t len, volatile bool* stop_flag);

/**
 * @brief Receives exactly 'len' bytes from the socket. Handles partial receives.
 * @param sock The socket descriptor.
 * @param buf The buffer to store received data.
 * @param len The number of bytes to receive.
 * @param stop_flag Optional pointer to a boolean flag. If not NULL and becomes true, the operation aborts early.
 * @return 0 on success, -1 on error, connection closed, or if aborted by stop_flag.
 */
int mcp_socket_recv_exact(socket_t sock, char* buf, size_t len, volatile bool* stop_flag);

/**
 * @brief Sends data from multiple buffers (vectored I/O). Handles partial sends.
 * @param sock The socket descriptor.
 * @param iov Array of iovec structures describing the buffers.
 * @param iovcnt The number of iovec structures in the array.
 * @param stop_flag Optional pointer to a boolean flag. If not NULL and becomes true, the operation aborts early.
 * @return 0 on success, -1 on error or if aborted by stop_flag.
 */
int mcp_socket_send_vectors(socket_t sock, mcp_iovec_t* iov, int iovcnt, volatile bool* stop_flag);

/**
 * @brief Waits for a socket to become readable or until a timeout occurs.
 * @param sock The socket descriptor.
 * @param timeout_ms Timeout in milliseconds. 0 means no wait, -1 means wait indefinitely.
 * @param stop_flag Optional pointer to a boolean flag. If not NULL and becomes true, the operation aborts early.
 * @return 1 if readable, 0 if timeout, -1 on error or if aborted by stop_flag.
 */
int mcp_socket_wait_readable(socket_t sock, int timeout_ms, volatile bool* stop_flag);

// --- Server Specific (Consider moving later if needed) ---

/**
 * @brief Creates a listening socket bound to the specified host and port.
 * @param host The host address to bind to (e.g., "0.0.0.0" for all interfaces).
 * @param port The port number to bind to.
 * @param backlog The maximum length of the queue of pending connections.
 * @return The listening socket descriptor on success, MCP_INVALID_SOCKET on failure.
 */
socket_t mcp_socket_create_listener(const char* host, uint16_t port, int backlog);

/**
 * @brief Accepts a new connection on a listening socket.
 * @param listen_sock The listening socket descriptor.
 * @param client_addr Optional pointer to store the client's address information.
 * @param addr_len Optional pointer to store the size of the client_addr structure.
 * @return The connected client socket descriptor on success, MCP_INVALID_SOCKET on failure.
 */
socket_t mcp_socket_accept(socket_t listen_sock, struct sockaddr* client_addr, socklen_t* addr_len);


#ifdef __cplusplus
}
#endif

#endif // MCP_SOCKET_UTILS_H
