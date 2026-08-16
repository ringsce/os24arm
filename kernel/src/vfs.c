/* ============================================================================
 * kernel/src/vfs.c  —  Virtual File System layer
 *
 * Sits between userspace (CLI, BASIC) and filesystem drivers (FAT32).
 * Manages mount points, file descriptors, and dispatches to fs ops.
 * ========================================================================== */

#include "vfs.h"
#include "kio.h"
#include "uart.h"  /*** @brief uart.h **/

/* ── Mount table ─────────────────────────────────────────────────────────── */

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static int         mount_count = 0;

/* ── File / dir descriptor tables ───────────────────────────────────────── */

static vfs_file_t files[VFS_MAX_FILES];
static vfs_dir_t  dirs [VFS_MAX_DIRS];

/* ══════════════════════════════════════════════════════════════════════════
   INTERNAL HELPERS
   ══════════════════════════════════════════════════════════════════════════ */

/* Find the best-matching mount for a path.
   "C:/FOO" matches mount "C:", "/" matches mount "/".
   Returns NULL if none. */
static vfs_mount_t *find_mount(const char *path)
{
    vfs_mount_t *best = NULL;
    int          best_len = -1;

    for (int i = 0; i < mount_count; i++) {
        const char *mp  = mounts[i].mountpoint;
        int         mpl = kstrlen(mp);

        /* Match the mountpoint prefix */
        if (kstrncmp(path, mp, (size_t)mpl) != 0) continue;

        /* Root mount "/" matches any absolute path: the leading slash IS
         * the separator, so there's no boundary character left to check
         * (unlike "C:", where the next char must still be '/' or ':'). */
        if (!(mpl == 1 && mp[0] == '/')) {
            char next = path[mpl];
            if (next != '\0' && next != '/' && next != ':' && next != '\\')
                continue;
        }

        if (mpl > best_len) { best = &mounts[i]; best_len = mpl; }
    }
    return best;
}

/* Strip the mountpoint prefix from a path, returning the relative path.
   "C:/FOO/BAR" with mount "C:" → "/FOO/BAR" */
static const char *relative_path(vfs_mount_t *mnt, const char *path)
{
    int mpl = kstrlen(mnt->mountpoint);
    path += mpl;
    /* skip a trailing ':' used in DOS-style paths */
    if (*path == ':') path++;
    if (*path == '\0') return "/";
    return path;
}

/* Allocate a file descriptor */
static int alloc_fd(void)
{
    for (int i = 0; i < VFS_MAX_FILES; i++) {
        if (!files[i].used) return i;
    }
    return VFS_ERR_NOSPACE;
}

/* Allocate a directory descriptor (offset into dirs[], returned as dfd = fd + VFS_MAX_FILES) */
static int alloc_dfd(void)
{
    for (int i = 0; i < VFS_MAX_DIRS; i++) {
        if (!dirs[i].used) return i;
    }
    return VFS_ERR_NOSPACE;
}

/* ══════════════════════════════════════════════════════════════════════════
   PUBLIC API
   ══════════════════════════════════════════════════════════════════════════ */

void vfs_init(void)
{
    uart_puts("DEBUG: vfs_init starting\r\n");

    // Arrays (mounts, files, dirs) are already zeroed by boot.S BSS clearing
    // Just initialize the global counter
    mount_count = 0;

    uart_puts("DEBUG: vfs_init complete\r\n");
}
/* ── Mount management ────────────────────────────────────────────────────── */

int vfs_mount(const char *mountpoint, vfs_fs_t *fs, void *fs_priv)
{
    if (mount_count >= VFS_MAX_MOUNTS) return VFS_ERR_NOSPACE;

    vfs_mount_t *mnt = &mounts[mount_count];
    kstrncpy(mnt->mountpoint, mountpoint, VFS_PATH_MAX);
    mnt->fs   = fs;
    mnt->priv = fs_priv;

    int r = fs->mount(mnt);
    if (r != VFS_OK) {
        kmemset(mnt, 0, sizeof(*mnt));
        kprintf("[VFS]  Mount '%s' failed: %d\n", mountpoint, r);
        return r;
    }

    mount_count++;
    kprintf("[VFS]  Mounted '%s' as '%s'\n", fs->name, mountpoint);
    return VFS_OK;
}

int vfs_unmount(const char *mountpoint)
{
    for (int i = 0; i < mount_count; i++) {
        if (kstrcmp(mounts[i].mountpoint, mountpoint) == 0) {
            mounts[i].fs->unmount(&mounts[i]);
            /* Shift table down */
            for (int j = i; j < mount_count - 1; j++)
                mounts[j] = mounts[j + 1];
            mount_count--;
            return VFS_OK;
        }
    }
    return VFS_ERR_NOMOUNT;
}

/* ── File operations ─────────────────────────────────────────────────────── */

int vfs_open(const char *path, int flags)
{
    vfs_mount_t *mnt = find_mount(path);
    if (!mnt) return VFS_ERR_NOMOUNT;

    int fd = alloc_fd();
    if (fd < 0) return fd;

    void *fpriv = NULL;
    int r = mnt->fs->open(mnt, relative_path(mnt, path), flags, &fpriv);
    if (r != VFS_OK) return r;

    files[fd].used  = true;
    files[fd].mnt   = mnt;
    files[fd].fpriv = fpriv;
    files[fd].flags = flags;
    files[fd].pos   = 0;
    return fd;
}

int vfs_close(int fd)
{
    if (fd < 0 || fd >= VFS_MAX_FILES || !files[fd].used) return VFS_ERR_BADFD;
    int r = files[fd].mnt->fs->close(files[fd].mnt, files[fd].fpriv);
    files[fd].used = false;
    return r;
}

int vfs_read(int fd, void *buf, uint32_t len)
{
    if (fd < 0 || fd >= VFS_MAX_FILES || !files[fd].used) return VFS_ERR_BADFD;
    uint32_t actual = 0;
    int r = files[fd].mnt->fs->read(files[fd].mnt, files[fd].fpriv, buf, len, &actual);
    if (r != VFS_OK) return r;
    return (int)actual;
}

int vfs_write(int fd, const void *buf, uint32_t len)
{
    if (fd < 0 || fd >= VFS_MAX_FILES || !files[fd].used) return VFS_ERR_BADFD;
    uint32_t actual = 0;
    int r = files[fd].mnt->fs->write(files[fd].mnt, files[fd].fpriv, buf, len, &actual);
    if (r != VFS_OK) return r;
    return (int)actual;
}

int vfs_seek(int fd, int32_t offset, int whence)
{
    if (fd < 0 || fd >= VFS_MAX_FILES || !files[fd].used) return VFS_ERR_BADFD;
    uint32_t newpos = 0;
    int r = files[fd].mnt->fs->seek(files[fd].mnt, files[fd].fpriv, offset, whence, &newpos);
    if (r != VFS_OK) return r;
    files[fd].pos = newpos;
    return (int)newpos;
}

int vfs_stat(const char *path, vfs_stat_t *st)
{
    vfs_mount_t *mnt = find_mount(path);
    if (!mnt) return VFS_ERR_NOMOUNT;
    return mnt->fs->stat(mnt, relative_path(mnt, path), st);
}

int vfs_unlink(const char *path)
{
    vfs_mount_t *mnt = find_mount(path);
    if (!mnt) return VFS_ERR_NOMOUNT;
    return mnt->fs->unlink(mnt, relative_path(mnt, path));
}

int vfs_rename(const char *old, const char *nw)
{
    vfs_mount_t *mnt = find_mount(old);
    if (!mnt) return VFS_ERR_NOMOUNT;
    /* Cross-mount rename not supported */
    vfs_mount_t *mnt2 = find_mount(nw);
    if (mnt != mnt2) return VFS_ERR_INVAL;
    if (!mnt->fs->rename) return VFS_ERR_ROFS;
    return mnt->fs->rename(mnt, relative_path(mnt, old), relative_path(mnt, nw));
}

int vfs_mkdir(const char *path)
{
    vfs_mount_t *mnt = find_mount(path);
    if (!mnt) return VFS_ERR_NOMOUNT;
    return mnt->fs->mkdir(mnt, relative_path(mnt, path));
}

/* ── Directory operations ─────────────────────────────────────────────────── */

int vfs_opendir(const char *path)
{
    vfs_mount_t *mnt = find_mount(path);
    if (!mnt) return VFS_ERR_NOMOUNT;

    int di = alloc_dfd();
    if (di < 0) return di;

    void *dpriv = NULL;
    int r = mnt->fs->opendir(mnt, relative_path(mnt, path), &dpriv);
    if (r != VFS_OK) return r;

    dirs[di].used  = true;
    dirs[di].mnt   = mnt;
    dirs[di].dpriv = dpriv;

    /* Return as offset to distinguish from file fds */
    return di + VFS_MAX_FILES;
}

int vfs_readdir(int dfd, vfs_dirent_t *ent)
{
    int di = dfd - VFS_MAX_FILES;
    if (di < 0 || di >= VFS_MAX_DIRS || !dirs[di].used) return VFS_ERR_BADFD;
    return dirs[di].mnt->fs->readdir(dirs[di].mnt, dirs[di].dpriv, ent);
}

int vfs_closedir(int dfd)
{
    int di = dfd - VFS_MAX_FILES;
    if (di < 0 || di >= VFS_MAX_DIRS || !dirs[di].used) return VFS_ERR_BADFD;
    int r = dirs[di].mnt->fs->closedir(dirs[di].mnt, dirs[di].dpriv);
    dirs[di].used = false;
    return r;
}