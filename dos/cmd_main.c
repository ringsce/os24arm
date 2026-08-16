/**
 * @file cmd_main.c
 * @brief Standalone entry point for CMD.EXE
 *
 * This is the top-level command interpreter loop for the DOS command set
 * (dos_commands.c / dos_shell_dispatch), built as its own freestanding
 * program and wrapped as a genuine OS/2 LX CMD.EXE (see the CMD_EXE build
 * target in the top-level CMakeLists.txt).
 *
 * It calls the exact same os2api/VFS stack as the in-kernel shell -
 * everything here runs in the same flat, unprotected address space as the
 * kernel (there is no process isolation or syscall boundary in this
 * project), so this is the real command-interpreter logic, not a demo.
 *
 * The kernel's built-in LX loader (kernel/src/lx_loader.c, RUN CMD.EXE)
 * can now load and jump into this binary directly, but CMD.EXE links its
 * own private copies of vfs.c/blkdev.c/ext4.c/btrfs.c (for self-
 * containment - see the CMD_EXE_SOURCES build target) with their own
 * separate static state, entirely disconnected from whatever the kernel
 * already mounted. So main() below re-runs the same vfs_init/blk_init/
 * ext4_init/btrfs_init sequence kernel_main() does (kernel/src/main.c) -
 * both discover the same real, physical block device, so this is not
 * wasted work, just redone in CMD.EXE's own address space.
 */

#include "dos.h"
#include "uart.h"
#include "vfs.h"
#include "blkdev.h"
#include "ext4.h"
#include "btrfs.h"
#include "string.h"

extern void mem_init(uintptr_t start, size_t size);

/* Private heap for CMD.EXE's own mem_alloc() (used by vfs_mount/ext4/btrfs)
 * - separate from the kernel's own heap, and placed just past the loader's
 * LX_LOAD_BASE/LX_LOAD_MAX_SIZE window (kernel/include/lx_loader.h:
 * 0x48000000 + 16MB = 0x49000000) so it can't collide with CMD.EXE's own
 * loaded code or the preloaded test disk at 0x44000000. */
#define CMD_HEAP_START  0x49000000UL
#define CMD_HEAP_SIZE   (4u * 1024u * 1024u)

#define CMD_LINE_MAX 256

static int cmd_tokenize(char *str, char **tokens, int max_tokens)
{
    int count = 0;
    char *p = str;

    while (*p && count < max_tokens) {
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '\0')
            break;

        tokens[count++] = p;

        while (*p && *p != ' ' && *p != '\t')
            p++;

        if (*p)
            *p++ = '\0';
    }

    return count;
}

int main(void)
{
    char buffer[CMD_LINE_MAX];

    /* Mount the same real block device the kernel already found, in
     * CMD.EXE's own private VFS state (see the comment above main()). */
    mem_init(CMD_HEAP_START, CMD_HEAP_SIZE);
    vfs_init();
    blk_init();
    ext4_init("/CACHE:1024");
    btrfs_init(NULL);

    uart_puts("\r\nOS/2 Warp ARM64 - CMD.EXE\r\n");
    uart_puts("Type a DOS command (DIR, TYPE, COPY, DEL, REN, MD, RD, CD, VOL, ATTRIB, CHKDSK, FORMAT)\r\n");
    uart_puts("Type DOSKEY to enable command history/macros\r\n\r\n");

    for (;;) {
        dos_read_line("[C:\\]> ", buffer, sizeof(buffer), NULL);

        /* A DOSKEY macro using $T comes back as multiple '\n'-separated
         * commands - run each one in turn. */
        char *seg = buffer;
        while (*seg) {
            char *nl = strchr(seg, '\n');
            if (nl) *nl = '\0';
            if (*seg) {
                char *argv[16];
                int argc = cmd_tokenize(seg, argv, 16);
                if (argc > 0 && !dos_shell_dispatch(argc, argv)) {
                    uart_puts("Bad command or file name: ");
                    uart_puts(argv[0]);
                    uart_puts("\r\n");
                }
            }
            if (!nl) break;
            seg = nl + 1;
        }
    }

    return 0;
}
