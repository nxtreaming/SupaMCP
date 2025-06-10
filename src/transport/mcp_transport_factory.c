#include "mcp_transport_factory.h"
#include "mcp_stdio_transport.h"
#include "mcp_tcp_transport.h"
#include "mcp_tcp_client_transport.h"
#include "mcp_websocket_transport.h"
#include "mcp_websocket_connection_pool.h"
#include "mcp_http_transport.h"
#include "mcp_http_client_transport.h"
#include "mcp_sthttp_transport.h"
#include "mcp_sthttp_client_transport.h"
#include "mcp_mqtt_transport.h"
#include "mcp_mqtt_client_transport.h"
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

        case MCP_TRANSPORT_STHTTP:
            if (config == NULL) {
                return NULL;
            }
            {
                // Convert from transport factory config to HTTP Streamable config
                mcp_sthttp_config_t streamable_config = {
                    .host = config->sthttp.host,
                    .port = config->sthttp.port,
                    .use_ssl = config->sthttp.use_ssl ? true : false,
                    .cert_path = config->sthttp.cert_path,
                    .key_path = config->sthttp.key_path,
                    .doc_root = config->sthttp.doc_root,
                    .timeout_ms = config->sthttp.timeout_ms,
                    .mcp_endpoint = config->sthttp.mcp_endpoint,
                    .enable_sessions = config->sthttp.enable_sessions ? true : false,
                    .session_timeout_seconds = config->sthttp.session_timeout_seconds,
                    .validate_origin = config->sthttp.validate_origin ? true : false,
                    .allowed_origins = config->sthttp.allowed_origins,
                    .enable_cors = config->sthttp.enable_cors ? true : false,
                    .cors_allow_origin = config->sthttp.cors_allow_origin,
                    .cors_allow_methods = config->sthttp.cors_allow_methods,
                    .cors_allow_headers = config->sthttp.cors_allow_headers,
                    .cors_max_age = config->sthttp.cors_max_age,
                    .enable_sse_resumability = config->sthttp.enable_sse_resumability ? true : false,
                    .max_sse_clients = config->sthttp.max_sse_clients,
                    .max_stored_events = config->sthttp.max_stored_events,
                    .send_heartbeats = config->sthttp.send_heartbeats ? true : false,
                    .heartbeat_interval_ms = config->sthttp.heartbeat_interval_ms,
                    .enable_legacy_endpoints = config->sthttp.enable_legacy_endpoints ? true : false
                };
                return mcp_transport_sthttp_create(&streamable_config);
            }

        case MCP_TRANSPORT_STHTTP_CLIENT:
            if (config == NULL) {
                return NULL;
            }
            {
                // Convert from transport factory config to HTTP Streamable client config
                mcp_sthttp_client_config_t client_config = {
                    .host = config->sthttp_client.host,
                    .port = config->sthttp_client.port,
                    .use_ssl = config->sthttp_client.use_ssl ? true : false,
                    .cert_path = config->sthttp_client.cert_path,
                    .key_path = config->sthttp_client.key_path,
                    .ca_cert_path = config->sthttp_client.ca_cert_path,
                    .verify_ssl = config->sthttp_client.verify_ssl ? true : false,
                    .mcp_endpoint = config->sthttp_client.mcp_endpoint,
                    .user_agent = config->sthttp_client.user_agent,
                    .api_key = config->sthttp_client.api_key,
                    .connect_timeout_ms = config->sthttp_client.connect_timeout_ms,
                    .request_timeout_ms = config->sthttp_client.request_timeout_ms,
                    .sse_reconnect_delay_ms = config->sthttp_client.sse_reconnect_delay_ms,
                    .max_reconnect_attempts = config->sthttp_client.max_reconnect_attempts,
                    .enable_sessions = config->sthttp_client.enable_sessions ? true : false,
                    .enable_sse_streams = config->sthttp_client.enable_sse_streams ? true : false,
                    .auto_reconnect_sse = config->sthttp_client.auto_reconnect_sse ? true : false,
                    .custom_headers = config->sthttp_client.custom_headers
                };
                return mcp_transport_sthttp_client_create(&client_config);
            }

        case MCP_TRANSPORT_MQTT_SERVER:
            // MQTT server transport has been removed.
            // Use external MQTT broker with MCP_TRANSPORT_MQTT_CLIENT instead.
            return NULL;

        case MCP_TRANSPORT_MQTT_CLIENT:
            if (config == NULL) {
                return NULL;
            }
            {
                // Convert from transport factory config to MQTT config
                mcp_mqtt_config_t mqtt_config = {
                    .host = config->mqtt.host,
                    .port = config->mqtt.port,
                    .client_id = config->mqtt.client_id,
                    .username = config->mqtt.username,
                    .password = config->mqtt.password,
                    .topic_prefix = config->mqtt.topic_prefix,
                    .request_topic = config->mqtt.request_topic,
                    .response_topic = config->mqtt.response_topic,
                    .notification_topic = config->mqtt.notification_topic,
                    .keep_alive = config->mqtt.keep_alive,
                    .clean_session = config->mqtt.clean_session ? true : false,
                    .use_ssl = config->mqtt.use_ssl ? true : false,
                    .cert_path = config->mqtt.cert_path,
                    .key_path = config->mqtt.key_path,
                    .ca_cert_path = config->mqtt.ca_cert_path,
                    .verify_ssl = config->mqtt.verify_ssl ? true : false,
                    .connect_timeout_ms = config->mqtt.connect_timeout_ms,
                    .message_timeout_ms = config->mqtt.message_timeout_ms,
                    .qos = config->mqtt.qos,
                    .retain = config->mqtt.retain ? true : false,
                    .will_topic = config->mqtt.will_topic,
                    .will_message = config->mqtt.will_message,
                    .will_qos = config->mqtt.will_qos,
                    .will_retain = config->mqtt.will_retain ? true : false
                };
                return mcp_transport_mqtt_client_create(&mqtt_config);
            }

        default:
            // Unsupported transport type
            return NULL;
    }
}
