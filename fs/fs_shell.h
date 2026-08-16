#ifndef FS_SHELL_H
#define FS_SHELL_H

/* ============================================================================
 * kernel/fs/fs_shell.h  —  Filesystem shell command prototypes
 * ========================================================================== */

#include "uart.h"  /* for uart_getc in FORMAT confirmation */
#include "kio.h"

void fs_cmd_format (int argc, char *argv[]);
void fs_cmd_mount  (int argc, char *argv[]);
void fs_cmd_umount (int argc, char *argv[]);
void fs_cmd_ls     (int argc, char *argv[]);
void fs_cmd_cat    (int argc, char *argv[]);
void fs_cmd_write  (int argc, char *argv[]);
void fs_cmd_mkdir_vfs(int argc, char *argv[]);
void fs_cmd_rm     (int argc, char *argv[]);
void fs_cmd_lsblk  (void);
void fs_cmd_mounts (void);
void fs_cmd_fsinfo (void);

#endif /* FS_SHELL_H */
