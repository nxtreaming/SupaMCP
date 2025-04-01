cmake_minimum_required(VERSION 3.10)
project(SupaMCPServer VERSION 1.0.0 LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add compiler flags
if(MSVC)
    # MSVC flags
    add_compile_options(/W4)
else()
    # GCC/Clang flags
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Check for pthreads on non-Windows platforms
if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/client/include
)

# Common library source files
set(COMMON_SOURCES
    src/mcp_json.c
    src/mcp_json_rpc.c
    src/mcp_transport.c
    src/mcp_stdio_transport.c
    src/mcp_tcp_transport.c
    src/mcp_tcp_client_transport.c
    src/mcp_types.c
    src/mcp_arena.c
    src/mcp_log.c
)

# Common library header files
set(COMMON_HEADERS
    include/mcp_json.h
    include/mcp_json_rpc.h
    include/mcp_transport.h
    include/mcp_stdio_transport.h
    include/mcp_tcp_transport.h
    include/mcp_types.h
    include/mcp_arena.h
    include/mcp_log.h
)

# Server-specific source files
set(SERVER_SOURCES
    src/main.c
    src/mcp_server.c
)

# Server-specific header files
set(SERVER_HEADERS
    include/mcp_server.h
)

# Client-specific source files
set(CLIENT_SOURCES
    client/src/main.c
    client/src/mcp_client.c
)

# Client-specific header files
set(CLIENT_HEADERS
    client/include/mcp_client.h
)

# Create common library
add_library(mcp_common STATIC ${COMMON_SOURCES} ${COMMON_HEADERS})

# Create server executable
add_executable(mcp_server ${SERVER_SOURCES} ${SERVER_HEADERS})
target_link_libraries(mcp_server mcp_common)

# Create client executable
add_executable(mcp_client ${CLIENT_SOURCES} ${CLIENT_HEADERS})
target_link_libraries(mcp_client mcp_common)
# Explicitly add include directories for the client target
target_include_directories(mcp_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/client/include
)

# Link platform-specific libraries
if(WIN32)
    target_link_libraries(mcp_common ws2_32)
else()
    target_link_libraries(mcp_common ${CMAKE_THREAD_LIBS_INIT})
endif()

# Install
install(TARGETS mcp_server mcp_client DESTINATION bin)

# --- Testing Setup ---
enable_testing()

# Add Unity library
set(UNITY_SOURCES tests/unity/unity.c)
set(UNITY_HEADERS tests/unity/unity.h tests/unity/unity_internals.h)
add_library(unity_lib STATIC ${UNITY_SOURCES} ${UNITY_HEADERS})
target_include_directories(unity_lib PUBLIC tests/unity) # Make headers available

# Add tests subdirectory (will contain test runner definition)
add_subdirectory(tests)

# Print configuration
message(STATUS "CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
message(STATUS "CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
