# ============================================================================
# kernel/commands/CMakeLists.txt - Commands without linking
# ============================================================================

message(STATUS "Commands build using: ${CMAKE_C_COMPILER}")

# Find lxbuild tool - use full path
if(CMAKE_CROSSCOMPILING)
    set(LXBUILD ${CMAKE_BINARY_DIR}/lx-host-build/lxbuild)
    set(LX_TOOLS_DEP lx_host_tools)
else()
    set(LXBUILD ${CMAKE_BINARY_DIR}/lx/lxbuild)
    set(LX_TOOLS_DEP lxbuild)
endif()

message(STATUS "LXBUILD tool: ${LXBUILD}")

# Use native ARM64
set(CMD_C_FLAGS
        -arch arm64
        -ffreestanding
        -nostdlib
        -fno-stack-protector
        -Os
)

set(CMD_INCLUDES
        "-I${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "-I${CMAKE_SOURCE_DIR}/rexx"
        "-I${CMAKE_SOURCE_DIR}/os2"
)

set(DEFAULT_STACK 65536)
set(LARGE_STACK   131072)

# ── Helper Functions ──────────────────────────────────────────────────────────

function(build_lx_exe EXE_NAME SOURCE_FILE STACK_SIZE)
    string(TOUPPER ${EXE_NAME} TARGET_NAME)

    # Compile to object file only
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.o
            COMMAND ${CMAKE_C_COMPILER}
            ${CMD_C_FLAGS}
            ${CMD_INCLUDES}
            -c ${CMAKE_CURRENT_SOURCE_DIR}/../src/${SOURCE_FILE}
            -o ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.o
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../src/${SOURCE_FILE}
            COMMENT "Compiling ${SOURCE_FILE} for ${EXE_NAME}.EXE"
            VERBATIM
    )

    # Extract raw binary directly from object file
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.bin
            COMMAND ${CMAKE_OBJCOPY}
            -O binary
            ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.o
            ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.bin
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.o
            COMMENT "Extracting binary from object file"
            VERBATIM
    )

    # Create LX executable
    # Note: We can't use DEPENDS with ExternalProject targets in custom commands
    # Instead, we make the target depend on LX_TOOLS_DEP
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${EXE_NAME}.EXE
            COMMAND ${LXBUILD}
            -o ${EXE_NAME}.EXE
            --entry 0
            --stack ${STACK_SIZE}
            ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.bin
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.bin
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Creating ${EXE_NAME}.EXE with lxbuild"
            VERBATIM
    )

    add_custom_target(${TARGET_NAME}_exe ALL
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${EXE_NAME}.EXE
    )

    # Add dependency on lx tools
    add_dependencies(${TARGET_NAME}_exe ${LX_TOOLS_DEP})

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${EXE_NAME}.EXE DESTINATION bin)
endfunction()

function(build_lx_exe_multi EXE_NAME STACK_SIZE)
    string(TOUPPER ${EXE_NAME} TARGET_NAME)
    set(SOURCE_FILES ${ARGN})
    set(OBJECT_FILES "")

    # Compile each source
    foreach(SOURCE_FILE ${SOURCE_FILES})
        get_filename_component(BASE_NAME ${SOURCE_FILE} NAME_WE)
        set(OBJ_FILE ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_${BASE_NAME}.o)

        add_custom_command(
                OUTPUT ${OBJ_FILE}
                COMMAND ${CMAKE_C_COMPILER}
                ${CMD_C_FLAGS}
                ${CMD_INCLUDES}
                -c ${CMAKE_CURRENT_SOURCE_DIR}/../src/${SOURCE_FILE}
                -o ${OBJ_FILE}
                DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../src/${SOURCE_FILE}
                COMMENT "Compiling ${SOURCE_FILE}"
                VERBATIM
        )
        list(APPEND OBJECT_FILES ${OBJ_FILE})
    endforeach()

    # Combine objects
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_combined.o
            COMMAND ld -r -arch arm64 ${OBJECT_FILES}
            -o ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_combined.o
            DEPENDS ${OBJECT_FILES}
            COMMENT "Combining objects"
            VERBATIM
    )

    # Extract binary
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.bin
            COMMAND ${CMAKE_OBJCOPY}
            -O binary
            ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_combined.o
            ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.bin
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_combined.o
            COMMENT "Extracting binary"
            VERBATIM
    )

    # Create LX executable
    add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${EXE_NAME}.EXE
            COMMAND ${LXBUILD}
            -o ${EXE_NAME}.EXE
            --entry 0
            --stack ${STACK_SIZE}
            ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.bin
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.bin
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Creating ${EXE_NAME}.EXE with lxbuild"
            VERBATIM
    )

    add_custom_target(${TARGET_NAME}_exe ALL
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${EXE_NAME}.EXE
    )

    # Add dependency on lx tools
    add_dependencies(${TARGET_NAME}_exe ${LX_TOOLS_DEP})

    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${EXE_NAME}.EXE DESTINATION bin)
endfunction()

# ══════════════════════════════════════════════════════════════════════════════

build_lx_exe(BAS basic.c ${LARGE_STACK})

build_lx_exe_multi(CLI ${LARGE_STACK}
        cli.c
        cli_fs.c
)

add_custom_target(all_commands
        DEPENDS BAS_exe CLI_exe
)

message(STATUS "Commands: BAS.EXE, CLI.EXE")