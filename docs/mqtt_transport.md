# MQTT Transport

This document describes the MQTT transport implementation in SupaMCP, which enables MCP communication over MQTT protocol.

## Overview

The MQTT transport allows MCP servers and clients to communicate using the MQTT protocol. This is particularly useful for:

- IoT environments where MQTT is the primary communication protocol
- Scenarios requiring publish/subscribe messaging patterns
- Distributed systems with MQTT brokers as message intermediaries
- Applications needing reliable message delivery with QoS guarantees

## Features

- **MQTT 3.1.1 Protocol Support**: Full compatibility with MQTT 3.1.1 specification
- **Quality of Service (QoS)**: Support for QoS levels 0, 1, and 2
- **SSL/TLS Encryption**: Optional secure connections to MQTT brokers
- **Authentication**: Username/password authentication support
- **Topic Management**: Configurable topic structure for MCP messages
- **Session Management**: Clean and persistent session support
- **Last Will and Testament**: Configurable LWT messages
- **Auto-reconnection**: Automatic reconnection with exponential backoff
- **Message Tracking**: In-flight message tracking for QoS > 0

## Architecture

### Topic Structure

The MQTT transport uses a structured topic hierarchy for MCP messages:

```
{topic_prefix}request/{client_id}    - Client requests to server
{topic_prefix}response/{client_id}   - Server responses to client
{topic_prefix}notification/{client_id} - Server notifications to client
```

Default topic prefix is `mcp/`, resulting in topics like:
- `mcp/request/client_123`
- `mcp/response/client_123`
- `mcp/notification/client_123`

### Message Flow

1. **Client Request**: Published to `{prefix}request/{client_id}`
2. **Server Processing**: Server subscribes to request topics and processes messages
3. **Server Response**: Published to `{prefix}response/{client_id}`
4. **Client Reception**: Client subscribes to its response topic

## Configuration

### Server Configuration

```c
#include "mcp_mqtt_transport.h"

mcp_mqtt_config_t config = MCP_MQTT_CONFIG_DEFAULT;
config.host = "mqtt.example.com";
config.port = 1883;
config.client_id = "mcp_server_001";
config.username = "server_user";
config.password = "server_pass";
config.topic_prefix = "mcp/";
config.qos = 1;
config.clean_session = true;

mcp_transport_t* transport = mcp_transport_mqtt_create(&config);
```

### Client Configuration

```c
#include "mcp_mqtt_client_transport.h"

mcp_mqtt_client_config_t config = MCP_MQTT_CLIENT_CONFIG_DEFAULT;
config.base.host = "mqtt.example.com";
config.base.port = 1883;
config.base.client_id = "mcp_client_001";
config.base.username = "client_user";
config.base.password = "client_pass";
config.auto_reconnect = true;
config.enable_metrics = true;

mcp_transport_t* transport = mcp_transport_mqtt_client_create_with_config(&config);
```

## Command Line Usage

### Server

```bash
# Basic MQTT server
./mcp_server --mqtt --host 127.0.0.1 --port 1883

# MQTT server with authentication
./mcp_server --mqtt --host 127.0.0.1 --port 1883 \
  --mqtt-username server_user --mqtt-password server_pass \
  --mqtt-client-id mcp_server_001

# MQTT server with SSL and custom settings
./mcp_server --mqtt --host 127.0.0.1 --port 8883 \
  --mqtt-ssl --mqtt-qos 2 --mqtt-topic-prefix "myapp/mcp/" \
  --mqtt-persistent-session
```

### Client

```bash
# Basic MQTT client
./mcp_client --mqtt --host 127.0.0.1 --port 1883

# MQTT client with authentication
./mcp_client --mqtt --host 127.0.0.1 --port 1883 \
  --mqtt-username client_user --mqtt-password client_pass \
  --mqtt-client-id mcp_client_001

# MQTT client with SSL
./mcp_client --mqtt --host 127.0.0.1 --port 8883 \
  --mqtt-ssl --mqtt-qos 1 --mqtt-topic-prefix "myapp/mcp/"
```

## Configuration Options

### Basic Options

| Option | Description | Default |
|--------|-------------|---------|
| `host` | MQTT broker hostname | `localhost` |
| `port` | MQTT broker port | `1883` (non-SSL), `8883` (SSL) |
| `client_id` | MQTT client identifier | Auto-generated |
| `username` | MQTT username | None |
| `password` | MQTT password | None |

### Topic Configuration

| Option | Description | Default |
|--------|-------------|---------|
| `topic_prefix` | Prefix for all MCP topics | `mcp/` |
| `request_topic` | Custom request topic template | `{prefix}request/{client_id}` |
| `response_topic` | Custom response topic template | `{prefix}response/{client_id}` |
| `notification_topic` | Custom notification topic template | `{prefix}notification/{client_id}` |

### Quality of Service

| Option | Description | Default |
|--------|-------------|---------|
| `qos` | MQTT Quality of Service level (0, 1, 2) | `1` |
| `retain` | Whether to retain messages | `false` |

### Session Management

| Option | Description | Default |
|--------|-------------|---------|
| `clean_session` | Use clean session | `true` |
| `keep_alive` | Keep-alive interval (seconds) | `60` |

### SSL/TLS Options

| Option | Description | Default |
|--------|-------------|---------|
| `use_ssl` | Enable SSL/TLS | `false` |
| `cert_path` | Client certificate path | None |
| `key_path` | Client private key path | None |
| `ca_cert_path` | CA certificate path | None |
| `verify_ssl` | Verify SSL certificates | `true` |

### Advanced Options

| Option | Description | Default |
|--------|-------------|---------|
| `connect_timeout_ms` | Connection timeout | `30000` |
| `message_timeout_ms` | Message timeout | `10000` |
| `will_topic` | Last Will and Testament topic | None |
| `will_message` | Last Will and Testament message | None |
| `will_qos` | LWT QoS level | `0` |
| `will_retain` | Retain LWT message | `false` |

## Client-Specific Options

### Reconnection

| Option | Description | Default |
|--------|-------------|---------|
| `auto_reconnect` | Enable automatic reconnection | `true` |
| `reconnect_delay_ms` | Initial reconnection delay | `1000` |
| `max_reconnect_attempts` | Maximum reconnection attempts (0 = infinite) | `0` |
| `backoff_factor` | Exponential backoff factor | `2.0` |
| `max_reconnect_delay_ms` | Maximum reconnection delay | `30000` |

### Message Handling

| Option | Description | Default |
|--------|-------------|---------|
| `max_inflight_messages` | Maximum in-flight messages | `10` |
| `message_retry_interval_ms` | Message retry interval | `5000` |
| `max_message_retries` | Maximum message retries | `3` |

## Examples

### Basic Echo Server

```c
#include "mcp_server.h"
#include "mcp_mqtt_transport.h"

int main() {
    // Create MQTT transport
    mcp_mqtt_config_t config = MCP_MQTT_CONFIG_DEFAULT;
    config.host = "localhost";
    config.port = 1883;
    
    mcp_transport_t* transport = mcp_transport_mqtt_create(&config);
    if (!transport) {
        fprintf(stderr, "Failed to create MQTT transport\n");
        return 1;
    }
    
    // Create and configure server
    mcp_server_config_t server_config = {
        .name = "MQTT Echo Server",
        .version = "1.0.0"
    };
    
    mcp_server_capabilities_t capabilities = {
        .tools_supported = true,
        .resources_supported = false
    };
    
    mcp_server_t* server = mcp_server_create(&server_config, &capabilities);
    if (!server) {
        fprintf(stderr, "Failed to create server\n");
        mcp_transport_destroy(transport);
        return 1;
    }
    
    // Start server
    if (mcp_server_start(server, transport) != 0) {
        fprintf(stderr, "Failed to start server\n");
        mcp_server_destroy(server);
        return 1;
    }
    
    printf("MQTT server started. Press Ctrl+C to stop.\n");
    
    // Wait for shutdown signal
    while (1) {
        sleep(1);
    }
    
    mcp_server_destroy(server);
    return 0;
}
```

### Basic Client

```c
#include "mcp_client.h"
#include "mcp_mqtt_client_transport.h"

int main() {
    // Create MQTT client transport
    mcp_mqtt_client_config_t config = MCP_MQTT_CLIENT_CONFIG_DEFAULT;
    config.base.host = "localhost";
    config.base.port = 1883;
    
    mcp_transport_t* transport = mcp_transport_mqtt_client_create_with_config(&config);
    if (!transport) {
        fprintf(stderr, "Failed to create MQTT client transport\n");
        return 1;
    }
    
    // Create client
    mcp_client_config_t client_config = {0};
    client_config.request_timeout_ms = 30000;
    
    mcp_client_t* client = mcp_client_create(&client_config, transport);
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    // List available tools
    mcp_tool_t** tools = NULL;
    size_t tool_count = 0;
    
    if (mcp_client_list_tools(client, &tools, &tool_count) == 0) {
        printf("Available tools:\n");
        for (size_t i = 0; i < tool_count; i++) {
            printf("  - %s: %s\n", tools[i]->name, 
                   tools[i]->description ? tools[i]->description : "No description");
            mcp_tool_free(tools[i]);
        }
        free(tools);
    }
    
    mcp_client_destroy(client);
    return 0;
}
```

## Troubleshooting

### Common Issues

1. **Connection Failed**: Check broker hostname, port, and network connectivity
2. **Authentication Failed**: Verify username and password
3. **SSL/TLS Errors**: Check certificate paths and broker SSL configuration
4. **Message Not Received**: Verify topic subscriptions and QoS levels
5. **High Latency**: Consider using QoS 0 for better performance

### Debug Logging

Enable debug logging to troubleshoot MQTT transport issues:

```c
mcp_log_init(NULL, MCP_LOG_LEVEL_DEBUG);
```

### Performance Tuning

- Use QoS 0 for best performance when message loss is acceptable
- Increase `max_inflight_messages` for high-throughput scenarios
- Adjust `keep_alive` interval based on network conditions
- Use persistent sessions for reliable message delivery

## Limitations

- QoS 2 support is limited (depends on libwebsockets implementation)
- Large message support may be limited by MQTT broker configuration
- Topic wildcards are not supported in the current implementation
- Shared subscriptions are not implemented

## Future Enhancements

- MQTT 5.0 protocol support
- Topic wildcard subscriptions
- Shared subscription support
- Message compression
- Enhanced metrics and monitoring
- Bridge mode for MQTT broker integration
