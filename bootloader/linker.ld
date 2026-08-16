ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text : {
        *(.text.boot)
        *(.text*)
    }

    .rodata : ALIGN(8) { *(.rodata*) }
    .data   : ALIGN(8) { *(.data*)   }

    . = ALIGN(8);
    __bss_start = .;
    .bss : {
        *(.bss*)
        *(COMMON)
    }
    __bss_end = .;

    /DISCARD/ : { *(.comment) *(.note*) }
}
