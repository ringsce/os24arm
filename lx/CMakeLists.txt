# ============================================================================
# lx/CMakeLists.txt  —  LX Executable Builder Tools (STANDALONE BUILD)
# ============================================================================

cmake_minimum_required(VERSION 3.10)
project(lx_tools C)

# Compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -O2)

# ── LX Builder Library ───────────────────────────────────────────────────────

add_library(lxlib STATIC
        lxbuilder.c
        lxformat.h
)

target_include_directories(lxlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ── Command-Line Tools ───────────────────────────────────────────────────────

# Main LX builder tool
add_executable(lxbuild
        lxbuild.c
)
target_link_libraries(lxbuild lxlib)

# Simple EXE maker
add_executable(makeexe
        makeexe.c
)
target_link_libraries(makeexe lxlib)

# Simple DLL maker
add_executable(makedll
        makedll.c
)
target_link_libraries(makedll lxlib)

# ── Test Targets ─────────────────────────────────────────────────────────────

# Create test binary data (ARM64 RET instruction: 0xD65F03C0 in little-endian: C0 03 5F D6)
add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test_code.bin
        COMMAND printf "\\xC0\\x03\\x5F\\xD6" > ${CMAKE_CURRENT_BINARY_DIR}/test_code.bin
        COMMENT "Generating test binary code"
        VERBATIM
)

# Test executable creation
add_custom_target(test-exe
        COMMAND lxbuild -o TEST.EXE --entry 0 --stack 64 ${CMAKE_CURRENT_BINARY_DIR}/test_code.bin
        DEPENDS lxbuild ${CMAKE_CURRENT_BINARY_DIR}/test_code.bin
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Creating test executable TEST.EXE"
)

# Test DLL creation
add_custom_target(test-dll
        COMMAND lxbuild -t dll -o TEST.DLL -n TEST --export TestFunc:0 ${CMAKE_CURRENT_BINARY_DIR}/test_code.bin
        DEPENDS lxbuild ${CMAKE_CURRENT_BINARY_DIR}/test_code.bin
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Creating test DLL TEST.DLL"
)

# Run all tests
add_custom_target(test-lx
        DEPENDS test-exe test-dll
        COMMENT "Running LX builder tests"
)

# ── Installation ─────────────────────────────────────────────────────────────

install(TARGETS lxbuild makeexe makedll
        RUNTIME DESTINATION bin
)

# ── Status Message ───────────────────────────────────────────────────────────

message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "LX Tools Build Configuration")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")
message(STATUS "Targets:")
message(STATUS "  - lxbuild  : Full-featured LX builder")
message(STATUS "  - makeexe  : Simple EXE maker")
message(STATUS "  - makedll  : Simple DLL maker")
message(STATUS "")
message(STATUS "After build, test with: make test-lx")
message(STATUS "═══════════════════════════════════════════════════════════")
message(STATUS "")