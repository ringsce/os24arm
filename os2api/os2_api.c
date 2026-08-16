/* ============================================================================
 * os2api/os2_api.c  —  OS/2 DOS-call API implementation
 *
 * Bridges DOS-call-shaped requests (DosOpen, DosRead, DosFindFirst, ...)
 * onto the kernel's real VFS layer. dos/ command implementations call
 * these, not vfs_* directly - matching how real OS/2 programs go through
 * DOSCALLS.DLL rather than talking to a filesystem driver themselves.
 * ========================================================================== */

#include "os2_api.h"
#include "vfs.h"
#include "string.h"

/* ── Error / attribute translation ──────────────────────────────────────── */

APIRET vfs_to_os2_error(int vfs_error)
{
    switch (vfs_error) {
        case VFS_OK:           return NO_ERROR;
        case VFS_ERR_NOSPACE:  return ERROR_NOT_ENOUGH_MEMORY;
        case VFS_ERR_NOMOUNT:  return ERROR_INVALID_DRIVE;
        case VFS_ERR_BADFD:    return ERROR_INVALID_HANDLE;
        case VFS_ERR_INVAL:    return ERROR_INVALID_PARAMETER;
        case VFS_ERR_NOTFOUND: return ERROR_FILE_NOT_FOUND;
        case VFS_ERR_NOTDIR:   return ERROR_PATH_NOT_FOUND;
        case VFS_ERR_ISDIR:    return ERROR_ACCESS_DENIED;
        case VFS_ERR_ROFS:     return ERROR_WRITE_PROTECT;
        case VFS_ERR_IO:       return ERROR_NOT_SUPPORTED;
        default:                return ERROR_NOT_SUPPORTED;
    }
}

uint32_t os2_to_vfs_attrs(uint16_t os2_attrs)
{
    return (uint32_t)os2_attrs; /* bit values already match VFS_ATTR_* */
}

uint16_t vfs_to_os2_attrs(uint32_t vfs_attrs)
{
    return (uint16_t)vfs_attrs;
}

/* ── Current directory ───────────────────────────────────────────────────── */

static char current_dir[VFS_PATH_MAX] = "/";

void DosResolvePath(const char *in, char *out, uint32_t out_size)
{
    char tmp[VFS_PATH_MAX];
    uint32_t n = 0;

    /* DOS-style backslashes are accepted anywhere and normalized to '/'. */
    const char *src = in;
    if (*src == '/' || *src == '\\') {
        /* Already absolute. */
        while (*src && n < sizeof(tmp) - 1) {
            tmp[n++] = (*src == '\\') ? '/' : *src;
            src++;
        }
        tmp[n] = '\0';
    } else {
        /* Relative to current_dir. */
        uint32_t cdlen = (uint32_t)strlen(current_dir);
        for (uint32_t i = 0; i < cdlen && n < sizeof(tmp) - 1; i++) tmp[n++] = current_dir[i];
        if (n == 0 || tmp[n - 1] != '/') {
            if (n < sizeof(tmp) - 1) tmp[n++] = '/';
        }
        while (*src && n < sizeof(tmp) - 1) {
            tmp[n++] = (*src == '\\') ? '/' : *src;
            src++;
        }
        tmp[n] = '\0';
    }

    uint32_t i = 0;
    while (tmp[i] && i < out_size - 1) { out[i] = tmp[i]; i++; }
    out[i] = '\0';
}

APIRET DosSetCurrentDir(const char *dirname)
{
    char resolved[VFS_PATH_MAX];
    DosResolvePath(dirname, resolved, sizeof(resolved));

    vfs_stat_t st;
    int r = vfs_stat(resolved, &st);
    if (r != VFS_OK) return vfs_to_os2_error(r);
    if (!(st.attrs & VFS_ATTR_DIR)) return ERROR_PATH_NOT_FOUND;

    strncpy(current_dir, resolved, sizeof(current_dir) - 1);
    current_dir[sizeof(current_dir) - 1] = '\0';
    return NO_ERROR;
}

APIRET DosQueryCurrentDir(char *buffer, uint32_t buffer_len)
{
    if (!buffer) return ERROR_INVALID_PARAMETER;
    uint32_t n = (uint32_t)strlen(current_dir);
    if (n >= buffer_len) return ERROR_BUFFER_OVERFLOW;
    strncpy(buffer, current_dir, buffer_len);
    return NO_ERROR;
}

/* ── File APIs ───────────────────────────────────────────────────────────── */

APIRET DosOpen(const char *filename, HFILE *handle, uint32_t *action,
               uint32_t size, uint32_t attributes, uint32_t open_flags,
               uint32_t open_mode)
{
    (void)size; (void)attributes; (void)open_flags;

    char resolved[VFS_PATH_MAX];
    DosResolvePath(filename, resolved, sizeof(resolved));

    int fd = vfs_open(resolved, (int)open_mode);
    if (fd < 0) return vfs_to_os2_error(fd);

    if (handle) *handle = (HFILE)fd;
    if (action) *action = 1; /* opened existing (we don't distinguish create-vs-open here) */
    return NO_ERROR;
}

APIRET DosClose(HFILE handle)
{
    int r = vfs_close((int)handle);
    return vfs_to_os2_error(r);
}

APIRET DosRead(HFILE handle, void *buffer, uint32_t count, uint32_t *bytes_read)
{
    int r = vfs_read((int)handle, buffer, count);
    if (r < 0) {
        if (bytes_read) *bytes_read = 0;
        return vfs_to_os2_error(r);
    }
    if (bytes_read) *bytes_read = (uint32_t)r;
    return NO_ERROR;
}

APIRET DosWrite(HFILE handle, const void *buffer, uint32_t count, uint32_t *bytes_written)
{
    int r = vfs_write((int)handle, buffer, count);
    if (r < 0) {
        if (bytes_written) *bytes_written = 0;
        return vfs_to_os2_error(r);
    }
    if (bytes_written) *bytes_written = (uint32_t)r;
    return NO_ERROR;
}

APIRET DosChgFilePtr(HFILE handle, int32_t offset, uint32_t method, uint32_t *newpos)
{
    int r = vfs_seek((int)handle, offset, (int)method);
    if (r < 0) {
        if (newpos) *newpos = 0;
        return vfs_to_os2_error(r);
    }
    if (newpos) *newpos = (uint32_t)r;
    return NO_ERROR;
}

/* ── Directory search ───────────────────────────────────────────────────── */

/* Minimal DOS-style wildcard match: '*' = any run of chars, '?' = one char. */
static bool wildcard_match(const char *pattern, const char *name)
{
    while (*pattern && *name) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return true;
            while (*name) {
                if (wildcard_match(pattern, name)) return true;
                name++;
            }
            return false;
        }
        if (*pattern == '?' || *pattern == *name) {
            pattern++;
            name++;
            continue;
        }
        return false;
    }
    while (*pattern == '*') pattern++;
    return *pattern == '\0' && *name == '\0';
}

/* Split "/dir/PATTERN" into dir ("/dir") and the trailing name pattern. */
static void split_pattern(const char *path, char *dir_out, uint32_t dir_size, char *pattern_out, uint32_t pattern_size)
{
    int len = (int)strlen(path);
    int slash = -1;
    for (int i = 0; i < len; i++) {
        if (path[i] == '/') slash = i;
    }

    if (slash < 0) {
        strncpy(dir_out, "/", dir_size);
        strncpy(pattern_out, path, pattern_size);
        return;
    }

    uint32_t dlen = (uint32_t)slash;
    if (dlen == 0) dlen = 1; /* root */
    if (dlen >= dir_size) dlen = dir_size - 1;
    memcpy(dir_out, path, dlen);
    dir_out[dlen] = '\0';

    const char *name = path + slash + 1;
    strncpy(pattern_out, name, pattern_size);
}

static void fill_findbuf(FILEFINDBUF *buf, const vfs_stat_t *st, const char *name)
{
    memset(buf, 0, sizeof(*buf));
    buf->file_size = st->size;
    buf->alloc_size = st->size;
    buf->attributes = vfs_to_os2_attrs(st->attrs);

    uint32_t n = (uint32_t)strlen(name);
    if (n > sizeof(buf->name) - 1) n = sizeof(buf->name) - 1;
    memcpy(buf->name, name, n);
    buf->name[n] = '\0';
    buf->name_len = (uint8_t)n;
}

/* Directory search state: HDIR is a VFS dfd plus the remembered pattern. */
typedef struct {
    int dfd;
    char dir[VFS_PATH_MAX];
    char pattern[VFS_NAME_MAX + 1];
} find_state_t;

#define MAX_FIND_HANDLES 16
static find_state_t find_states[MAX_FIND_HANDLES];
static bool find_used[MAX_FIND_HANDLES];

static int alloc_find_handle(void)
{
    for (int i = 0; i < MAX_FIND_HANDLES; i++) {
        if (!find_used[i]) return i;
    }
    return -1;
}

static APIRET find_next_locked(find_state_t *fs, FILEFINDBUF *buffer, uint32_t *count)
{
    vfs_dirent_t ent;
    while (vfs_readdir(fs->dfd, &ent) == VFS_OK) {
        if (!wildcard_match(fs->pattern, ent.name)) continue;

        char full[VFS_PATH_MAX];
        uint32_t dlen = (uint32_t)strlen(fs->dir);
        uint32_t i = 0;
        while (i < dlen && i < sizeof(full) - 1) { full[i] = fs->dir[i]; i++; }
        if (i == 0 || full[i - 1] != '/') { if (i < sizeof(full) - 1) full[i++] = '/'; }
        uint32_t j = 0;
        while (ent.name[j] && i < sizeof(full) - 1) full[i++] = ent.name[j++];
        full[i] = '\0';

        vfs_stat_t st;
        if (vfs_stat(full, &st) != VFS_OK) continue;

        fill_findbuf(buffer, &st, ent.name);
        if (count) *count = 1;
        return NO_ERROR;
    }
    if (count) *count = 0;
    return ERROR_NO_MORE_FILES;
}

APIRET DosFindFirst(const char *pattern, HDIR *handle, uint32_t attributes,
                     FILEFINDBUF *buffer, uint32_t buffer_size, uint32_t *count)
{
    (void)attributes; (void)buffer_size;
    if (!handle || !buffer) return ERROR_INVALID_PARAMETER;

    char resolved[VFS_PATH_MAX];
    DosResolvePath(pattern, resolved, sizeof(resolved));

    int slot = alloc_find_handle();
    if (slot < 0) return ERROR_NOT_ENOUGH_MEMORY;

    find_state_t *fs = &find_states[slot];
    split_pattern(resolved, fs->dir, sizeof(fs->dir), fs->pattern, sizeof(fs->pattern));

    int dfd = vfs_opendir(fs->dir);
    if (dfd < 0) return vfs_to_os2_error(dfd);
    fs->dfd = dfd;
    find_used[slot] = true;

    APIRET r = find_next_locked(fs, buffer, count);
    if (r != NO_ERROR) {
        vfs_closedir(fs->dfd);
        find_used[slot] = false;
        return r;
    }

    *handle = (HDIR)slot;
    return NO_ERROR;
}

APIRET DosFindNext(HDIR handle, FILEFINDBUF *buffer, uint32_t buffer_size, uint32_t *count)
{
    (void)buffer_size;
    if (handle >= MAX_FIND_HANDLES || !find_used[handle] || !buffer) return ERROR_INVALID_HANDLE;
    return find_next_locked(&find_states[handle], buffer, count);
}

APIRET DosFindClose(HDIR handle)
{
    if (handle >= MAX_FIND_HANDLES || !find_used[handle]) return ERROR_INVALID_HANDLE;
    vfs_closedir(find_states[handle].dfd);
    find_used[handle] = false;
    return NO_ERROR;
}

/* ── Metadata / directory tree ──────────────────────────────────────────── */

APIRET DosQueryPathInfo(const char *filename, uint32_t info_level, FILESTATUS *info, uint32_t buf_size)
{
    (void)info_level; (void)buf_size;
    if (!info) return ERROR_INVALID_PARAMETER;

    char resolved[VFS_PATH_MAX];
    DosResolvePath(filename, resolved, sizeof(resolved));

    vfs_stat_t st;
    int r = vfs_stat(resolved, &st);
    if (r != VFS_OK) return vfs_to_os2_error(r);

    memset(info, 0, sizeof(*info));
    info->file_size = st.size;
    info->alloc_size = st.size;
    info->attributes = vfs_to_os2_attrs(st.attrs);
    return NO_ERROR;
}

APIRET DosDelete(const char *filename)
{
    char resolved[VFS_PATH_MAX];
    DosResolvePath(filename, resolved, sizeof(resolved));
    int r = vfs_unlink(resolved);
    return vfs_to_os2_error(r);
}

APIRET DosMove(const char *old_name, const char *new_name)
{
    char old_resolved[VFS_PATH_MAX];
    char new_resolved[VFS_PATH_MAX];
    DosResolvePath(old_name, old_resolved, sizeof(old_resolved));
    DosResolvePath(new_name, new_resolved, sizeof(new_resolved));
    int r = vfs_rename(old_resolved, new_resolved);
    return vfs_to_os2_error(r);
}

APIRET DosCreateDir(const char *dirname)
{
    char resolved[VFS_PATH_MAX];
    DosResolvePath(dirname, resolved, sizeof(resolved));
    int r = vfs_mkdir(resolved);
    return vfs_to_os2_error(r);
}

APIRET DosDeleteDir(const char *dirname)
{
    /* The VFS has no dedicated rmdir op; unlink is the closest available
     * primitive, and every current filesystem driver is read-only anyway. */
    char resolved[VFS_PATH_MAX];
    DosResolvePath(dirname, resolved, sizeof(resolved));
    int r = vfs_unlink(resolved);
    return vfs_to_os2_error(r);
}
