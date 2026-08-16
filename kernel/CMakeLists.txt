# kernel/CMakeLists.txt - Updated with IFS and ext4 support

cmake_minimum_required(VERSION 3.21)

# ── Kernel source files ───────────────────────────────────────────────────────
set(KERNEL_SOURCES
        src/main.c
        src/boot.S
        src/mem.c
        src/uart.c
        src/scheduler.c
        src/keyboard.c
        src/vfs.c
        src/blkdev.c
        src/gpt.c
        src/string.c
        src/kprintf.c
        src/ifs_loader.c        # ← IFS loader
        src/ai.c                # ← on-box AI assistant command
        src/basic.c
        src/kio.c
        src/lx_loader.c         # ← built-in OS/2 LX (.EXE) loader
        src/stack_protector.c   # ← -fstack-protector-strong runtime support

        # Add GUI sources:
        src/gui/framebuffer.c
        src/gui/wayland.c
        src/gui/gui_demo.c
        src/gui/ramfb.c
        src/gui/virtio_input.c
        src/gui/virtio_gpu.c
        ../drivers/video/virtiofb.c
)

# ── Add string.c if it exists ─────────────────────────────────────────────────
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/string.c)
    list(APPEND KERNEL_SOURCES src/string.c)
endif()

# ── Platform-specific sources ─────────────────────────────────────────────────
if(TARGET_PLATFORM STREQUAL "RPI3")
    list(APPEND KERNEL_SOURCES
            ../rpi/rpi_boot.S
            ../rpi/rpi_main.c
    )
    set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/../rpi/rpi_linker.ld)
else()
    set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
endif()

# ── Kernel executable ─────────────────────────────────────────────────────────
add_executable(os2warp_kernel ${KERNEL_SOURCES})

# ── Include directories ───────────────────────────────────────────────────────
target_include_directories(os2warp_kernel PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/fs/ext4
        ${CMAKE_SOURCE_DIR}/fs/btrfs
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(os2warp_kernel
        os2api
        dos             # ← DOS command set (calls os2api, not vfs_* directly)
        rexx
        ext4            # ← ext4 filesystem driver
        btrfs           # ← btrfs filesystem driver
)

# ── Linker options ────────────────────────────────────────────────────────────
target_link_options(os2warp_kernel PRIVATE
        -T ${LINKER_SCRIPT}
        -nostdlib
        -static
)

# ── Set linker script dependency ──────────────────────────────────────────────
set_target_properties(os2warp_kernel PROPERTIES
        LINK_DEPENDS ${LINKER_SCRIPT}
)

# ── Print configuration ───────────────────────────────────────────────────────
message(STATUS "Kernel configuration:")
message(STATUS "  Platform:      ${TARGET_PLATFORM}")
message(STATUS "  Linker script: ${LINKER_SCRIPT}")
message(STATUS "  Sources:       ${KERNEL_SOURCES}")