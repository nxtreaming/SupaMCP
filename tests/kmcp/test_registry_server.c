#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define PORT 8081
#define BUFFER_SIZE 4096

// Sample registry response
const char* registry_response = 
"HTTP/1.1 200 OK\r\n"
"Content-Type: application/json\r\n"
"Content-Length: %d\r\n"
"\r\n"
"%s";

// Sample servers list
const char* servers_list = 
"{"
"  \"servers\": ["
"    {"
"      \"id\": \"server1\","
"      \"name\": \"Local Server\","
"      \"url\": \"http://localhost:8080\","
"      \"description\": \"Local MCP server for testing\","
"      \"version\": \"1.0.0\","
"      \"tools\": [\"echo\", \"calculator\", \"translator\"]"
"    },"
"    {"
"      \"id\": \"server2\","
"      \"name\": \"Remote Server\","
"      \"url\": \"http://example.com:8080\","
"      \"description\": \"Remote MCP server for testing\","
"      \"version\": \"1.0.0\","
"      \"tools\": [\"echo\", \"calculator\", \"translator\"]"
"    }"
"  ]"
"}";

// Sample server details
const char* server_details = 
"{"
"  \"id\": \"server1\","
"  \"name\": \"Local Server\","
"  \"url\": \"http://localhost:8080\","
"  \"description\": \"Local MCP server for testing\","
"  \"version\": \"1.0.0\","
"  \"tools\": [\"echo\", \"calculator\", \"translator\"],"
"  \"capabilities\": [\"batch\", \"streaming\"],"
"  \"status\": \"online\","
"  \"lastSeen\": \"2023-01-01T00:00:00Z\","
"  \"metadata\": {"
"    \"owner\": \"KMCP Team\","
"    \"region\": \"local\""
"  }"
"}";

/**
 * Initialize socket library
 */
int initialize_socket_library() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
    return 0;
#endif
}

/**
 * Cleanup socket library
 */
void cleanup_socket_library() {
#ifdef _WIN32
    WSACleanup();
#endif
}

/**
 * Close socket
 */
void close_socket(int socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

/**
 * Handle client request
 */
void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Received request:\n%s\n", buffer);
        
        // Parse request
        char method[16];
        char path[256];
        sscanf(buffer, "%15s %255s", method, path);
        
        // Handle different endpoints
        const char* response_body = NULL;
        
        if (strcmp(path, "/servers") == 0) {
            response_body = servers_list;
        } else if (strncmp(path, "/servers/", 9) == 0) {
            response_body = server_details;
        } else {
            response_body = "{\"error\":\"Not found\"}";
        }
        
        // Send response
        char response[BUFFER_SIZE];
        int response_length = snprintf(response, BUFFER_SIZE, registry_response, strlen(response_body), response_body);
        
        send(client_socket, response, response_length, 0);
    }
    
    close_socket(client_socket);
}

/**
 * Main function
 */
int main() {
    // Initialize socket library
    if (initialize_socket_library() != 0) {
        fprintf(stderr, "Failed to initialize socket library\n");
        return 1;
    }
    
    // Create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket < 0) {
        fprintf(stderr, "Failed to create socket\n");
        cleanup_socket_library();
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
        fprintf(stderr, "Failed to set socket options\n");
        close_socket(server_socket);
        cleanup_socket_library();
        return 1;
    }
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Failed to bind socket\n");
        close_socket(server_socket);
        cleanup_socket_library();
        return 1;
    }
    
    // Listen for connections
    if (listen(server_socket, 10) < 0) {
        fprintf(stderr, "Failed to listen on socket\n");
        close_socket(server_socket);
        cleanup_socket_library();
        return 1;
    }
    
    printf("Registry server listening on port %d\n", PORT);
    
    // Accept connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket < 0) {
            fprintf(stderr, "Failed to accept connection\n");
            continue;
        }
        
        printf("Client connected: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        handle_client(client_socket);
    }
    
    // Cleanup
    close_socket(server_socket);
    cleanup_socket_library();
    
    return 0;
}
