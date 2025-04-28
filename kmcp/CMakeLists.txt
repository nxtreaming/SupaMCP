# KMCP Module CMakeLists.txt

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/client/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# No need to define sources or create the library here
# as it's already defined in the parent CMakeLists.txt

# Add include directories for internal headers
target_include_directories(kmcp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
