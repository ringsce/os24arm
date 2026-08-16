/* lx/lx_app.ld — linker script for LX applications built by this project
 *
 * Every program wrapped into an OS/2 LX .EXE/.DLL by this project's build
 * (see the top-level CMakeLists.txt's add_lx_executable()/add_lx_library()
 * and the CMD.EXE target) is linked here at LX_LOAD_BASE, which must match
 * the kernel's built-in loader (kernel/include/lx_loader.h). Since link
 * address == load address by construction, the loader (kernel/src/lx_loader.c)
 * can copy the flat image verbatim with no relocation/fixup processing.
 *
 * Sections are placed in strict, contiguous order starting at that fixed
 * address, matching exactly what `objcopy -O binary` extracts into the raw
 * blob wrapped by makeexe/makedll.
 */

ENTRY(main)

SECTIONS
{
    . = 0x48000000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
