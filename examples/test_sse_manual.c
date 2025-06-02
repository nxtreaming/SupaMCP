/**
 * @file test_sse_manual.c
 * @brief Manual SSE connection test tool for debugging HTTP Streamable transport
 * 
 * This tool manually creates a TCP connection to test SSE (Server-Sent Events)
 * functionality of the HTTP Streamable transport server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    // Create socket
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        WSACleanup();
        return 1;
    }

    // Connect to server
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Connection failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Connected to server\n");

    // Send GET request for SSE
    const char* request = 
        "GET /mcp HTTP/1.1\r\n"
        "Host: 127.0.0.1:8080\r\n"
        "Accept: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    printf("Sending SSE request:\n%s\n", request);

    if (send(sock, request, (int)strlen(request), 0) == SOCKET_ERROR) {
        printf("Send failed\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    // Receive response
    char buffer[4096];
    int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Received response (%d bytes):\n", bytes_received);
        printf("=== RAW RESPONSE ===\n");
        
        // Print raw bytes for debugging
        for (int i = 0; i < bytes_received; i++) {
            if (buffer[i] == '\r') {
                printf("\\r");
            } else if (buffer[i] == '\n') {
                printf("\\n\n");
            } else if (buffer[i] >= 32 && buffer[i] <= 126) {
                printf("%c", buffer[i]);
            } else {
                printf("\\x%02x", (unsigned char)buffer[i]);
            }
        }
        printf("\n=== END RAW ===\n\n");
        
        printf("=== FORMATTED RESPONSE ===\n");
        printf("%s\n", buffer);
        printf("=== END FORMATTED ===\n\n");
        
        // Look for Content-Type header (case-insensitive)
        char* content_type = strstr(buffer, "Content-Type:");
        if (!content_type) {
            content_type = strstr(buffer, "content-type:");
        }
        if (!content_type) {
            content_type = strstr(buffer, "Content-type:");
        }
        if (!content_type) {
            content_type = strstr(buffer, "CONTENT-TYPE:");
        }
        
        if (content_type) {
            char* line_end = strstr(content_type, "\r\n");
            if (line_end) {
                int header_len = (int)(line_end - content_type);
                printf("Found Content-Type header: %.*s\n", header_len, content_type);
            }
        } else {
            printf("Content-Type header not found!\n");
        }
        
        // Check for text/event-stream
        if (strstr(buffer, "text/event-stream")) {
            printf("Found text/event-stream in response\n");
        } else {
            printf("text/event-stream not found in response\n");
        }
    } else {
        printf("Receive failed or no data\n");
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
