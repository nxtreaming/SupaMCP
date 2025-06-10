/**
 * @file mqtt_session_persistence_example.c
 * @brief Example demonstrating MQTT session persistence functionality
 * 
 * This example shows how to:
 * 1. Configure MQTT client with session persistence
 * 2. Save and restore session state
 * 3. Handle session expiry
 * 4. Clean up expired sessions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "mcp_mqtt_client_transport.h"
#include "mcp_transport_factory.h"
#include "mcp_transport.h"
#include "mcp_log.h"
#include "mcp_sys_utils.h"

static volatile bool g_running = true;

void signal_handler(int sig) {
    (void)sig;
    g_running = false;
    printf("\nShutting down...\n");
}

void print_session_info(mcp_transport_t* transport) {
    if (!transport) return;
    
    printf("\n=== Session Information ===\n");
    
    // Check if session exists
    bool exists = mcp_mqtt_client_session_exists(transport);
    printf("Session exists: %s\n", exists ? "Yes" : "No");
    
    // Get client statistics
    mcp_mqtt_client_stats_t stats;
    if (mcp_mqtt_client_get_stats(transport, &stats) == 0) {
        printf("Messages sent: %llu\n", (unsigned long long)stats.messages_sent);
        printf("Messages received: %llu\n", (unsigned long long)stats.messages_received);
        printf("Successful connections: %llu\n", (unsigned long long)stats.successful_connections);
        printf("Connection failures: %llu\n", (unsigned long long)stats.connection_failures);
        printf("Current in-flight messages: %u\n", stats.current_inflight_messages);
    }
    
    printf("===========================\n\n");
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    // Set up signal handling
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize logging
    mcp_log_set_level(MCP_LOG_LEVEL_DEBUG);
    
    printf("MQTT Session Persistence Example\n");
    printf("================================\n\n");
    
    // Configure MQTT client with session persistence
    mcp_mqtt_client_config_t config = MCP_MQTT_CLIENT_CONFIG_DEFAULT;
    config.base.host = "mqtt.supamcp.com";
    config.base.port = 1883;
    config.base.client_id = "session_test_client_001";
    config.base.keep_alive = 60;
    config.base.clean_session = false;  // Important: disable clean session for persistence
    
    // Enable session persistence
    config.persistent_session = true;
    config.session_storage_path = "./mqtt_sessions";
    config.session_expiry_interval = 3600;  // 1 hour expiry
    
    // Enable metrics for demonstration
    config.enable_metrics = true;
    
    // Create MQTT client transport
    mcp_transport_t* transport = mcp_transport_mqtt_client_create_with_config(&config);
    if (!transport) {
        fprintf(stderr, "Failed to create MQTT client transport\n");
        return 1;
    }
    
    printf("Created MQTT client with session persistence enabled\n");
    printf("Session storage path: %s\n", config.session_storage_path);
    printf("Session expiry: %u seconds\n", config.session_expiry_interval);
    printf("Client ID: %s\n\n", config.base.client_id);
    
    // Check if we have an existing session
    print_session_info(transport);
    
    // Try to load existing session
    printf("Attempting to load existing session...\n");
    if (mcp_mqtt_client_load_session(transport) == 0) {
        printf("Successfully loaded existing session!\n");
    } else {
        printf("No existing session found or failed to load\n");
    }
    
    print_session_info(transport);
    
    // Start the transport (this will also start session cleanup thread)
    printf("Starting MQTT client transport...\n");
    if (mcp_transport_start(transport, NULL, NULL, NULL) != 0) {
        fprintf(stderr, "Failed to start MQTT client transport\n");
        goto cleanup;
    }
    
    printf("MQTT client started successfully\n");
    printf("Session cleanup thread is running in background\n\n");
    
    // Simulate some activity
    printf("Simulating client activity...\n");
    printf("Press Ctrl+C to stop\n\n");
    
    int counter = 0;
    while (g_running) {
        // Get current state
        mcp_mqtt_client_state_t state = mcp_mqtt_client_get_state(transport);
        printf("Client state: %d, Counter: %d\r", state, counter++);
        fflush(stdout);
        
        // Save session periodically
        if (counter % 10 == 0) {
            printf("\nSaving session state...\n");
            if (mcp_mqtt_client_save_session(transport) == 0) {
                printf("Session saved successfully\n");
            } else {
                printf("Failed to save session\n");
            }
        }
        
        // Trigger expired session cleanup periodically
        if (counter % 50 == 0) {
            printf("\nCleaning up expired sessions...\n");
            int cleaned = mcp_mqtt_client_cleanup_expired_sessions();
            if (cleaned >= 0) {
                printf("Cleaned %d expired sessions\n", cleaned);
            } else {
                printf("Failed to clean expired sessions\n");
            }
        }
        
        mcp_sleep_ms(1000);
    }
    
    printf("\n\nStopping MQTT client...\n");
    
    // Save final session state
    printf("Saving final session state...\n");
    if (mcp_mqtt_client_save_session(transport) == 0) {
        printf("Final session saved successfully\n");
    } else {
        printf("Failed to save final session\n");
    }
    
    print_session_info(transport);
    
    // Stop the transport
    if (mcp_transport_stop(transport) != 0) {
        fprintf(stderr, "Failed to stop MQTT client transport\n");
    }
    
cleanup:
    // Clean up
    mcp_transport_destroy(transport);
    
    printf("Example completed\n");
    return 0;
}
