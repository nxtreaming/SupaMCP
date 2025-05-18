#ifndef MCP_WIN_SOCKET_COMPAT_H
#define MCP_WIN_SOCKET_COMPAT_H

/**
 * @file win_socket_compat.h
 * @brief Windows socket compatibility header
 *
 * This header ensures that winsock2.h is included before any other Windows headers
 * to prevent redefinition errors. Include this header first in any file that uses
 * Windows sockets.
 */

#ifdef _WIN32
    // Define WIN32_LEAN_AND_MEAN to exclude rarely-used stuff from Windows headers
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    // Prevent winsock.h from being included by windows.h
    #ifndef _WINSOCKAPI_
        #define _WINSOCKAPI_
    #endif

    // Prevent winsock.h from being included by windows.h
    #ifndef _WINSOCK_H
        #define _WINSOCK_H
    #endif

    // Define _WINSOCK2API_ to prevent winsock2.h from being included again
    #ifndef _WINSOCK2API_
        // Include winsock2.h first
        #include <winsock2.h>
        #include <ws2tcpip.h>
    #endif

    // Now it's safe to include windows.h
    #include <windows.h>

    // Define socklen_t for Windows
    #ifndef socklen_t
        typedef int socklen_t;
    #endif

	#define strcasecmp _stricmp
#endif

#endif // MCP_WIN_SOCKET_COMPAT_H
