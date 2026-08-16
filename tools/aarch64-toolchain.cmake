# ============================================================================
# tools/aarch64-toolchain.cmake
#
# CMake toolchain file for aarch64-elf cross compilation.
#
# Install the cross compiler:
#   macOS (Homebrew):  brew install aarch64-elf-gcc
#   Ubuntu/Debian:     sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
#
# Usage:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=tools/aarch64-toolchain.cmake
# ============================================================================

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# ── Probe for cross-compiler ─────────────────────────────────────────────────
# Try aarch64-elf- first (typical Homebrew), then aarch64-linux-gnu- (Debian)
find_program(CROSS_GCC
    NAMES aarch64-elf-gcc aarch64-linux-gnu-gcc aarch64-none-elf-gcc
    REQUIRED
)
get_filename_component(CROSS_PREFIX "${CROSS_GCC}" NAME)
string(REPLACE "gcc" "" CROSS_PREFIX "${CROSS_PREFIX}")  # strip "gcc" suffix

set(CMAKE_C_COMPILER   "${CROSS_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${CROSS_PREFIX}g++")
set(CMAKE_ASM_COMPILER "${CROSS_PREFIX}gcc")
set(CMAKE_OBJCOPY      "${CROSS_PREFIX}objcopy")
set(CMAKE_OBJDUMP      "${CROSS_PREFIX}objdump")
set(CMAKE_SIZE         "${CROSS_PREFIX}size")
set(CMAKE_LINKER       "${CROSS_PREFIX}ld")

# Do not try to link test executables (we're bare metal)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ── Default flags ─────────────────────────────────────────────────────────────
set(CMAKE_C_FLAGS_INIT   "-ffreestanding -nostdlib -fno-builtin")
set(CMAKE_ASM_FLAGS_INIT "-ffreestanding -nostdlib")
