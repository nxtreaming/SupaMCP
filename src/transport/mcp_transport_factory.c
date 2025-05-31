#include "mcp_transport_factory.h"
#include "mcp_stdio_transport.h"
#include "mcp_tcp_transport.h"
#include "mcp_tcp_client_transport.h"
#include "mcp_websocket_transport.h"
#include "mcp_websocket_connection_pool.h"
#include "mcp_http_transport.h"
#include "mcp_http_client_transport.h"
#include "mcp_http_streamable_transport.h"
#include <stdlib.h>

mcp_transport_t* mcp_transport_factory_create(
    mcp_transport_type_t type,
    const mcp_transport_config_t* config
) {
    if (config == NULL && type != MCP_TRANSPORT_STDIO) {
        return NULL;
    }

    switch (type) {
        case MCP_TRANSPORT_STDIO:
            return mcp_transport_stdio_create();

        case MCP_TRANSPORT_TCP:
            if (config == NULL) {
                return NULL;
            }
            return mcp_transport_tcp_create(
                config->tcp.host,
                config->tcp.port,
                config->tcp.idle_timeout_ms
            );

        case MCP_TRANSPORT_TCP_CLIENT:
            if (config == NULL) {
                return NULL;
            }
            return mcp_transport_tcp_client_create(
                config->tcp.host,
                config->tcp.port
            );

        case MCP_TRANSPORT_WS_SERVER:
            if (config == NULL) {
                return NULL;
            }
            {
                // Convert from transport factory config to WebSocket config
                mcp_websocket_config_t ws_config = {
                    .host = config->ws.host,
                    .port = config->ws.port,
                    .path = config->ws.path,
                    .origin = config->ws.origin,
                    .protocol = config->ws.protocol,
                    .use_ssl = config->ws.use_ssl ? true : false,
                    .cert_path = config->ws.cert_path,
                    .key_path = config->ws.key_path,
                    .connect_timeout_ms = config->ws.connect_timeout_ms
                };
                return mcp_transport_websocket_server_create(&ws_config);
            }

        case MCP_TRANSPORT_WS_CLIENT:
            if (config == NULL) {
                return NULL;
            }
            {
                // Convert from transport factory config to WebSocket config
                mcp_websocket_config_t ws_config = {
                    .host = config->ws.host,
                    .port = config->ws.port,
                    .path = config->ws.path,
                    .origin = config->ws.origin,
                    .protocol = config->ws.protocol,
                    .use_ssl = config->ws.use_ssl ? true : false,
                    .cert_path = config->ws.cert_path,
                    .key_path = config->ws.key_path,
                    .connect_timeout_ms = config->ws.connect_timeout_ms
                };
                return mcp_transport_websocket_client_create(&ws_config);
            }

        case MCP_TRANSPORT_HTTP_SERVER:
            if (config == NULL) {
                return NULL;
            }
            {
                // Convert from transport factory config to HTTP config
                mcp_http_config_t http_config = {
                    .host = config->http.host,
                    .port = config->http.port,
                    .use_ssl = config->http.use_ssl ? true : false,
                    .cert_path = config->http.cert_path,
                    .key_path = config->http.key_path,
                    .doc_root = config->http.doc_root,
                    .timeout_ms = config->http.timeout_ms
                };
                return mcp_transport_http_create(&http_config);
            }

        case MCP_TRANSPORT_WS_POOL:
            if (config == NULL) {
                return NULL;
            }
            {
                // Create WebSocket connection pool configuration
                mcp_ws_pool_config_t pool_config = {
                    .min_connections = config->ws_pool.min_connections,
                    .max_connections = config->ws_pool.max_connections,
                    .idle_timeout_ms = config->ws_pool.idle_timeout_ms,
                    .health_check_ms = config->ws_pool.health_check_ms,
                    .connect_timeout_ms = config->ws_pool.connect_timeout_ms,
                    .ws_config = {
                        .host = config->ws_pool.host,
                        .port = config->ws_pool.port,
                        .path = config->ws_pool.path,
                        .origin = config->ws_pool.origin,
                        .protocol = config->ws_pool.protocol,
                        .use_ssl = config->ws_pool.use_ssl ? true : false,
                        .cert_path = config->ws_pool.cert_path,
                        .key_path = config->ws_pool.key_path,
                        .connect_timeout_ms = config->ws_pool.connect_timeout_ms
                    }
                };

                // Create connection pool
                mcp_ws_connection_pool_t* pool = mcp_ws_connection_pool_create(&pool_config);
                if (!pool) {
                    return NULL;
                }

                // Get a connection from the pool
                return mcp_ws_connection_pool_get(pool, pool_config.connect_timeout_ms);
            }

        case MCP_TRANSPORT_HTTP_CLIENT:
            if (config == NULL) {
                return NULL;
            }
            {
                // Convert from transport factory config to HTTP client config
                mcp_http_client_config_t http_client_config = {
                    .host = config->http_client.host,
                    .port = config->http_client.port,
                    .use_ssl = config->http_client.use_ssl ? true : false,
                    .cert_path = config->http_client.cert_path,
                    .key_path = config->http_client.key_path,
                    .timeout_ms = config->http_client.timeout_ms,
                    .api_key = config->http_client.api_key
                };
                return mcp_transport_http_client_create_with_config(&http_client_config);
            }

        case MCP_TRANSPORT_HTTP_STREAMABLE:
            if (config == NULL) {
                return NULL;
            }
            {
                // Convert from transport factory config to HTTP Streamable config
                mcp_http_streamable_config_t streamable_config = {
                    .host = config->http_streamable.host,
                    .port = config->http_streamable.port,
                    .use_ssl = config->http_streamable.use_ssl ? true : false,
                    .cert_path = config->http_streamable.cert_path,
                    .key_path = config->http_streamable.key_path,
                    .doc_root = config->http_streamable.doc_root,
                    .timeout_ms = config->http_streamable.timeout_ms,
                    .mcp_endpoint = config->http_streamable.mcp_endpoint,
                    .enable_sessions = config->http_streamable.enable_sessions ? true : false,
                    .session_timeout_seconds = config->http_streamable.session_timeout_seconds,
                    .validate_origin = config->http_streamable.validate_origin ? true : false,
                    .allowed_origins = config->http_streamable.allowed_origins,
                    .enable_cors = config->http_streamable.enable_cors ? true : false,
                    .cors_allow_origin = config->http_streamable.cors_allow_origin,
                    .cors_allow_methods = config->http_streamable.cors_allow_methods,
                    .cors_allow_headers = config->http_streamable.cors_allow_headers,
                    .cors_max_age = config->http_streamable.cors_max_age,
                    .enable_sse_resumability = config->http_streamable.enable_sse_resumability ? true : false,
                    .max_stored_events = config->http_streamable.max_stored_events,
                    .send_heartbeats = config->http_streamable.send_heartbeats ? true : false,
                    .heartbeat_interval_ms = config->http_streamable.heartbeat_interval_ms,
                    .enable_legacy_endpoints = config->http_streamable.enable_legacy_endpoints ? true : false
                };
                return mcp_transport_http_streamable_create(&streamable_config);
            }

        default:
            // Unsupported transport type
            return NULL;
    }
}
