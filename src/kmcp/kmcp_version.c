/**
 * @file kmcp_version.c
 * @brief KMCP version information
 */

#include "kmcp.h"

/**
 * @brief KMCP version string
 */
static const char* KMCP_VERSION = "1.0.0";

/**
 * @brief KMCP build information string
 */
static const char* KMCP_BUILD_INFO = "Built on " __DATE__ " " __TIME__;

/**
 * @brief Get KMCP version information
 *
 * @return const char* Returns the KMCP version string
 */
const char* kmcp_get_version(void) {
    return KMCP_VERSION;
}

/**
 * @brief Get KMCP build information
 *
 * @return const char* Returns the KMCP build information string
 */
const char* kmcp_get_build_info(void) {
    return KMCP_BUILD_INFO;
}
