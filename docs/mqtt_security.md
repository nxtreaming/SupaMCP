# MQTT Security Guide for SupaMCP

## 🚨 Security Risks in MQTT Pub/Sub

### 1. Topic Enumeration Attack
```
Malicious clients can:
- Subscribe to wildcard topics: mcp/+/+, mcp/#
- Eavesdrop on all MCP communications
- Obtain sensitive data (API keys, authentication info, etc.)
```

### 2. Message Injection Attack
```
Attackers can:
- Publish fake responses to mcp/response/client_123
- Trick clients into receiving incorrect data
- Execute man-in-the-middle attacks
```

### 3. Client ID Prediction
```
Predictable client IDs:
mcp_client_001, mcp_client_002...
→ Attackers can guess and subscribe to corresponding topics
```

## 🛡️ SupaMCP MQTT Security Mechanisms

### 1. Topic Randomization
```c
// Original topics (insecure)
mcp/request/mcp_client_001
mcp/response/mcp_client_001

// Secure topics (with random token)
mcp/request/mcp_client_001_a7f9e2d8x3k5m1n4
mcp/response/mcp_client_001_a7f9e2d8x3k5m1n4
```

**Implementation**:
- 16-character random security token
- New token generated for each connection
- Token contains alphanumeric combinations

### 2. MQTT Broker ACL Configuration
```bash
# mosquitto ACL configuration example
# /etc/mosquitto/acl.conf

# Prohibit wildcard subscriptions
pattern read mcp/+/+
pattern read mcp/#

# User-based topic permissions
user mcp_client_001
topic readwrite mcp/request/mcp_client_001_*
topic read mcp/response/mcp_client_001_*
topic read mcp/notification/mcp_client_001_*

user mcp_server_001
topic read mcp/request/+
topic readwrite mcp/response/+
topic readwrite mcp/notification/+
```

### 3. TLS/SSL Encryption
```bash
# Enable SSL MQTT connection
./mcp_client --mqtt --host 127.0.0.1 --port 8883 --mqtt-ssl \
  --mqtt-ca-cert /path/to/ca.crt \
  --mqtt-client-cert /path/to/client.crt \
  --mqtt-client-key /path/to/client.key
```

### 4. Username/Password Authentication
```bash
# MQTT connection with authentication
./mcp_client --mqtt --host 127.0.0.1 --port 1883 \
  --mqtt-username secure_client_001 \
  --mqtt-password "complex_password_123!"
```

### 5. Message Encryption (Application Layer)
```c
// Optional: Add encryption at MCP message layer
typedef struct {
    char* encrypted_payload;
    char* iv;  // Initialization vector
    char* hmac; // Message authentication code
} mcp_encrypted_message_t;
```

## 🔧 MQTT Broker Security Configuration

### Mosquitto Security Configuration
```bash
# /etc/mosquitto/mosquitto.conf

# Basic security settings
allow_anonymous false
password_file /etc/mosquitto/passwd
acl_file /etc/mosquitto/acl.conf

# TLS settings
listener 8883
cafile /etc/mosquitto/ca.crt
certfile /etc/mosquitto/server.crt
keyfile /etc/mosquitto/server.key
require_certificate true

# Connection limits
max_connections 1000
max_inflight_messages 100
max_queued_messages 1000

# Logging
log_type error
log_type warning
log_type notice
log_type information
log_dest file /var/log/mosquitto/mosquitto.log
```

### Creating Users and Passwords
```bash
# Create password file
sudo mosquitto_passwd -c /etc/mosquitto/passwd mcp_client_001
sudo mosquitto_passwd /etc/mosquitto/passwd mcp_server_001

# Set permissions
sudo chown mosquitto:mosquitto /etc/mosquitto/passwd
sudo chmod 600 /etc/mosquitto/passwd
```

## 🎯 Best Practices

### 1. Topic Naming Strategy
```
Recommended:
mcp/req/{random_token}/{client_id}
mcp/res/{random_token}/{client_id}

Avoid:
mcp/request/predictable_id
mcp/response/sequential_number
```

### 2. Connection Security
```bash
# Production environment recommended configuration
./mcp_client --mqtt \
  --host secure-mqtt.company.com \
  --port 8883 \
  --mqtt-ssl \
  --mqtt-username $(generate_secure_username) \
  --mqtt-password $(read_from_secure_store) \
  --mqtt-client-id $(generate_unique_id) \
  --mqtt-qos 1 \
  --mqtt-clean-session false
```

### 3. Network Isolation
```
Recommended network architecture:
┌─────────────┐    VPN/TLS    ┌─────────────┐    Private Net    ┌─────────────┐
│ MCP Client  │◄─────────────►│MQTT Broker  │◄─────────────────►│ MCP Server  │
│ (External)  │               │ (DMZ)       │                   │ (Internal)  │
└─────────────┘               └─────────────┘                   └─────────────┘
```

### 4. Monitoring and Auditing
```bash
# Monitor suspicious activities
- Wildcard subscription attempts
- Unauthorized topic access
- Abnormal connection patterns
- Unusual message frequency

# Logging
- All connection/disconnection events
- Topic subscription/unsubscription
- Authentication failures
- ACL violations
```

## ⚠️ Security Considerations

1. **Regular Token Rotation**: Security tokens should be updated periodically
2. **Principle of Least Privilege**: Only grant necessary topic permissions
3. **Monitor Anomalies**: Real-time monitoring of suspicious subscription patterns
4. **Encrypted Transport**: Production environments must use TLS
5. **Strong Authentication**: Use complex passwords and certificate authentication
6. **Network Isolation**: Place MQTT broker in protected network

## 🔍 Security Checklist

- [ ] Enable TLS/SSL encryption
- [ ] Configure username/password authentication
- [ ] Set up ACL permission control
- [ ] Disable anonymous connections
- [ ] Use random topic tokens
- [ ] Restrict wildcard subscriptions
- [ ] Enable connection logging
- [ ] Regularly audit permission configurations
- [ ] Monitor abnormal connection patterns
- [ ] Implement network isolation
