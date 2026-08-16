# fs/btrfs/CMakeLists.txt - btrfs Filesystem Driver

# btrfs driver sources
set(BTRFS_SOURCES
        btrfs.c
        btrfs_write.c
)

# btrfs driver headers
set(BTRFS_HEADERS
        btrfs.h
        btrfs_disk.h
        btrfs_internal.h
)

# Create btrfs driver library
add_library(btrfs STATIC ${BTRFS_SOURCES})

# Include directories
target_include_directories(btrfs PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/kernel/include
)

# Compiler flags for btrfs
target_compile_options(btrfs PRIVATE
        -Wall
        -Wextra
        -Werror=implicit-function-declaration
)

# Install headers to kernel include directory (optional)
install(FILES ${BTRFS_HEADERS}
        DESTINATION ${CMAKE_SOURCE_DIR}/kernel/include/fs
)

message(STATUS "btrfs filesystem driver configured")
