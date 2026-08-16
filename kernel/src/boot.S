/**
 * @file boot.S
 * @brief ARM64 Boot Code for OS/2 Warp Kernel (Fixed)
 */

.section ".text.boot"

.global _start

_start:
    /* We enter here on core 0, EL1 (kernel mode) */

    /* Read CPU ID, hang all cores except core 0 */
    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbz     x1, core0

    /* Cores 1-3: infinite loop */
hang:
    wfe
    b       hang

core0:
    /* Disable interrupts */
    msr     daifset, #0xF

    /* Set up stack pointer (grows downward) */
    ldr     x1, =_stack_top
    mov     sp, x1

    /* Ensure stack is valid before proceeding */
    mov     x0, #0
    str     x0, [sp, #-16]!   /* Test stack write */
    ldr     x0, [sp], #16     /* Test stack read */

    /* Clear BSS section with safety checks */
    ldr     x1, =_bss_start
    ldr     x2, =_bss_end

    /* Safety: Check if BSS addresses are valid */
    cmp     x1, x2
    b.ge    bss_done          /* Skip if _bss_start >= _bss_end */

clear_bss:
    /* Clear 8 bytes at a time */
    str     xzr, [x1], #8
    cmp     x1, x2
    b.lt    clear_bss

bss_done:
    /* Ensure memory operations complete */
    dsb     sy
    isb

    /* Set up exception vector table */
    ldr     x0, =vector_table
    msr     vbar_el1, x0
    isb

    /* Enable FP/SIMD access at EL1 (CPACR_EL1.FPEN = 0b11). Traps by
     * default at reset; kprintf's variadic prologue spills q0-q7 as part
     * of the AAPCS64 varargs register-save area, which needs this. */
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    /* Enable alignment checking (optional, helps debug) */
    mrs     x0, sctlr_el1
    orr     x0, x0, #(1 << 1)  /* Enable alignment checking */
    msr     sctlr_el1, x0
    isb

    /* Jump to C kernel entry point */
    bl      kernel_main

    /* If kernel_main returns, hang */
    b       hang

/* Exception vector table (required for ARM64) */
.balign 2048
vector_table:
    /* Current EL with SP0 */
    .balign 128
    b       sync_exception_sp0
    .balign 128
    b       irq_exception_sp0
    .balign 128
    b       fiq_exception_sp0
    .balign 128
    b       serror_exception_sp0

    /* Current EL with SPx */
    .balign 128
    b       sync_exception_spx
    .balign 128
    b       irq_exception_spx
    .balign 128
    b       fiq_exception_spx
    .balign 128
    b       serror_exception_spx

    /* Lower EL using AArch64 */
    .balign 128
    b       hang
    .balign 128
    b       hang
    .balign 128
    b       hang
    .balign 128
    b       hang

    /* Lower EL using AArch32 */
    .balign 128
    b       hang
    .balign 128
    b       hang
    .balign 128
    b       hang
    .balign 128
    b       hang

/* Exception handlers: report to UART (esr/elr/far -> x0/x1/x2) then hang.
 * report_sync_exception() uses only uart_putc/uart_puts, so it stays usable
 * even if the fault happened inside variadic-argument handling. */
sync_exception_sp0:
    stp     x29, x30, [sp, #-16]!
    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mrs     x2, far_el1
    bl      report_sync_exception
    b       hang

irq_exception_sp0:
    b       hang

fiq_exception_sp0:
    b       hang

serror_exception_sp0:
    b       hang

sync_exception_spx:
    stp     x29, x30, [sp, #-16]!
    mrs     x0, esr_el1
    mrs     x1, elr_el1
    mrs     x2, far_el1
    bl      report_sync_exception
    b       hang

irq_exception_spx:
    b       hang

fiq_exception_spx:
    b       hang

serror_exception_spx:
    b       hang