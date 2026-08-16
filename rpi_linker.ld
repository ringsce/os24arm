/* ============================================================================
 * rpi_linker.ld - Linker Script for Raspberry Pi 3+ ARM64
 * ============================================================================
 * RPi firmware loads kernel at 0x80000
 */

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
    /* RPi loads kernel at 0x80000 */
    . = 0x80000;
    
    /* Code section */
    .text : {
        KEEP(*(.text.boot))
        *(.text*)
    }
    
    /* Read-only data */
    .rodata : {
        *(.rodata*)
    }
    
    /* Initialized data */
    .data : {
        *(.data*)
    }
    
    /* BSS (uninitialized data) */
    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        _bss_end = .;
    }
    
    /* Stack grows down from here */
    . = ALIGN(16);
    _end = .;
    
    /* Discard unnecessary sections */
    /DISCARD/ : {
        *(.comment)
        *(.gnu*)
        *(.note*)
        *(.eh_frame*)
    }
}
