# Python bindings for KMCP
cmake_minimum_required(VERSION 3.10)

# We only need to copy the KMCP library to the correct location
# since we're using ctypes for bindings

# Create lib directory if it doesn't exist
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/lib)

# Create a custom target for copying the library
add_custom_target(copy_kmcp_lib ALL)

# Copy KMCP library and its dependencies to the lib directory
if(WIN32)
    add_custom_command(
        TARGET copy_kmcp_lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:kmcp>
        ${CMAKE_CURRENT_SOURCE_DIR}/build/lib/kmcp.dll
        DEPENDS kmcp
    )
    add_custom_command(
        TARGET copy_kmcp_lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:mcpcore>
        ${CMAKE_CURRENT_SOURCE_DIR}/build/lib/mcpcore.dll
        DEPENDS mcpcore
    )
elseif(APPLE)
    add_custom_command(
        TARGET copy_kmcp_lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:kmcp>
        ${CMAKE_CURRENT_SOURCE_DIR}/build/lib/libkmcp.dylib
        DEPENDS kmcp
    )
    add_custom_command(
        TARGET copy_kmcp_lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:mcpcore>
        ${CMAKE_CURRENT_SOURCE_DIR}/build/lib/libmcpcore.dylib
        DEPENDS mcpcore
    )
else()
    add_custom_command(
        TARGET copy_kmcp_lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:kmcp>
        ${CMAKE_CURRENT_SOURCE_DIR}/build/lib/libkmcp.so
        DEPENDS kmcp
    )
    add_custom_command(
        TARGET copy_kmcp_lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:mcpcore>
        ${CMAKE_CURRENT_SOURCE_DIR}/build/lib/libmcpcore.so
        DEPENDS mcpcore
    )
endif()
