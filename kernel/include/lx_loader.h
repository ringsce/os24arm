/**
 * @file lx_loader.h
 * @brief Built-in loader for OS/2 LX (.EXE) executables
 *
 * Loads the LX programs produced by this project's own toolchain (see the
 * top-level CMakeLists.txt's add_lx_executable()/add_lx_library() and the
 * CMD.EXE build target, all of which link via lx/lx_app.ld) and runs them
 * in-place, in the kernel's own address space - there is no process
 * isolation or syscall boundary in this project, so "loading" a program
 * just means copying its pages to the fixed address they were linked at
 * (LX_LOAD_BASE) and calling into it like any other function.
 *
 * This only supports the flat, single-object, no-fixup LX files this
 * project's own build produces (see lx/lxbuilder.c: one code/data object
 * holding the whole program image, no imports, no relocations). It is NOT
 * a general OS/2 LX loader - no fixup/import resolution, no per-object
 * placement, no demand paging.
 */

#ifndef LX_LOADER_H
#define LX_LOADER_H

#include "types.h"

/* Fixed load address for LX programs built by this project's toolchain.
 * Must match the base address lx/lx_app.ld links them at - link address
 * equals load address by construction, so pages are copied verbatim with
 * no relocation. Chosen well clear of the kernel image/heap and the
 * preloaded test ramdisk (blkdev.c's PRELOADED_DISK_ADDR = 0x44000000,
 * 24MB). */
#define LX_LOAD_BASE      0x48000000UL
#define LX_LOAD_MAX_SIZE  (16u * 1024u * 1024u)

#define LX_ERR_IO        -1  /* couldn't read/stat the file */
#define LX_ERR_BADMAGIC  -2  /* not an LX file (or corrupt) */
#define LX_ERR_BADCPU    -3  /* not built for ARM64 */
#define LX_ERR_TOOBIG    -4  /* image too large for the load window */
#define LX_ERR_NOMEM     -5  /* mem_alloc failed */

/* Load the LX .EXE at `path` from the VFS and run it. Blocks the caller
 * until the loaded program's entry point returns (it may never return -
 * e.g. CMD.EXE's shell loop takes over the UART forever, exactly like
 * calling into another kernel shell). Returns the entry point's return
 * value, or a negative LX_ERR_* on load failure. */
int lx_exec_file(const char *path);

/* Load and run an LX image already sitting in memory (e.g. embedded in
 * the kernel image via .incbin). Same semantics as lx_exec_file(). */
int lx_exec_image(const void *image, uint32_t image_size);

#endif /* LX_LOADER_H */
