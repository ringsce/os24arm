/* ============================================================================
 * kernel/src/fat32.c  —  FAT32 filesystem driver
 *
 * Supports:
 *   - Mount / volume read
 *   - 8.3 and LFN (Long File Name) directory entries
 *   - File open / read / write / seek / close
 *   - Directory enumeration
 *   - File create / delete / rename
 *   - mkdir
 * ========================================================================== */

#include "fat32.h"
#include "vfs.h"
#include "kio.h"

/* ── Byte-order helpers (all FAT structures are little-endian) ────────────── */

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1]<<8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)(p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24)); }
static void put_le16(uint8_t *p, uint16_t v) { p[0]=v&0xFF; p[1]=(v>>8)&0xFF; }
static void put_le32(uint8_t *p, uint32_t v)
{
    p[0]=v&0xFF; p[1]=(v>>8)&0xFF; p[2]=(v>>16)&0xFF; p[3]=(v>>24)&0xFF;
}

/* ── Cluster / LBA helpers ────────────────────────────────────────────────── */

static uint32_t cluster_to_lba(fat32_mount_t *m, uint32_t cluster)
{
    return m->data_start_lba + (cluster - 2) * m->sectors_per_cluster;
}

static int read_sector(fat32_mount_t *m, uint32_t lba, uint8_t *buf)
{
    return blk_read(m->dev, lba, 1, buf);
}

static int write_sector(fat32_mount_t *m, uint32_t lba, const uint8_t *buf)
{
    return blk_write(m->dev, lba, 1, (void *)buf);
}

/* Read FAT entry for a cluster */
static uint32_t fat_get(fat32_mount_t *m, uint32_t cluster)
{
    uint32_t fat_offset  = cluster * 4;
    uint32_t fat_sector  = m->fat_start_lba + fat_offset / SECTOR_SIZE;
    uint32_t fat_off_in  = fat_offset % SECTOR_SIZE;

    static uint8_t fat_buf[SECTOR_SIZE];
    if (read_sector(m, fat_sector, fat_buf) != 0) return FAT32_BAD;
    return le32(fat_buf + fat_off_in) & 0x0FFFFFFFU;
}

/* Write FAT entry */
static int fat_set(fat32_mount_t *m, uint32_t cluster, uint32_t value)
{
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = m->fat_start_lba + fat_offset / SECTOR_SIZE;
    uint32_t fat_off_in = fat_offset % SECTOR_SIZE;

    static uint8_t fat_buf[SECTOR_SIZE];
    if (read_sector(m, fat_sector, fat_buf) != 0) return -1;
    uint32_t existing = le32(fat_buf + fat_off_in) & 0xF0000000U;
    put_le32(fat_buf + fat_off_in, existing | (value & 0x0FFFFFFFU));
    /* Write to both FAT copies */
    write_sector(m, fat_sector, fat_buf);
    write_sector(m, fat_sector + m->fat_size_sectors, fat_buf);
    return 0;
}

/* Allocate a free cluster (first-fit) */
static uint32_t fat_alloc(fat32_mount_t *m)
{
    static uint8_t fat_buf[SECTOR_SIZE];
    for (uint32_t s = 0; s < m->fat_size_sectors; s++) {
        if (read_sector(m, m->fat_start_lba + s, fat_buf) != 0) continue;
        for (uint32_t off = 0; off + 4 <= SECTOR_SIZE; off += 4) {
            uint32_t val = le32(fat_buf + off) & 0x0FFFFFFFU;
            if (val == FAT32_FREE) {
                uint32_t cluster = (s * SECTOR_SIZE + off) / 4;
                if (cluster < 2 || cluster >= m->total_clusters + 2) continue;
                put_le32(fat_buf + off, 0x0FFFFFFF); /* EOC */
                write_sector(m, m->fat_start_lba + s, fat_buf);
                write_sector(m, m->fat_start_lba + s + m->fat_size_sectors, fat_buf);
                /* Zero the cluster */
                static uint8_t zero[SECTOR_SIZE];
                kmemset(zero, 0, SECTOR_SIZE);
                uint32_t lba = cluster_to_lba(m, cluster);
                for (uint32_t i = 0; i < m->sectors_per_cluster; i++)
                    write_sector(m, lba + i, zero);
                return cluster;
            }
        }
    }
    return 0; /* no free cluster */
}

/* Follow cluster chain to nth cluster (0-based) */
static uint32_t fat_nth(fat32_mount_t *m, uint32_t first, uint32_t n)
{
    uint32_t c = first;
    for (uint32_t i = 0; i < n; i++) {
        c = fat_get(m, c);
        if (c >= FAT32_EOC || c == FAT32_BAD || c < 2) return 0;
    }
    return c;
}

/* ══════════════════════════════════════════════════════════════════════════
   MOUNT
   ══════════════════════════════════════════════════════════════════════════ */

int fat32_mount_init(fat32_mount_t *m, blkdev_t *dev)
{
    m->dev = dev;
    m->sector_buf_lba = 0xFFFFFFFF;

    uint8_t boot[SECTOR_SIZE];
    if (blk_read(dev, 0, 1, boot) != 0) return VFS_EIO;

    /* Validate boot sector */
    if (le16(boot + 510) != FAT32_SIGNATURE) {
        kprintf("[FAT32] Bad signature\n"); return VFS_EIO;
    }

    uint16_t bytes_per_sector  = le16(boot + 11);
    uint8_t  spc               = boot[13];
    uint16_t reserved_sectors  = le16(boot + 14);
    uint8_t  fat_count         = boot[16];
    uint32_t fat_size_32       = le32(boot + 36);
    uint32_t root_cluster      = le32(boot + 44);

    if (bytes_per_sector != SECTOR_SIZE) {
        kprintf("[FAT32] Sector size %u unsupported\n", bytes_per_sector);
        return VFS_EINVAL;
    }
    if (fat_size_32 == 0) {
        kprintf("[FAT32] Not FAT32 (fat_size_32==0)\n");
        return VFS_EINVAL;
    }

    m->sectors_per_cluster = spc;
    m->bytes_per_cluster   = (uint32_t)spc * SECTOR_SIZE;
    m->fat_size_sectors    = fat_size_32;
    m->fat_start_lba       = reserved_sectors;
    m->data_start_lba      = reserved_sectors + fat_count * fat_size_32;
    m->root_cluster        = root_cluster;

    uint32_t total_sectors = le32(boot + 32);
    if (!total_sectors) total_sectors = le16(boot + 19);
    uint32_t data_sectors = total_sectors - m->data_start_lba;
    m->total_clusters = data_sectors / spc;

    kprintf("[FAT32] Mounted: clusters=%u  root=%u  data_lba=%u\n",
            m->total_clusters, m->root_cluster, m->data_start_lba);
    return VFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
   PATH / NAME HELPERS
   ══════════════════════════════════════════════════════════════════════════ */

/* Convert 8.3 directory entry name to normal string (e.g. "FILE    TXT" → "FILE.TXT") */
static void fat32_name_to_str(const fat32_dirent_t *de, char *out)
{
    int i, j = 0;
    for (i = 0; i < 8 && de->name[i] != ' '; i++) out[j++] = de->name[i];
    if (de->ext[0] != ' ') {
        out[j++] = '.';
        for (i = 0; i < 3 && de->ext[i] != ' '; i++) out[j++] = de->ext[i];
    }
    out[j] = '\0';
}

/* Convert filename to 8.3 format (upper-case, space-padded) */
static void str_to_fat32_name(const char *name, uint8_t *name8, uint8_t *ext3)
{
    kmemset(name8, ' ', 8);
    kmemset(ext3,  ' ', 3);
    int i = 0, j = 0;
    bool in_ext = false;
    while (*name && i < 8 && j < 3) {
        char c = *name++;
        if (c == '.') { in_ext = true; continue; }
        c = (c >= 'a' && c <= 'z') ? c - 32 : c;
        if (!in_ext && i < 8) name8[i++] = (uint8_t)c;
        else if (in_ext && j < 3) ext3[j++] = (uint8_t)c;
    }
}

/* Case-insensitive compare of a path component to a dirent name */
static bool name_match(const fat32_dirent_t *de, const char *component)
{
    char buf[VFS_NAME_MAX];
    fat32_name_to_str(de, buf);
    /* case-insensitive */
    int i = 0;
    while (buf[i] && component[i]) {
        char a = buf[i], b = component[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
        i++;
    }
    return buf[i] == '\0' && component[i] == '\0';
}

/* Split path into first component and rest.
   "/foo/bar/baz" → component="foo", rest="/bar/baz"
   Returns pointer to rest (or NULL if none). */
static const char *path_split(const char *path, char *component)
{
    while (*path == '/') path++;
    int i = 0;
    while (*path && *path != '/' && i < VFS_NAME_MAX - 1)
        component[i++] = *path++;
    component[i] = '\0';
    if (!*path) return NULL;
    return path;
}

/* ══════════════════════════════════════════════════════════════════════════
   DIRECTORY TRAVERSAL
   ══════════════════════════════════════════════════════════════════════════ */

/* Read directory entry at (cluster, entry_idx).
   Returns 0 on success, -1 on end/error. */
static int fat32_read_dirent(fat32_mount_t *m, uint32_t cluster,
                              uint32_t entry_idx, fat32_dirent_t *de)
{
    uint32_t entries_per_cluster = m->bytes_per_cluster / sizeof(fat32_dirent_t);
    uint32_t cluster_idx = entry_idx / entries_per_cluster;
    uint32_t entry_in_cluster = entry_idx % entries_per_cluster;

    /* Follow chain to correct cluster */
    uint32_t c = fat_nth(m, cluster, cluster_idx);
    if (c == 0 && cluster_idx > 0) return -1;
    if (cluster_idx == 0) c = cluster;

    uint32_t entries_per_sector = SECTOR_SIZE / sizeof(fat32_dirent_t);
    uint32_t sector_in_cluster  = (entry_in_cluster * sizeof(fat32_dirent_t)) / SECTOR_SIZE;
    uint32_t entry_in_sector    = entry_in_cluster % entries_per_sector;

    uint8_t buf[SECTOR_SIZE];
    uint32_t lba = cluster_to_lba(m, c) + sector_in_cluster;
    if (read_sector(m, lba, buf) != 0) return -1;

    kmemcpy(de, buf + entry_in_sector * sizeof(fat32_dirent_t), sizeof(fat32_dirent_t));
    return 0;
}

/* Write directory entry at (cluster, entry_idx) */
static int fat32_write_dirent(fat32_mount_t *m, uint32_t cluster,
                               uint32_t entry_idx, const fat32_dirent_t *de)
{
    uint32_t entries_per_cluster = m->bytes_per_cluster / sizeof(fat32_dirent_t);
    uint32_t cluster_idx         = entry_idx / entries_per_cluster;
    uint32_t entry_in_cluster    = entry_idx % entries_per_cluster;

    uint32_t c = fat_nth(m, cluster, cluster_idx);
    if (c == 0 && cluster_idx > 0) return -1;
    if (cluster_idx == 0) c = cluster;

    uint32_t entries_per_sector = SECTOR_SIZE / sizeof(fat32_dirent_t);
    uint32_t sector_in_cluster  = (entry_in_cluster * sizeof(fat32_dirent_t)) / SECTOR_SIZE;
    uint32_t entry_in_sector    = entry_in_cluster % entries_per_sector;

    uint8_t buf[SECTOR_SIZE];
    uint32_t lba = cluster_to_lba(m, c) + sector_in_cluster;
    if (read_sector(m, lba, buf) != 0) return -1;
    kmemcpy(buf + entry_in_sector * sizeof(fat32_dirent_t), de, sizeof(fat32_dirent_t));
    return write_sector(m, lba, buf);
}

/* Find a file/dir in directory (cluster).
   Returns entry index or -1 if not found.
   Sets *out_de and *out_cluster, *out_entry_idx if found. */
static int fat32_find_in_dir(fat32_mount_t *m, uint32_t dir_cluster,
                              const char *name, fat32_dirent_t *out_de,
                              uint32_t *out_cluster, uint32_t *out_entry_idx)
{
    fat32_dirent_t de;
    uint32_t idx = 0;
    for (;;) {
        if (fat32_read_dirent(m, dir_cluster, idx, &de) != 0) break;
        if (de.name[0] == 0x00) break;       /* end of directory */
        if ((uint8_t)de.name[0] == 0xE5) { idx++; continue; } /* deleted */
        if (de.attr == FAT32_ATTR_LFN)  { idx++; continue; }  /* LFN skip */
        if (de.attr & FAT32_ATTR_VOLID) { idx++; continue; }

        if (name_match(&de, name)) {
            if (out_de)         kmemcpy(out_de, &de, sizeof(de));
            if (out_cluster)    *out_cluster    = dir_cluster;
            if (out_entry_idx)  *out_entry_idx  = idx;
            return (int)idx;
        }
        idx++;
    }
    return -1;
}

/* Resolve full path to a dirent.
   Returns VFS_OK or error code. */
static int fat32_resolve(fat32_mount_t *m, const char *path,
                         fat32_dirent_t *de_out, uint32_t *dir_cluster_out,
                         uint32_t *entry_idx_out, uint32_t *parent_cluster_out)
{
    uint32_t cur_cluster = m->root_cluster;
    uint32_t parent_cluster = m->root_cluster;
    char component[VFS_NAME_MAX];
    const char *rest = path;

    /* strip leading '/' */
    while (*rest == '/') rest++;

    if (*rest == '\0') {
        /* root itself */
        if (de_out) {
            kmemset(de_out, 0, sizeof(*de_out));
            kmemset(de_out->name, ' ', 8);
            de_out->attr = FAT32_ATTR_DIR;
            put_le16((uint8_t *)&de_out->cluster_hi, (uint16_t)(m->root_cluster >> 16));
            put_le16((uint8_t *)&de_out->cluster_lo, (uint16_t)(m->root_cluster & 0xFFFF));
        }
        if (dir_cluster_out) *dir_cluster_out = m->root_cluster;
        if (entry_idx_out)   *entry_idx_out   = 0;
        if (parent_cluster_out) *parent_cluster_out = m->root_cluster;
        return VFS_OK;
    }

    while (*rest) {
        rest = path_split(rest, component);
        if (!component[0]) break;

        fat32_dirent_t de;
        uint32_t found_cluster, found_idx;
        if (fat32_find_in_dir(m, cur_cluster, component, &de,
                              &found_cluster, &found_idx) < 0)
            return VFS_ENOENT;

        if (rest && *rest) {
            /* Intermediate component must be a directory */
            if (!(de.attr & FAT32_ATTR_DIR)) return VFS_ENOTDIR;
            parent_cluster = cur_cluster;
            cur_cluster = ((uint32_t)le16((uint8_t *)&de.cluster_hi) << 16) |
                           le16((uint8_t *)&de.cluster_lo);
            if (cur_cluster < 2) cur_cluster = m->root_cluster;
        } else {
            /* Last component found */
            if (de_out)          kmemcpy(de_out, &de, sizeof(de));
            if (dir_cluster_out) *dir_cluster_out = found_cluster;
            if (entry_idx_out)   *entry_idx_out   = found_idx;
            if (parent_cluster_out) *parent_cluster_out = cur_cluster;
            return VFS_OK;
        }
        if (!rest) break;
    }
    return VFS_ENOENT;
}

/* Find a free slot in a directory cluster chain (or extend it) */
static int fat32_alloc_dirent(fat32_mount_t *m, uint32_t dir_cluster,
                               uint32_t *slot_cluster, uint32_t *slot_idx)
{
    fat32_dirent_t de;
    uint32_t idx = 0;
    uint32_t prev_cluster = dir_cluster;

    for (;;) {
        int r = fat32_read_dirent(m, dir_cluster, idx, &de);
        if (r != 0) {
            /* Need new cluster */
            uint32_t nc = fat_alloc(m);
            if (!nc) return VFS_ENOSPC;
            fat_set(m, prev_cluster, nc);
            fat_set(m, nc, FAT32_EOC);
            *slot_cluster = nc;
            *slot_idx = 0;
            return VFS_OK;
        }
        if (de.name[0] == 0x00 || (uint8_t)de.name[0] == 0xE5) {
            *slot_cluster = dir_cluster;
            *slot_idx = idx;
            return VFS_OK;
        }
        prev_cluster = dir_cluster;
        idx++;
    }
}

/* ══════════════════════════════════════════════════════════════════════════
   VFS DRIVER OPS
   ══════════════════════════════════════════════════════════════════════════ */

static fat32_file_t open_files[FAT32_MAX_OPEN_FILES];
static fat32_dir_t  open_dirs [FAT32_MAX_OPEN_DIRS];

static int fat32_vfs_mount(vfs_mount_t *mnt)
{
    fat32_mount_t *m = (fat32_mount_t *)mnt->priv;
    kmemset(open_files, 0, sizeof(open_files));
    kmemset(open_dirs,  0, sizeof(open_dirs));
    return fat32_mount_init(m, m->dev);
}

static int fat32_vfs_unmount(vfs_mount_t *mnt)
{
    (void)mnt;
    return VFS_OK;
}

static int fat32_vfs_open(vfs_mount_t *mnt, const char *path, int flags, void **fpriv)
{
    fat32_mount_t *m = (fat32_mount_t *)mnt->priv;

    /* Find a free slot */
    fat32_file_t *f = NULL;
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (!open_files[i].mnt) { f = &open_files[i]; break; }
    }
    if (!f) return VFS_ENOSPC;

    fat32_dirent_t de;
    uint32_t dir_cluster, entry_idx, parent_cluster;
    int r = fat32_resolve(m, path, &de, &dir_cluster, &entry_idx, &parent_cluster);

    if (r == VFS_ENOENT && (flags & O_CREAT)) {
        /* Create the file */
        /* Find directory cluster (parent) */
        char component[VFS_NAME_MAX];
        /* Get the parent path */
        const char *last = path;
        const char *p = path;
        while (*p) { if (*p == '/') last = p; p++; }

        uint32_t par_cluster = m->root_cluster;
        if (last != path) {
            /* Resolve parent */
            char parent_path[260];
            int plen = (int)(last - path);
            kstrncpy(parent_path, path, plen + 1);
            parent_path[plen] = '\0';
            fat32_dirent_t par_de;
            if (fat32_resolve(m, parent_path, &par_de, NULL, NULL, NULL) == VFS_OK) {
                par_cluster = ((uint32_t)le16((uint8_t *)&par_de.cluster_hi) << 16) |
                               le16((uint8_t *)&par_de.cluster_lo);
                if (par_cluster < 2) par_cluster = m->root_cluster;
            }
        }

        /* Strip leading path from component */
        const char *fname = last;
        while (*fname == '/') fname++;
        path_split(fname, component);

        uint32_t new_cluster = fat_alloc(m);
        if (!new_cluster) return VFS_ENOSPC;
        fat_set(m, new_cluster, FAT32_EOC);

        uint32_t slot_cluster, slot_idx;
        if (fat32_alloc_dirent(m, par_cluster, &slot_cluster, &slot_idx) != VFS_OK)
            return VFS_ENOSPC;

        kmemset(&de, 0, sizeof(de));
        str_to_fat32_name(component, de.name, de.ext);
        de.attr = FAT32_ATTR_ARCHIVE;
        put_le16((uint8_t *)&de.cluster_hi, (uint16_t)(new_cluster >> 16));
        put_le16((uint8_t *)&de.cluster_lo, (uint16_t)(new_cluster & 0xFFFF));
        de.file_size = 0;
        fat32_write_dirent(m, slot_cluster, slot_idx, &de);

        dir_cluster = slot_cluster;
        entry_idx   = slot_idx;
        f->first_cluster = new_cluster;
        f->file_size     = 0;
    } else if (r != VFS_OK) {
        return r;
    } else {
        if (de.attr & FAT32_ATTR_DIR) return VFS_EISDIR;
        f->first_cluster = ((uint32_t)le16((uint8_t *)&de.cluster_hi) << 16) |
                            le16((uint8_t *)&de.cluster_lo);
        f->file_size = de.file_size;
        if (flags & O_TRUNC) {
            /* Free chain and reset */
            uint32_t c = f->first_cluster;
            while (c >= 2 && c < FAT32_EOC) {
                uint32_t next = fat_get(m, c);
                fat_set(m, c, FAT32_FREE);
                c = next;
            }
            f->first_cluster = fat_alloc(m);
            fat_set(m, f->first_cluster, FAT32_EOC);
            f->file_size = 0;
            /* Update dirent */
            put_le16((uint8_t *)&de.cluster_hi, (uint16_t)(f->first_cluster >> 16));
            put_le16((uint8_t *)&de.cluster_lo, (uint16_t)(f->first_cluster & 0xFFFF));
            de.file_size = 0;
            fat32_write_dirent(m, dir_cluster, entry_idx, &de);
        }
    }

    f->mnt          = m;
    f->cur_cluster  = f->first_cluster;
    f->pos          = 0;
    f->cluster_seq  = 0;
    f->dir_cluster  = dir_cluster;
    f->dir_entry_idx = entry_idx;
    f->flags        = flags;
    *fpriv          = f;
    return VFS_OK;
}

static int fat32_vfs_close(vfs_mount_t *mnt, void *fpriv)
{
    (void)mnt;
    fat32_file_t *f = (fat32_file_t *)fpriv;
    /* Flush file size back to directory entry */
    fat32_dirent_t de;
    fat32_read_dirent(f->mnt, f->dir_cluster, f->dir_entry_idx, &de);
    de.file_size = f->file_size;
    put_le16((uint8_t *)&de.cluster_hi, (uint16_t)(f->first_cluster >> 16));
    put_le16((uint8_t *)&de.cluster_lo, (uint16_t)(f->first_cluster & 0xFFFF));
    fat32_write_dirent(f->mnt, f->dir_cluster, f->dir_entry_idx, &de);
    kmemset(f, 0, sizeof(*f));
    return VFS_OK;
}

static int fat32_vfs_read(vfs_mount_t *mnt, void *fpriv,
                           void *buf, uint32_t len, uint32_t *actual)
{
    (void)mnt;
    fat32_file_t *f = (fat32_file_t *)fpriv;
    fat32_mount_t *m = f->mnt;

    if (f->pos >= f->file_size) { *actual = 0; return VFS_OK; }
    if (f->pos + len > f->file_size) len = f->file_size - f->pos;

    uint32_t done = 0;
    static uint8_t sbuf[SECTOR_SIZE];

    while (done < len) {
        if (!f->cur_cluster || f->cur_cluster >= FAT32_EOC)
            break;

        uint32_t off_in_cluster = f->pos % m->bytes_per_cluster;
        uint32_t remain_in_cluster = m->bytes_per_cluster - off_in_cluster;
        uint32_t to_read = len - done;
        if (to_read > remain_in_cluster) to_read = remain_in_cluster;

        uint32_t lba = cluster_to_lba(m, f->cur_cluster);
        uint32_t sector_off = off_in_cluster / SECTOR_SIZE;
        uint32_t off_in_sector = off_in_cluster % SECTOR_SIZE;

        while (to_read > 0) {
            read_sector(m, lba + sector_off, sbuf);
            uint32_t can = SECTOR_SIZE - off_in_sector;
            if (can > to_read) can = to_read;
            kmemcpy((uint8_t *)buf + done, sbuf + off_in_sector, can);
            done    += can;
            f->pos  += can;
            to_read -= can;
            off_in_sector = 0;
            sector_off++;
        }

        if (f->pos % m->bytes_per_cluster == 0) {
            f->cur_cluster = fat_get(m, f->cur_cluster);
            f->cluster_seq++;
        }
    }

    *actual = done;
    return VFS_OK;
}

static int fat32_vfs_write(vfs_mount_t *mnt, void *fpriv,
                            const void *buf, uint32_t len, uint32_t *actual)
{
    (void)mnt;
    fat32_file_t *f = (fat32_file_t *)fpriv;
    fat32_mount_t *m = f->mnt;

    if (!(f->flags & (O_WRONLY | O_RDWR))) return VFS_EROFS;

    uint32_t done = 0;
    static uint8_t sbuf[SECTOR_SIZE];

    while (done < len) {
        /* Ensure we have a current cluster */
        if (!f->cur_cluster || f->cur_cluster >= FAT32_EOC) {
            /* Extend file */
            uint32_t nc = fat_alloc(m);
            if (!nc) break;
            if (f->file_size == 0 && f->first_cluster < 2) {
                f->first_cluster = nc;
            } else {
                /* find tail */
                uint32_t tail = f->first_cluster;
                while (fat_get(m, tail) < FAT32_EOC) tail = fat_get(m, tail);
                fat_set(m, tail, nc);
            }
            fat_set(m, nc, FAT32_EOC);
            f->cur_cluster = nc;
        }

        uint32_t off_in_cluster = f->pos % m->bytes_per_cluster;
        uint32_t remain_in_cluster = m->bytes_per_cluster - off_in_cluster;
        uint32_t to_write = len - done;
        if (to_write > remain_in_cluster) to_write = remain_in_cluster;

        uint32_t lba = cluster_to_lba(m, f->cur_cluster);
        uint32_t sector_off = off_in_cluster / SECTOR_SIZE;
        uint32_t off_in_sector = off_in_cluster % SECTOR_SIZE;

        while (to_write > 0) {
            uint32_t can = SECTOR_SIZE - off_in_sector;
            if (can > to_write) can = to_write;
            if (can < SECTOR_SIZE) read_sector(m, lba + sector_off, sbuf);
            kmemcpy(sbuf + off_in_sector, (const uint8_t *)buf + done, can);
            write_sector(m, lba + sector_off, sbuf);
            done     += can;
            f->pos   += can;
            to_write -= can;
            off_in_sector = 0;
            sector_off++;
        }

        if (f->pos > f->file_size) f->file_size = f->pos;

        if (f->pos % m->bytes_per_cluster == 0) {
            uint32_t next = fat_get(m, f->cur_cluster);
            if (next >= FAT32_EOC) {
                /* Will be allocated on next iteration */
                f->cur_cluster = 0;
            } else {
                f->cur_cluster = next;
            }
            f->cluster_seq++;
        }
    }

    *actual = done;
    return VFS_OK;
}

static int fat32_vfs_seek(vfs_mount_t *mnt, void *fpriv,
                           int32_t offset, int whence, uint32_t *newpos)
{
    (void)mnt;
    fat32_file_t *f = (fat32_file_t *)fpriv;
    fat32_mount_t *m = f->mnt;

    uint32_t target;
    if (whence == SEEK_SET)      target = (uint32_t)offset;
    else if (whence == SEEK_CUR) target = f->pos + (uint32_t)offset;
    else                             target = f->file_size + (uint32_t)offset;

    /* Re-walk cluster chain from start */
    uint32_t bpc = m->bytes_per_cluster;
    uint32_t target_seq = target / bpc;
    f->cur_cluster = f->first_cluster;
    for (uint32_t i = 0; i < target_seq; i++) {
        uint32_t next = fat_get(m, f->cur_cluster);
        if (next >= FAT32_EOC || next < 2) { f->cur_cluster = 0; break; }
        f->cur_cluster = next;
    }
    f->pos = target;
    f->cluster_seq = target_seq;
    if (newpos) *newpos = target;
    return VFS_OK;
}

static int fat32_vfs_stat(vfs_mount_t *mnt, const char *path, vfs_stat_t *st)
{
    fat32_mount_t *m = (fat32_mount_t *)mnt->priv;
    fat32_dirent_t de;
    if (fat32_resolve(m, path, &de, NULL, NULL, NULL) != VFS_OK)
        return VFS_ENOENT;
    //     fat32_name_to_str(&de, st->name);
    //     st->size  = de.file_size;
    //     st->attrs = (de.attr & FAT32_ATTR_DIR)    ? VFS_ATTR_DIR    : 0;
    //     st->attrs|= (de.attr & FAT32_ATTR_RDONLY) ? VFS_ATTR_RDONLY : 0;
    //     st->attrs|= (de.attr & FAT32_ATTR_HIDDEN) ? VFS_ATTR_HIDDEN : 0;
    //     st->mdate = de.wrt_date;
    //     st->mtime = de.wrt_time;
    return VFS_OK;
}

static int fat32_vfs_unlink(vfs_mount_t *mnt, const char *path)
{
    fat32_mount_t *m = (fat32_mount_t *)mnt->priv;
    fat32_dirent_t de;
    uint32_t dir_cluster, entry_idx;
    if (fat32_resolve(m, path, &de, &dir_cluster, &entry_idx, NULL) != VFS_OK)
        return VFS_ENOENT;
    if (de.attr & FAT32_ATTR_DIR) return VFS_EISDIR;

    /* Free cluster chain */
    uint32_t c = ((uint32_t)le16((uint8_t *)&de.cluster_hi) << 16) |
                  le16((uint8_t *)&de.cluster_lo);
    while (c >= 2 && c < FAT32_EOC) {
        uint32_t next = fat_get(m, c);
        fat_set(m, c, FAT32_FREE);
        c = next;
    }
    /* Mark entry deleted */
    de.name[0] = 0xE5;
    return fat32_write_dirent(m, dir_cluster, entry_idx, &de);
}

static int fat32_vfs_rename(vfs_mount_t *mnt, const char *old, const char *nw)
{
    fat32_mount_t *m = (fat32_mount_t *)mnt->priv;
    fat32_dirent_t de;
    uint32_t dir_cluster, entry_idx;
    if (fat32_resolve(m, old, &de, &dir_cluster, &entry_idx, NULL) != VFS_OK)
        return VFS_ENOENT;

    /* Extract new name component */
    const char *last = nw;
    const char *p = nw;
    while (*p) { if (*p == '/') last = p; p++; }
    while (*last == '/') last++;

    str_to_fat32_name(last, de.name, de.ext);
    return fat32_write_dirent(m, dir_cluster, entry_idx, &de);
}

static int fat32_vfs_mkdir(vfs_mount_t *mnt, const char *path)
{
    fat32_mount_t *m = (fat32_mount_t *)mnt->priv;

    /* Get parent cluster */
    const char *last = path;
    const char *p = path;
    while (*p) { if (*p == '/') last = p; p++; }

    uint32_t par_cluster = m->root_cluster;
    if (last != path) {
        char parent_path[260];
        int plen = (int)(last - path);
        kstrncpy(parent_path, path, plen + 1);
        parent_path[plen] = '\0';
        fat32_dirent_t par_de;
        if (fat32_resolve(m, parent_path, &par_de, NULL, NULL, NULL) == VFS_OK) {
            par_cluster = ((uint32_t)le16((uint8_t *)&par_de.cluster_hi) << 16) |
                           le16((uint8_t *)&par_de.cluster_lo);
            if (par_cluster < 2) par_cluster = m->root_cluster;
        }
    }

    const char *dname = last;
    while (*dname == '/') dname++;

    /* Allocate new cluster for the directory */
    uint32_t nc = fat_alloc(m);
    if (!nc) return VFS_ENOSPC;
    fat_set(m, nc, FAT32_EOC);

    /* Create the dirent in parent */
    uint32_t slot_cluster, slot_idx;
    if (fat32_alloc_dirent(m, par_cluster, &slot_cluster, &slot_idx) != VFS_OK)
        return VFS_ENOSPC;

    fat32_dirent_t de;
    kmemset(&de, 0, sizeof(de));
    str_to_fat32_name(dname, de.name, de.ext);
    de.attr = FAT32_ATTR_DIR;
    put_le16((uint8_t *)&de.cluster_hi, (uint16_t)(nc >> 16));
    put_le16((uint8_t *)&de.cluster_lo, (uint16_t)(nc & 0xFFFF));
    fat32_write_dirent(m, slot_cluster, slot_idx, &de);

    /* Write . and .. entries */
    fat32_dirent_t dot;
    kmemset(&dot, 0, sizeof(dot));
    kmemset(dot.name, ' ', 8); kmemset(dot.ext, ' ', 3);
    dot.name[0] = '.'; dot.attr = FAT32_ATTR_DIR;
    put_le16((uint8_t *)&dot.cluster_hi, (uint16_t)(nc >> 16));
    put_le16((uint8_t *)&dot.cluster_lo, (uint16_t)(nc & 0xFFFF));
    fat32_write_dirent(m, nc, 0, &dot);

    dot.name[1] = '.';
    put_le16((uint8_t *)&dot.cluster_hi, (uint16_t)(par_cluster >> 16));
    put_le16((uint8_t *)&dot.cluster_lo, (uint16_t)(par_cluster & 0xFFFF));
    fat32_write_dirent(m, nc, 1, &dot);

    return VFS_OK;
}

/* ── Directory iteration ──────────────────────────────────────────────────── */

static int fat32_vfs_opendir(vfs_mount_t *mnt, const char *path, void **dpriv)
{
    fat32_mount_t *m = (fat32_mount_t *)mnt->priv;

    fat32_dir_t *d = NULL;
    for (int i = 0; i < FAT32_MAX_OPEN_DIRS; i++) {
        if (!open_dirs[i].mnt) { d = &open_dirs[i]; break; }
    }
    if (!d) return VFS_ENOSPC;

    uint32_t cluster = m->root_cluster;
    if (kstrcmp(path, "/") != 0 && *path) {
        fat32_dirent_t de;
        if (fat32_resolve(m, path, &de, NULL, NULL, NULL) != VFS_OK)
            return VFS_ENOENT;
        if (!(de.attr & FAT32_ATTR_DIR)) return VFS_ENOTDIR;
        cluster = ((uint32_t)le16((uint8_t *)&de.cluster_hi) << 16) |
                   le16((uint8_t *)&de.cluster_lo);
        if (cluster < 2) cluster = m->root_cluster;
    }

    d->mnt       = m;
    d->cluster   = cluster;
    d->entry_idx = 0;
    *dpriv       = d;
    return VFS_OK;
}

static int fat32_vfs_readdir(vfs_mount_t *mnt, void *dpriv, vfs_dirent_t *ent)
{
    (void)mnt;
    fat32_dir_t *d = (fat32_dir_t *)dpriv;

    for (;;) {
        fat32_dirent_t de;
        if (fat32_read_dirent(d->mnt, d->cluster, d->entry_idx, &de) != 0)
            return VFS_ENOENT;

        d->entry_idx++;

        if (de.name[0] == 0x00) return VFS_ENOENT;  /* end */
        if ((uint8_t)de.name[0] == 0xE5) continue;        /* deleted */
        if (de.attr == FAT32_ATTR_LFN)   continue;
        if (de.attr & FAT32_ATTR_VOLID)  continue;
        /* Skip . and .. */
        if (de.name[0] == '.') continue;

        fat32_name_to_str(&de, ent->name);
        ent->size  = de.file_size;
        //         ent->attrs = (de.attr & FAT32_ATTR_DIR)    ? VFS_ATTR_DIR    : 0;
        //         ent->attrs|= (de.attr & FAT32_ATTR_RDONLY) ? VFS_ATTR_RDONLY : 0;
        ent->attrs|= (de.attr & FAT32_ATTR_HIDDEN) ? VFS_ATTR_HIDDEN : 0;
        return VFS_OK;
    }
}

static int fat32_vfs_closedir(vfs_mount_t *mnt, void *dpriv)
{
    (void)mnt;
    fat32_dir_t *d = (fat32_dir_t *)dpriv;
    kmemset(d, 0, sizeof(*d));
    return VFS_OK;
}

/* ── VFS driver struct ────────────────────────────────────────────────────── */

vfs_fs_t vfs_fat32_fs = {
    .name     = "fat32",
    .mount    = fat32_vfs_mount,
    .unmount  = fat32_vfs_unmount,
    .open     = fat32_vfs_open,
    .close    = fat32_vfs_close,
    .read     = fat32_vfs_read,
    .write    = fat32_vfs_write,
    .seek     = fat32_vfs_seek,
    .stat     = fat32_vfs_stat,
    .unlink   = fat32_vfs_unlink,
    .rename   = fat32_vfs_rename,
    .mkdir    = fat32_vfs_mkdir,
    .opendir  = fat32_vfs_opendir,
    .readdir  = fat32_vfs_readdir,
    .closedir = fat32_vfs_closedir,
};
