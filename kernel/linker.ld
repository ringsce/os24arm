ENTRY(_start)

SECTIONS
{
    /* QEMU's aarch64 "virt" machine loads a raw -kernel image (no ARM64
     * Linux Image header) at RAM_BASE + 0x80000: it drops a small stub at
     * RAM_BASE that branches to this offset, mirroring the Linux boot
     * protocol's default TEXT_OFFSET. Every symbol here must be linked at
     * the address the image actually runs at, or absolute references
     * (VBAR_EL1, _bss_start/_end, _stack_top) end up wrong by 0x80000
     * while PC-relative code/data access keeps working - which is exactly
     * what made this so hard to see: only things needing an absolute
     * address (notably the exception vector table) broke. */
    . = 0x40080000;        /* QEMU virt load address */

    .text : {
        *(.text.boot)      /* start.S must be first */
        *(.text*)
    }

    .rodata : ALIGN(8) { *(.rodata*) }

    .data   : ALIGN(8) { *(.data*)   }

    . = ALIGN(16);
    __bss_start = .;
    _bss_start  = .;       /* alias for start.S */
    .bss : {
        *(.bss*)
        *(COMMON)
    }
    . = ALIGN(16);
    __bss_end = .;
    _bss_end  = .;         /* alias for start.S */

    /* 64 KB stack at top of 128 MB RAM */
    _stack_top = 0x40000000 + 128M;

    /DISCARD/ : { *(.comment) *(.note*) }
}
