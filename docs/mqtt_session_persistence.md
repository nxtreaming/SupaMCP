# MQTT Session Persistence

This document describes the MQTT session persistence functionality in SupaMCP, which allows MQTT clients to maintain session state across disconnections and reconnections.

## Overview

MQTT session persistence enables:
- **Subscription persistence**: Subscriptions are maintained across client restarts
- **In-flight message tracking**: QoS > 0 messages are tracked and retried
- **Session state management**: Client session data is stored persistently
- **Session expiry**: Automatic cleanup of expired sessions
- **Cross-platform support**: Works on Windows, Linux, and macOS

## Configuration

### Basic Configuration

```c
mcp_mqtt_client_config_t config = MCP_MQTT_CLIENT_CONFIG_DEFAULT;

// Enable session persistence
config.persistent_session = true;
config.session_storage_path = "./mqtt_sessions";
config.session_expiry_interval = 3600;  // 1 hour

// Important: Disable clean session for persistence
config.base.clean_session = false;
```

### Configuration Options

| Option | Type | Description | Default |
|--------|------|-------------|---------|
| `persistent_session` | `bool` | Enable/disable session persistence | `false` |
| `session_storage_path` | `char*` | Directory to store session files | `NULL` |
| `session_expiry_interval` | `uint32_t` | Session expiry time in seconds (0 = no expiry) | `0` |

## Session Data

The following data is persisted:

### Subscriptions
- Topic names and QoS levels
- Subscription state (active/inactive)

### In-flight Messages
- Packet IDs for QoS > 0 messages
- Message topics and payloads
- QoS levels and retain flags
- Send timestamps and retry counts

### Session Metadata
- Client ID
- Session creation time
- Last access time
- Expiry interval
- File format version

## API Functions

### Session Management

```c
// Save current session state
int mcp_mqtt_client_save_session(mcp_transport_t* transport);

// Load existing session state
int mcp_mqtt_client_load_session(mcp_transport_t* transport);

// Delete session data
int mcp_mqtt_client_delete_session(mcp_transport_t* transport);

// Check if session exists
bool mcp_mqtt_client_session_exists(mcp_transport_t* transport);

// Clean up expired sessions
int mcp_mqtt_client_cleanup_expired_sessions(void);
```

### Automatic Operations

Session persistence operates automatically:
- **On connection**: Existing sessions are loaded automatically
- **On disconnection**: Session state is saved automatically
- **Background cleanup**: Expired sessions are cleaned up periodically

## File Format

Session files are stored in binary format with the following structure:

```
[Magic Number: 4 bytes]     // "MCPS" (0x4D435053)
[Version: 2 bytes]          // File format version
[Created Time: 8 bytes]     // Session creation timestamp
[Access Time: 8 bytes]      // Last access timestamp
[Expiry Interval: 4 bytes]  // Expiry interval in seconds
[Client ID Length: 2 bytes] // Length of client ID
[Client ID: variable]       // Client ID string
[Subscription Count: 2 bytes] // Number of subscriptions
[Subscriptions: variable]   // Subscription data
[Last Packet ID: 4 bytes]   // Last used packet ID
[In-flight Count: 2 bytes]  // Number of in-flight messages
[In-flight Messages: variable] // In-flight message data
```

## Session Cleanup

### Automatic Cleanup

A background thread automatically cleans up expired sessions:
- Runs every hour by default
- Checks session expiry times
- Removes expired session files
- Logs cleanup activities

### Manual Cleanup

```c
// Trigger immediate cleanup
int cleaned = mcp_mqtt_client_cleanup_expired_sessions();
printf("Cleaned %d expired sessions\n", cleaned);
```

## Error Handling

### Common Issues

1. **Storage path not accessible**
   - Ensure directory exists and is writable
   - Check file permissions

2. **Session file corruption**
   - Files with invalid magic numbers are ignored
   - Corrupted files are automatically deleted

3. **Version incompatibility**
   - Newer file versions are rejected
   - Older versions are supported with migration

### Error Codes

- `0`: Success
- `-1`: General error (invalid parameters, file I/O error, etc.)

## Best Practices

### Configuration

1. **Choose appropriate expiry intervals**
   ```c
   config.session_expiry_interval = 86400;  // 24 hours for long-lived clients
   config.session_expiry_interval = 3600;   // 1 hour for temporary clients
   config.session_expiry_interval = 0;      // No expiry for permanent clients
   ```

2. **Use dedicated storage directories**
   ```c
   config.session_storage_path = "/var/lib/supamcp/sessions";  // Linux
   config.session_storage_path = "C:\\ProgramData\\SupaMCP\\sessions";  // Windows
   ```

3. **Set clean_session appropriately**
   ```c
   config.base.clean_session = false;  // For persistent sessions
   config.base.clean_session = true;   // For temporary sessions
   ```

### Monitoring

1. **Check session statistics**
   ```c
   mcp_mqtt_client_stats_t stats;
   mcp_mqtt_client_get_stats(transport, &stats);
   printf("In-flight messages: %u\n", stats.current_inflight_messages);
   ```

2. **Monitor cleanup activities**
   ```c
   // Enable debug logging to see cleanup activities
   mcp_log_set_level(MCP_LOG_DEBUG);
   ```

## Example Usage

See `examples/mqtt_session_persistence_example.c` for a complete example demonstrating:
- Session configuration
- Automatic session loading/saving
- Manual session management
- Statistics monitoring
- Cleanup operations

## Platform Notes

### Windows
- Session files are stored with Windows-safe filenames
- Uses Windows file APIs for directory enumeration
- Supports long path names

### Unix/Linux
- Uses POSIX file APIs
- Supports standard Unix file permissions
- Works with network filesystems

### Cross-platform
- Binary file format is platform-independent
- Timestamps are stored in milliseconds since epoch
- String encoding is UTF-8
