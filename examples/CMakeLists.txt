# Examples CMakeLists.txt

# Ensure the common library headers are available
# The target_include_directories in the parent CMakeLists.txt
# should make the 'include' directory available publicly.

# Example Echo Server
add_executable(echo_server echo_server.c)
target_link_libraries(echo_server PRIVATE mcp_common)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(echo_server PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(echo_server PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Example Echo Client
add_executable(echo_client echo_client.c)
target_include_directories(echo_client PRIVATE ../client/include) # Add client include path
target_link_libraries(echo_client PRIVATE mcp_common)
# Link platform-specific libraries needed by the transport
if(WIN32)
    target_link_libraries(echo_client PRIVATE ws2_32) # Use PRIVATE keyword
else()
    target_link_libraries(echo_client PRIVATE Threads::Threads) # Use PRIVATE keyword
endif()

# Template Demo
add_executable(template_demo template_demo.c)
target_link_libraries(template_demo PRIVATE mcp_common)

# Optional: Install examples
# install(TARGETS echo_server echo_client template_demo DESTINATION bin/examples)
