#ifndef VFS_H
#define VFS_H

/* ============================================================================
 * kernel/fs/vfs.h  —  Virtual Filesystem Switch
 *
 * Provides a unified API over ext4 / FAT16 / FAT32 / exFAT.
 * Mount points are simple string prefixes ("/", "/mnt/fat", etc.)
 * ========================================================================== */

#include "types.h"
#include "fs/blkdev.h"

/* ── Forward declarations ────────────────────────────────────────────────── */
typedef struct vfs_mount    vfs_mount_t;
typedef struct vfs_file     vfs_file_t;
typedef struct vfs_dirent   vfs_dirent_t;
typedef struct vfs_stat     vfs_stat_t;
typedef struct vfs_ops      vfs_ops_t;

/* ── Filesystem types ────────────────────────────────────────────────────── */
#define FS_TYPE_EXT4    0
#define FS_TYPE_FAT16   1
#define FS_TYPE_FAT32   2
#define FS_TYPE_EXFAT   3

/* ── Open flags ──────────────────────────────────────────────────────────── */
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0040
#define O_TRUNC    0x0200
#define O_APPEND   0x0400
#define O_DIRECTORY 0x010000

/* ── Seek whence ─────────────────────────────────────────────────────────── */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/* ── File type bits ──────────────────────────────────────────────────────── */
#define S_IFMT    0170000
#define S_IFREG   0100000
#define S_IFDIR   0040000
#define S_ISLNK   0120000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

/* ── Error codes ─────────────────────────────────────────────────────────── */
#define VFS_OK          0
#define VFS_ENOENT    (-2)
#define VFS_ENOTDIR   (-20)
#define VFS_EISDIR    (-21)
#define VFS_ENOSPC    (-28)
#define VFS_EROFS     (-30)
#define VFS_EEXIST    (-17)
#define VFS_EINVAL    (-22)
#define VFS_EIO       (-5)
#define VFS_ENOMEM    (-12)
#define VFS_ENOTSUP   (-95)

/* ── Stat structure ──────────────────────────────────────────────────────── */
struct vfs_stat {
    uint32_t st_mode;       /* file type + permissions                   */
    uint64_t st_size;       /* file size in bytes                        */
    uint32_t st_blksize;    /* preferred I/O block size                  */
    uint64_t st_blocks;     /* 512-byte blocks allocated                 */
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_mtime;      /* seconds since epoch (0 if no RTC)         */
};

/* ── Directory entry ─────────────────────────────────────────────────────── */
#define VFS_NAME_MAX  255

struct vfs_dirent {
    char     name[VFS_NAME_MAX + 1];
    uint32_t type;          /* DT_REG=8, DT_DIR=4 */
    uint64_t size;
};

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

/* ── File handle ─────────────────────────────────────────────────────────── */
#define VFS_FILE_PRIV_SIZE  256   /* enough for any driver's state */

struct vfs_file {
    vfs_mount_t *mount;
    uint32_t     flags;
    uint64_t     offset;    /* current byte position */
    uint64_t     size;
    bool         is_dir;
    uint8_t      priv[VFS_FILE_PRIV_SIZE];  /* driver-private state */
};

/* ── mkfs options ────────────────────────────────────────────────────────── */
typedef struct {
    int      fs_type;               /* FS_TYPE_* */
    uint32_t cluster_size;          /* bytes; 0 = auto-detect             */
    char     label[12];             /* volume label                        */
    uint32_t inode_count;           /* ext4 only; 0 = auto                */
} vfs_mkfs_opts_t;

/* ── Filesystem driver operations ────────────────────────────────────────── */
struct vfs_ops {
    const char *name;               /* "ext4", "fat16", "fat32", "exfat"  */
    int fs_type;

    int     (*mkfs)   (blkdev_t *dev, const vfs_mkfs_opts_t *opts);
    int     (*mount)  (vfs_mount_t *mnt);
    int     (*umount) (vfs_mount_t *mnt);

    int     (*open)   (vfs_mount_t *mnt, const char *path,
                       vfs_file_t *file, uint32_t flags);
    int     (*close)  (vfs_file_t *file);
    ssize_t (*read)   (vfs_file_t *file, void *buf, size_t count);
    ssize_t (*write)  (vfs_file_t *file, const void *buf, size_t count);
    int     (*seek)   (vfs_file_t *file, int64_t off, int whence);
    int     (*stat)   (vfs_mount_t *mnt, const char *path, vfs_stat_t *st);
    int     (*readdir)(vfs_file_t *dir, vfs_dirent_t *ent);
    int     (*mkdir)  (vfs_mount_t *mnt, const char *path);
    int     (*unlink) (vfs_mount_t *mnt, const char *path);
    int     (*sync)   (vfs_mount_t *mnt);
};

/* ── Mount table entry ───────────────────────────────────────────────────── */
#define VFS_MOUNT_PRIV_SIZE  1024

struct vfs_mount {
    char         mountpoint[128];
    blkdev_t    *dev;
    vfs_ops_t   *ops;
    bool         readonly;
    uint8_t      priv[VFS_MOUNT_PRIV_SIZE]; /* driver-private mount state  */
};

/* ── VFS public API ──────────────────────────────────────────────────────── */

void  vfs_init(void);

/* Register a filesystem driver */
void  vfs_register(vfs_ops_t *ops);

/* Format a device */
int   vfs_mkfs(blkdev_t *dev, const vfs_mkfs_opts_t *opts);

/* Mount / unmount */
int   vfs_mount(const char *mountpoint, blkdev_t *dev, int fs_type);
int   vfs_umount(const char *mountpoint);

/* File operations */
int     vfs_open   (const char *path, vfs_file_t *file, uint32_t flags);
int     vfs_close  (vfs_file_t *file);
ssize_t vfs_read   (vfs_file_t *file, void *buf, size_t count);
ssize_t vfs_write  (vfs_file_t *file, const void *buf, size_t count);
int     vfs_seek   (vfs_file_t *file, int64_t off, int whence);

/* Directory operations */
int   vfs_stat     (const char *path, vfs_stat_t *st);
int   vfs_readdir  (vfs_file_t *dir, vfs_dirent_t *ent);
int   vfs_mkdir    (const char *path);
int   vfs_unlink   (const char *path);

/* Diagnostic */
void  vfs_mounts_list(void);
const char *vfs_errstr(int err);

#endif /* VFS_H */
