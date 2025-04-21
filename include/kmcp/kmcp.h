/**
 * @file kmcp.h
 * @brief KMCP (Kernel MCP) main header file
 *
 * KMCP (Kernel MCP) is a module of the SupaMCPServer project that provides
 * a set of APIs for managing MCP servers and clients. This header file includes
 * all the necessary headers for using KMCP.
 *
 * @note This header is the main entry point for using KMCP. Include this header
 * in your application to access all KMCP functionality.
 */

#ifndef KMCP_H
#define KMCP_H

#include "kmcp_error.h"
#include "kmcp_common.h"
#include "kmcp_server_manager.h"
#include "kmcp_client.h"
#include "kmcp_tool_access.h"
#include "kmcp_config_parser.h"
#include "kmcp_process.h"
#include "kmcp_http_client.h"
#include "kmcp_profile_manager.h"
#include "kmcp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get KMCP version information
 *
 * This function returns the version information of the KMCP library.
 *
 * @return const char* KMCP version string
 */
const char* kmcp_get_version(void);

/**
 * @brief Get KMCP build information
 *
 * This function returns the build information of the KMCP library.
 *
 * @return const char* KMCP build information string
 */
const char* kmcp_get_build_info(void);

#ifdef __cplusplus
}
#endif

#endif /* KMCP_H */
