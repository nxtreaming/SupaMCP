# KMCP Configuration Guide

This document describes the configuration file format and options for the KMCP module, helping developers correctly configure KMCP clients.

## Configuration File Format

KMCP configuration files use JSON format and contain the following main sections:

- `clientConfig`: Client configuration
- `mcpServers`: Server configuration
- `toolAccessControl`: Tool access control configuration

## Configuration File Example

```json
{
  "clientConfig": {
    "clientName": "example-client",
    "clientVersion": "1.0.0",
    "useServerManager": true,
    "requestTimeoutMs": 30000
  },
  "mcpServers": {
    "local": {
      "command": "mcp_server",
      "args": ["--tcp", "--port", "8080"],
      "env": {
        "MCP_DEBUG": "1"
      }
    },
    "remote": {
      "url": "http://example.com:8080"
    }
  },
  "toolAccessControl": {
    "defaultAllow": false,
    "allowedTools": ["echo", "calculator", "translator"],
    "disallowedTools": ["dangerous_tool", "system_tool"]
  }
}
```

## Client Configuration

The `clientConfig` section contains basic client configuration:

| Field | Type | Required | Default | Description |
|------|------|------|--------|------|
| `clientName` | String | Yes | - | Client name |
| `clientVersion` | String | Yes | - | Client version |
| `useServerManager` | Boolean | No | `true` | Whether to use server manager |
| `requestTimeoutMs` | Integer | No | `30000` | Request timeout in milliseconds |

Example:

```json
"clientConfig": {
  "clientName": "example-client",
  "clientVersion": "1.0.0",
  "useServerManager": true,
  "requestTimeoutMs": 30000
}
```

## Server Configuration

The `mcpServers` section contains one or more server configurations, each with a unique name:

```json
"mcpServers": {
  "server1": { ... },
  "server2": { ... }
}
```

### Local Server Configuration

Local servers are MCP server processes started and managed by the KMCP client:

| Field | Type | Required | Description |
|------|------|------|------|
| `command` | String | Yes | Server command path |
| `args` | String array | No | Command arguments |
| `env` | Object | No | Environment variables |

Example:

```json
"local": {
  "command": "mcp_server",
  "args": ["--tcp", "--port", "8080"],
  "env": {
    "MCP_DEBUG": "1"
  }
}
```

### Remote Server Configuration

Remote servers are already running MCP servers that the KMCP client connects to via HTTP:

| Field | Type | Required | Description |
|------|------|------|------|
| `url` | String | Yes | Server URL |

Example:

```json
"remote": {
  "url": "http://example.com:8080"
}
```

## Tool Access Control Configuration

The `toolAccessControl` section configures tool access control policies:

| Field | Type | Required | Default | Description |
|------|------|------|--------|------|
| `defaultAllow` | Boolean | No | `true` | Default allow policy |
| `allowedTools` | String array | No | `[]` | List of allowed tools |
| `disallowedTools` | String array | No | `[]` | List of disallowed tools |

Example:

```json
"toolAccessControl": {
  "defaultAllow": false,
  "allowedTools": ["echo", "calculator", "translator"],
  "disallowedTools": ["dangerous_tool", "system_tool"]
}
```

### Access Control Policy

Tool access control policy works as follows:

1. If a tool is in the `allowedTools` list, access is allowed
2. If a tool is in the `disallowedTools` list, access is denied
3. If a tool is neither in the `allowedTools` list nor in the `disallowedTools` list, access is determined by the `defaultAllow` setting

## Configuration File Paths

Configuration files can be placed in the following locations:

1. Path specified by the application
2. Current working directory
3. User home directory under `.kmcp/config.json`
4. System configuration directory under `kmcp/config.json`

The KMCP client searches for configuration files in the above order and uses the first one found.

## Environment Variables

The KMCP client supports overriding certain settings in the configuration file through environment variables:

| Environment Variable | Corresponding Configuration | Description |
|----------|----------|------|
| `KMCP_CLIENT_NAME` | `clientConfig.clientName` | Client name |
| `KMCP_CLIENT_VERSION` | `clientConfig.clientVersion` | Client version |
| `KMCP_USE_SERVER_MANAGER` | `clientConfig.useServerManager` | Whether to use server manager |
| `KMCP_REQUEST_TIMEOUT_MS` | `clientConfig.requestTimeoutMs` | Request timeout in milliseconds |
| `KMCP_DEFAULT_ALLOW` | `toolAccessControl.defaultAllow` | Default allow policy |

## Configuration Validation

The KMCP client validates the configuration file when loading it to ensure the format is correct. If the configuration file format is incorrect, the KMCP client logs an error and uses default configuration.

## Configuration Best Practices

1. **Use Descriptive Server Names**: Use descriptive names for each server for easy identification and management
2. **Restrict Tool Access**: Use `defaultAllow: false` and explicitly list allowed tools to improve security
3. **Set Reasonable Timeouts**: Set reasonable timeout values based on network environment and server response times
4. **Use Absolute Paths**: For local server `command`, use absolute paths to ensure the server executable can be found correctly
5. **Use Environment Variables for Sensitive Information**: For sensitive information, such as API keys, use environment variables instead of writing them directly in the configuration file
