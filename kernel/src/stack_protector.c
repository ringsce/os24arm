/**
 * @file stack_protector.c
 * @brief Runtime support for -fstack-protector-strong
 *
 * Every function clang decides is worth protecting (has a local array or
 * address-taken local buffer) stores __stack_chk_guard as a canary on the
 * stack at entry and compares it again before returning; a mismatch means
 * something has already overflowed a local buffer and corrupted the
 * stack, so __stack_chk_fail() must never return - continuing would run
 * on untrustworthy state (corrupted saved registers/return address).
 *
 * This is the same class of "freestanding runtime support the compiler
 * assumes exists" as MSVC's __security_cookie/__security_check_cookie
 * (see wiki.osdev.org/Visual_C%2B%2B_Runtime) - clang/GCC's equivalent
 * for -fstack-protector, needed here because CMakeLists.txt used to build
 * this whole project with -fno-stack-protector specifically because
 * these two symbols didn't exist yet.
 */

#include "types.h"

extern void uart_puts(const char *s);

/* Non-zero compile-time default so protected functions are correctly
 * protected even in the small window before stack_protector_reseed()
 * runs (see kernel_main()) - must never be exactly 0, since an overflow
 * that happens to zero-fill past the canary would otherwise look
 * "unchanged" against a zero guard. */
uintptr_t __stack_chk_guard = 0xA5A5A5A5DEADBEEFULL;

/* Called once, early in kernel_main(), to fold in something better than
 * a fixed constant - not a real CSPRNG (no entropy source this early in
 * boot), just the free-running virtual counter, but enough that two
 * boots don't share an identical, guessable canary. */
void stack_protector_reseed(void)
{
    uintptr_t counter;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(counter));
    __stack_chk_guard ^= counter;
    if (__stack_chk_guard == 0) __stack_chk_guard = 0xA5A5A5A5DEADBEEFULL;
}

__attribute__((noreturn))
void __stack_chk_fail(void)
{
    uart_puts("\r\n[PANIC] Stack smashing detected - halting\r\n");
    while (1) {
        __asm__ volatile("wfi");
    }
}
