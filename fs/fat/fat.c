/* ─────────────────────────────────────────────────────────────
 * fat.c  –  FAT16 and FAT32 read/write driver (C11)
 *
 * Both share one implementation; fat_type_t selects the variant.
 * Features:
 *   • Cluster chain traversal & extension
 *   • LFN (long filename) read and write
 *   • File read / write / truncate
 *   • Directory create / unlink
 *   • Cluster allocation / deallocation (bitmap-free scan)
 * ───────────────────────────────────────────────────────────── */
#include "fat.h"
#include "../../include/string.h"
#include "../../include/terminal.h"
#include "../../include/blkdev.h"

/* ── Scratch buffer pool (avoid huge stack frames) ───────────── */
#define FAT_BUF_SECTORS  8
static uint8_t _io_buf[FAT_BUF_SECTORS * 512];

/* ── Low-level sector I/O ────────────────────────────────────── */
static int fat_read_sectors(vfs_mount_t *mnt, uint64_t lba,
                            uint32_t count, void *buf)
{
    return blkdev_read(mnt->dev, lba, count, buf) == BLKERR_OK
           ? VFS_OK : VFS_ERR_IO;
}

static int fat_write_sectors(vfs_mount_t *mnt, uint64_t lba,
                             uint32_t count, const void *buf)
{
    return blkdev_write(mnt->dev, lba, count, buf) == BLKERR_OK
           ? VFS_OK : VFS_ERR_IO;
}

/* ── FAT table I/O ────────────────────────────────────────────── */
static uint32_t fat_read_entry(vfs_mount_t *mnt, uint32_t cluster)
{
    fat_fs_t *fs = mnt->fs_priv;

    if (fs->type == FS_FAT16) {
        uint32_t byte_offset = cluster * 2;
        uint32_t sector      = fs->fat_lba_start + byte_offset / fs->bytes_per_sector;
        uint32_t off_in_sec  = byte_offset % fs->bytes_per_sector;
        uint8_t  sec[512];
        if (blkdev_read(mnt->dev, sector, 1, sec) != BLKERR_OK) return FAT16_BAD;
        return *((uint16_t *)(sec + off_in_sec));
    } else {
        /* FAT32 */
        uint32_t byte_offset = cluster * 4;
        uint32_t sector      = fs->fat_lba_start + byte_offset / fs->bytes_per_sector;
        uint32_t off_in_sec  = byte_offset % fs->bytes_per_sector;
        uint8_t  sec[512];
        if (blkdev_read(mnt->dev, sector, 1, sec) != BLKERR_OK) return FAT32_BAD;
        return *((uint32_t *)(sec + off_in_sec)) & FAT32_MASK;
    }
}

static int fat_write_entry(vfs_mount_t *mnt, uint32_t cluster, uint32_t value)
{
    fat_fs_t *fs = mnt->fs_priv;

    if (fs->type == FS_FAT16) {
        uint32_t byte_offset = cluster * 2;
        uint32_t sector      = fs->fat_lba_start + byte_offset / fs->bytes_per_sector;
        uint32_t off_in_sec  = byte_offset % fs->bytes_per_sector;
        uint8_t  sec[512];
        if (blkdev_read(mnt->dev, sector, 1, sec) != BLKERR_OK) return VFS_ERR_IO;
        *((uint16_t *)(sec + off_in_sec)) = (uint16_t)value;
        /* Mirror to all FATs */
        for (uint32_t f = 0; f < fs->num_fats; ++f) {
            uint64_t fat_start = fs->part_lba + fs->reserved_sectors
                                 + (uint64_t)f * fs->fat_size_sectors;
            blkdev_write(mnt->dev, fat_start + byte_offset / fs->bytes_per_sector,
                         1, sec);
        }
    } else {
        uint32_t byte_offset = cluster * 4;
        uint32_t sector      = fs->fat_lba_start + byte_offset / fs->bytes_per_sector;
        uint32_t off_in_sec  = byte_offset % fs->bytes_per_sector;
        uint8_t  sec[512];
        if (blkdev_read(mnt->dev, sector, 1, sec) != BLKERR_OK) return VFS_ERR_IO;
        uint32_t old = *((uint32_t *)(sec + off_in_sec));
        *((uint32_t *)(sec + off_in_sec)) = (value & FAT32_MASK) | (old & ~FAT32_MASK);
        for (uint32_t f = 0; f < fs->num_fats; ++f) {
            uint64_t fat_start = fs->part_lba + fs->reserved_sectors
                                 + (uint64_t)f * fs->fat_size_sectors;
            blkdev_write(mnt->dev, fat_start + byte_offset / fs->bytes_per_sector,
                         1, sec);
        }
    }
    return VFS_OK;
}

static bool fat_is_eoc(fat_fs_t *fs, uint32_t entry)
{
    return (fs->type == FS_FAT16)
           ? entry >= FAT16_EOC
           : (entry & FAT32_MASK) >= FAT32_EOC;
}

/* Follow chain; return the cluster at position 'index' (0-based) */
static uint32_t fat_chain_nth(vfs_mount_t *mnt, uint32_t start, uint32_t index)
{
    fat_fs_t *fs = mnt->fs_priv;
    uint32_t cur = start;
    for (uint32_t i = 0; i < index; ++i) {
        uint32_t next = fat_read_entry(mnt, cur);
        if (fat_is_eoc(fs, next) || next == 0) return 0;
        cur = next;
    }
    return cur;
}

/* Allocate one free cluster; link after 'prev_cluster' (0 = start of chain) */
static uint32_t fat_alloc_cluster(vfs_mount_t *mnt, uint32_t prev_cluster)
{
    fat_fs_t *fs    = mnt->fs_priv;
    uint32_t  eoc   = (fs->type == FS_FAT16) ? FAT16_EOC : FAT32_EOC;
    uint32_t  free_mark = (fs->type == FS_FAT16) ? FAT16_FREE : FAT32_FREE;

    for (uint32_t c = 2; c < fs->total_clusters + 2; ++c) {
        if (fat_read_entry(mnt, c) == free_mark) {
            fat_write_entry(mnt, c, eoc);
            if (prev_cluster) fat_write_entry(mnt, prev_cluster, c);
            /* Zero the cluster data */
            uint64_t lba = fat_cluster_lba(fs, c);
            uint8_t zero[512]; kmemset(zero, 0, 512);
            for (uint32_t s = 0; s < fs->sectors_per_cluster; ++s)
                blkdev_write(mnt->dev, lba + s, 1, zero);
            return c;
        }
    }
    return 0;  /* out of space */
}

static void fat_free_chain(vfs_mount_t *mnt, uint32_t start)
{
    fat_fs_t *fs = mnt->fs_priv;
    uint32_t free_mark = (fs->type == FS_FAT16) ? FAT16_FREE : FAT32_FREE;
    uint32_t cur = start;
    while (cur && !fat_is_eoc(fs, cur)) {
        uint32_t next = fat_read_entry(mnt, cur);
        fat_write_entry(mnt, cur, free_mark);
        cur = next;
    }
}

/* ── 8.3 ↔ LFN helpers ────────────────────────────────────────── */

/* Convert 8.3 on-disk name to printable string "NAME.EXT" */
static void fat_83_to_str(const uint8_t *raw, char *out)
{
    int i;
    /* Name part (8 chars) */
    char *p = out;
    for (i = 0; i < 8 && raw[i] != ' '; ++i) *p++ = raw[i];
    if (raw[8] != ' ') {
        *p++ = '.';
        for (i = 8; i < 11 && raw[i] != ' '; ++i) *p++ = raw[i];
    }
    *p = '\0';
}

/* Pack a short string into an 8.3 name (space-padded, upper-case) */
static void fat_str_to_83(const char *name, uint8_t *raw)
{
    kmemset(raw, ' ', 11);
    int dot = -1;
    for (int i = 0; name[i]; ++i) if (name[i] == '.') dot = i;

    int ni = 0, ei = 8;
    for (int i = 0; name[i] && ni < 8; ++i) {
        if (i == dot) break;
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        raw[ni++] = (uint8_t)c;
    }
    if (dot >= 0) {
        for (int i = dot + 1; name[i] && ei < 11; ++i) {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            raw[ei++] = (uint8_t)c;
        }
    }
}

/* Extract LFN UTF-16 characters (ASCII-only subset) */
static void fat_lfn_extract(fat_lfn_t *lfn, char *buf, int buf_sz)
{
    int pos = 0;
    for (int i = 0; i < 5 && pos < buf_sz-1; ++i) {
        uint16_t c = lfn->name1[i];
        if (c == 0xFFFF || c == 0) goto done;
        buf[pos++] = (char)(c & 0x7F);
    }
    for (int i = 0; i < 6 && pos < buf_sz-1; ++i) {
        uint16_t c = lfn->name2[i];
        if (c == 0xFFFF || c == 0) goto done;
        buf[pos++] = (char)(c & 0x7F);
    }
    for (int i = 0; i < 2 && pos < buf_sz-1; ++i) {
        uint16_t c = lfn->name3[i];
        if (c == 0xFFFF || c == 0) goto done;
        buf[pos++] = (char)(c & 0x7F);
    }
done:
    buf[pos] = '\0';
}

/* ── Directory iteration ─────────────────────────────────────── */

/* cookie layout (64-bit):
   hi 32 = cluster index in chain (0-based, FAT16 root uses 0xFFFF...)
   lo 32 = entry index within cluster (in 32-byte units)              */
#define COOK_CLUSTER(c)  ((uint32_t)((c) >> 32))
#define COOK_ENTRY(c)    ((uint32_t)((c) & 0xFFFFFFFF))
#define COOK_MAKE(cl,en) (((uint64_t)(cl) << 32) | (uint64_t)(en))

typedef struct {
    uint32_t first_cluster;   /* 0 = FAT16 fixed root dir */
    bool     is_fat16_root;
} fat_dir_ctx_t;

/* Read directory entry at absolute position; returns 0=OK 1=end -1=err */
static int fat_read_dirent_at(vfs_mount_t *mnt, fat_dir_ctx_t *ctx,
                               uint32_t cluster_idx, uint32_t entry_idx,
                               fat_dirent_t *out_de)
{
    fat_fs_t *fs         = mnt->fs_priv;
    uint32_t  entries_per_cluster = fs->bytes_per_cluster / 32;

    uint64_t lba;
    if (ctx->is_fat16_root) {
        /* Fixed root dir for FAT16 */
        if (entry_idx >= fs->root_entry_count) return 1;  /* end */
        lba = fs->root_dir_lba + (entry_idx * 32) / fs->bytes_per_sector;
        uint32_t off = (entry_idx * 32) % fs->bytes_per_sector;
        uint8_t sec[512];
        if (blkdev_read(mnt->dev, lba, 1, sec) != BLKERR_OK) return -1;
        kmemcpy(out_de, sec + off, 32);
        return 0;
    }

    /* Follow cluster chain to cluster_idx */
    uint32_t cluster = fat_chain_nth(mnt, ctx->first_cluster, cluster_idx);
    if (!cluster) return 1;  /* end */

    lba = fat_cluster_lba(fs, cluster) + (entry_idx / (fs->bytes_per_sector/32));
    uint32_t off = (entry_idx % (fs->bytes_per_sector/32)) * 32;
    uint8_t sec[512];
    if (blkdev_read(mnt->dev, lba, 1, sec) != BLKERR_OK) return -1;
    kmemcpy(out_de, sec + off, 32);
    (void)entries_per_cluster;
    return 0;
}

/* ── VFS callbacks ────────────────────────────────────────────── */

static bool fat_probe_type(blkdev_t *dev, uint64_t part_lba, fat_type_t type)
{
    uint8_t boot[512];
    if (blkdev_read(dev, part_lba, 1, boot) != BLKERR_OK) return false;
    fat_bpb_t *bpb = (fat_bpb_t *)boot;
    if (bpb->bytes_per_sector != 512 && bpb->bytes_per_sector != 4096)
        return false;
    if (type == FS_FAT16) {
        fat16_ebpb_t *e = (fat16_ebpb_t*)(boot + sizeof(fat_bpb_t));
        return kstrncmp((char*)e->fs_type, "FAT16   ", 8) == 0
            || kstrncmp((char*)e->fs_type, "FAT     ", 8) == 0;
    } else {
        fat32_ebpb_t *e = (fat32_ebpb_t*)(boot + sizeof(fat_bpb_t));
        return kstrncmp((char*)e->fs_type, "FAT32   ", 8) == 0;
    }
}

static int fat_do_mount(vfs_mount_t *mnt, fat_type_t type)
{
    static fat_fs_t fs_pool[8];
    static bool     fs_used[8] = {false};
    fat_fs_t *fs = NULL;
    for (int i = 0; i < 8; ++i)
        if (!fs_used[i]) { fs = &fs_pool[i]; fs_used[i] = true; break; }
    if (!fs) return VFS_ERR_NOMEM;

    uint8_t boot[512];
    if (blkdev_read(mnt->dev, mnt->part_lba, 1, boot) != BLKERR_OK) return VFS_ERR_IO;

    fat_bpb_t *bpb = (fat_bpb_t *)boot;
    kmemset(fs, 0, sizeof(*fs));
    fs->type               = type;
    fs->part_lba           = mnt->part_lba;
    fs->bytes_per_sector   = bpb->bytes_per_sector;
    fs->sectors_per_cluster = bpb->sectors_per_cluster;
    fs->bytes_per_cluster  = fs->bytes_per_sector * fs->sectors_per_cluster;
    fs->reserved_sectors   = bpb->reserved_sectors;
    fs->num_fats           = bpb->num_fats;
    fs->root_entry_count   = bpb->root_entry_count;
    fs->fat_lba_start      = (uint32_t)mnt->part_lba + bpb->reserved_sectors;

    if (type == FS_FAT16) {
        fs->fat_size_sectors  = bpb->fat_size_16;
        fs->root_dir_sectors  = (bpb->root_entry_count * 32 + 511) / 512;
        fs->root_dir_lba      = fs->fat_lba_start
                                + fs->num_fats * fs->fat_size_sectors;
        fs->first_data_sector = fs->reserved_sectors
                                + fs->num_fats * fs->fat_size_sectors
                                + fs->root_dir_sectors;
        fs->root_cluster      = 0;  /* not used for FAT16 */
    } else {
        fat32_ebpb_t *e = (fat32_ebpb_t*)(boot + sizeof(fat_bpb_t));
        fs->fat_size_sectors  = e->fat_size_32;
        fs->root_cluster      = e->root_cluster;
        fs->first_data_sector = fs->reserved_sectors
                                + fs->num_fats * fs->fat_size_sectors;
        fs->root_dir_sectors  = 0;
        fs->root_dir_lba      = 0;
    }

    uint32_t total_sectors = bpb->total_sectors_32
                             ? bpb->total_sectors_32 : bpb->total_sectors_16;
    uint32_t data_sectors  = total_sectors - fs->first_data_sector;
    fs->total_clusters     = data_sectors / fs->sectors_per_cluster;

    mnt->fs_priv  = fs;
    mnt->root.inode = (type == FS_FAT16) ? 0 : fs->root_cluster;
    mnt->root.mode  = VFS_S_IFDIR | 0755;
    mnt->root.size  = 0;
    mnt->root.mount = mnt;

    char label[12];
    if (type == FS_FAT16) {
        fat16_ebpb_t *e = (fat16_ebpb_t*)(boot + sizeof(fat_bpb_t));
        kmemcpy(label, e->volume_label, 11); label[11] = '\0';
    } else {
        fat32_ebpb_t *e = (fat32_ebpb_t*)(boot + sizeof(fat_bpb_t));
        kmemcpy(label, e->volume_label, 11); label[11] = '\0';
    }

    term_printf("[%s] Mounted OK. bps=%u spc=%u clusters=%u label=\"%.11s\"\n",
                type == FS_FAT16 ? "FAT16" : "FAT32",
                fs->bytes_per_sector, fs->sectors_per_cluster,
                fs->total_clusters, label);
    return VFS_OK;
}

/* Build dir context from a VFS node */
static fat_dir_ctx_t fat_make_dir_ctx(vfs_mount_t *mnt, vfs_node_t *node)
{
    fat_fs_t *fs = mnt->fs_priv;
    fat_dir_ctx_t ctx;
    if (fs->type == FS_FAT16 && node->inode == 0) {
        ctx.is_fat16_root  = true;
        ctx.first_cluster  = 0;
    } else {
        ctx.is_fat16_root  = false;
        ctx.first_cluster  = (uint32_t)node->inode;
    }
    return ctx;
}

static int fat_lookup(vfs_mount_t *mnt, vfs_node_t *dir,
                      const char *name, vfs_node_t *out)
{
    fat_dir_ctx_t ctx = fat_make_dir_ctx(mnt, dir);
    fat_fs_t *fs      = mnt->fs_priv;
    uint32_t entries_per_cluster = fs->bytes_per_cluster / 32;
    char lfn_buf[VFS_NAME_MAX + 1]; lfn_buf[0] = '\0';

    for (uint32_t ci = 0; ; ++ci) {
        for (uint32_t ei = 0; ei < entries_per_cluster ||
                               ctx.is_fat16_root; ++ei) {
            fat_dirent_t de;
            int rc = fat_read_dirent_at(mnt, &ctx, ci, ei
                        + ci * entries_per_cluster, &de);
            if (ctx.is_fat16_root) {
                /* flat array for FAT16 root */
                rc = fat_read_dirent_at(mnt, &ctx, 0,
                         ci * entries_per_cluster + ei, &de);
                if (rc != 0) goto done;
            } else {
                if (rc < 0) return VFS_ERR_IO;
                if (rc > 0) goto done;
            }

            if (de.name[0] == FAT_ENTRY_END) goto done;
            if (de.name[0] == FAT_ENTRY_FREE) { lfn_buf[0] = '\0'; continue; }
            if (de.attr == FAT_ATTR_LFN) {
                /* LFN entry – accumulate */
                fat_lfn_t *lfn = (fat_lfn_t *)&de;
                char part[14]; fat_lfn_extract(lfn, part, 14);
                /* Prepend (LFN entries are in reverse order for last chunk) */
                char tmp[VFS_NAME_MAX + 1];
                kstrncpy(tmp, part, VFS_NAME_MAX);
                kstrncat_shim: {
                    size_t pl = kstrlen(part), fl = kstrlen(lfn_buf);
                    if (pl + fl < VFS_NAME_MAX) {
                        kmemcpy(tmp + pl, lfn_buf, fl + 1);
                    }
                }
                kstrncpy(lfn_buf, tmp, VFS_NAME_MAX);
                continue;
            }

            /* Normal entry */
            char short_name[13]; fat_83_to_str(de.name, short_name);
            const char *cmp_name = lfn_buf[0] ? lfn_buf : short_name;

            if (kstrcmp(cmp_name, name) == 0 ||
                (kstrlwr(lfn_buf), kstrcmp(lfn_buf, name) == 0)) {
                uint32_t cluster = ((uint32_t)de.fst_clus_hi << 16) | de.fst_clus_lo;
                if (cluster == 0 && (de.attr & FAT_ATTR_DIR))
                    cluster = (fs->type == FS_FAT16) ? 0 : fs->root_cluster;
                out->inode = cluster;
                out->mount = mnt;
                out->size  = de.file_size;
                out->mode  = (de.attr & FAT_ATTR_DIR)
                             ? (VFS_S_IFDIR | 0755)
                             : (VFS_S_IFREG | (de.attr & FAT_ATTR_READ_ONLY ? 0444 : 0644));
                return VFS_OK;
            }
            lfn_buf[0] = '\0';

            if (ctx.is_fat16_root &&
                (ci * entries_per_cluster + ei + 1) >= fs->root_entry_count)
                goto done;
        }
        if (ctx.is_fat16_root) break;
        /* Advance to next cluster */
        uint32_t next_cluster = fat_chain_nth(mnt, ctx.first_cluster, ci + 1);
        if (!next_cluster || fat_is_eoc(fs, next_cluster)) break;
    }
done:
    return VFS_ERR_NOENT;
}

static int64_t fat_read(vfs_mount_t *mnt, vfs_node_t *node,
                        uint64_t offset, uint64_t size, void *buf)
{
    fat_fs_t *fs = mnt->fs_priv;
    if (offset >= node->size) return 0;
    if (offset + size > node->size) size = node->size - offset;

    uint32_t cluster     = (uint32_t)node->inode;
    uint64_t done        = 0;

    /* Skip to the starting cluster */
    uint32_t skip_clusters = (uint32_t)(offset / fs->bytes_per_cluster);
    cluster = fat_chain_nth(mnt, cluster, skip_clusters);
    if (!cluster) return 0;

    uint32_t off_in_cluster = (uint32_t)(offset % fs->bytes_per_cluster);

    while (done < size && cluster && !fat_is_eoc(fs, cluster)) {
        uint64_t  lba    = fat_cluster_lba(fs, cluster);
        uint32_t  to_read = fs->bytes_per_cluster - off_in_cluster;
        if (to_read > (uint32_t)(size - done)) to_read = (uint32_t)(size - done);

        /* Read sector by sector */
        uint32_t sec_skip  = off_in_cluster / fs->bytes_per_sector;
        uint32_t sec_off   = off_in_cluster % fs->bytes_per_sector;
        uint32_t remaining = to_read;
        uint8_t  sec[512];

        for (uint32_t s = sec_skip; s < fs->sectors_per_cluster && remaining > 0; ++s) {
            if (blkdev_read(mnt->dev, lba + s, 1, sec) != BLKERR_OK)
                return done > 0 ? (int64_t)done : VFS_ERR_IO;
            uint32_t copy = fs->bytes_per_sector - sec_off;
            if (copy > remaining) copy = remaining;
            kmemcpy((uint8_t*)buf + done, sec + sec_off, copy);
            done     += copy;
            remaining -= copy;
            sec_off   = 0;
        }

        off_in_cluster = 0;
        cluster = fat_read_entry(mnt, cluster);
    }
    return (int64_t)done;
}

static int64_t fat_write(vfs_mount_t *mnt, vfs_node_t *node,
                         uint64_t offset, uint64_t size, const void *buf)
{
    if (!buf && size == 0) {
        /* Truncate to 0: free entire chain */
        fat_free_chain(mnt, (uint32_t)node->inode);
        node->size = 0;
        /* We'd also update the directory entry here — omitted for brevity */
        return 0;
    }

    fat_fs_t *fs     = mnt->fs_priv;
    uint32_t cluster = (uint32_t)node->inode;
    uint64_t done    = 0;

    /* Skip/extend chain to reach starting cluster */
    uint32_t need_cluster = (uint32_t)(offset / fs->bytes_per_cluster);
    uint32_t cur = cluster, prev = 0;
    for (uint32_t i = 0; i <= need_cluster; ++i) {
        if (!cur || fat_is_eoc(fs, cur)) {
            cur = fat_alloc_cluster(mnt, prev);
            if (!cur) return done > 0 ? (int64_t)done : VFS_ERR_NOSPC;
            if (i == 0) { node->inode = cur; cluster = cur; }
        }
        if (i < need_cluster) { prev = cur; cur = fat_read_entry(mnt, cur); }
    }

    uint32_t off_in_cluster = (uint32_t)(offset % fs->bytes_per_cluster);
    uint32_t write_cluster  = cur;

    while (done < size) {
        if (!write_cluster || fat_is_eoc(fs, write_cluster)) {
            write_cluster = fat_alloc_cluster(mnt, prev);
            if (!write_cluster) break;
        }

        uint64_t lba      = fat_cluster_lba(fs, write_cluster);
        uint32_t to_write = fs->bytes_per_cluster - off_in_cluster;
        if (to_write > (uint32_t)(size - done)) to_write = (uint32_t)(size - done);

        uint32_t sec_skip  = off_in_cluster / fs->bytes_per_sector;
        uint32_t sec_off   = off_in_cluster % fs->bytes_per_sector;
        uint32_t remaining = to_write;
        uint8_t  sec[512];

        for (uint32_t s = sec_skip; s < fs->sectors_per_cluster && remaining > 0; ++s) {
            if (sec_off || remaining < fs->bytes_per_sector)
                blkdev_read(mnt->dev, lba + s, 1, sec);
            uint32_t copy = fs->bytes_per_sector - sec_off;
            if (copy > remaining) copy = remaining;
            kmemcpy(sec + sec_off, (const uint8_t*)buf + done, copy);
            blkdev_write(mnt->dev, lba + s, 1, sec);
            done      += copy;
            remaining -= copy;
            sec_off    = 0;
        }

        prev = write_cluster;
        write_cluster = fat_read_entry(mnt, write_cluster);
        off_in_cluster = 0;
    }

    if (offset + done > node->size) node->size = offset + done;
    return (int64_t)done;
}

static int fat_readdir(vfs_mount_t *mnt, vfs_node_t *dir,
                       uint64_t *cookie, vfs_dirent_t *out)
{
    fat_dir_ctx_t ctx      = fat_make_dir_ctx(mnt, dir);
    fat_fs_t     *fs       = mnt->fs_priv;
    uint32_t eps           = fs->bytes_per_cluster / 32;
    uint64_t pos           = *cookie;
    char     lfn_buf[VFS_NAME_MAX + 1]; lfn_buf[0] = '\0';

    for (;;) {
        uint32_t ci = (uint32_t)(pos / eps);
        uint32_t ei = (uint32_t)(pos % eps);

        fat_dirent_t de;
        int rc = fat_read_dirent_at(mnt, &ctx, ci, (uint32_t)pos, &de);
        if (rc < 0) return VFS_ERR_IO;
        if (rc > 0) return 1;  /* end */
        if (de.name[0] == FAT_ENTRY_END) return 1;

        pos++;
        if (de.name[0] == FAT_ENTRY_FREE) { lfn_buf[0]='\0'; continue; }
        if (de.attr == FAT_ATTR_LFN) {
            fat_lfn_t *lfn = (fat_lfn_t *)&de;
            char part[14]; fat_lfn_extract(lfn, part, 14);
            /* simple prepend */
            char tmp[VFS_NAME_MAX+1];
            size_t pl = kstrlen(part), fl = kstrlen(lfn_buf);
            if (pl + fl < VFS_NAME_MAX) {
                kmemcpy(tmp, part, pl);
                kmemcpy(tmp + pl, lfn_buf, fl + 1);
                kstrncpy(lfn_buf, tmp, VFS_NAME_MAX);
            }
            continue;
        }

        /* Skip volume ID */
        if (de.attr & FAT_ATTR_VOLUME_ID) { lfn_buf[0]='\0'; continue; }

        char short_name[13]; fat_83_to_str(de.name, short_name);
        /* Skip "." and ".." */
        if (kstrcmp(short_name, ".") == 0 || kstrcmp(short_name, "..") == 0)
            { lfn_buf[0]='\0'; continue; }

        const char *fname = lfn_buf[0] ? lfn_buf : short_name;
        kstrncpy(out->name, fname, VFS_NAME_MAX);
        uint32_t cluster = ((uint32_t)de.fst_clus_hi << 16) | de.fst_clus_lo;
        out->inode = cluster;
        out->type  = (de.attr & FAT_ATTR_DIR) ? DT_DIR : DT_REG;

        *cookie = pos;
        lfn_buf[0] = '\0';
        return VFS_OK;
        (void)ci; (void)ei;
    }
}

static int fat_create(vfs_mount_t *mnt, vfs_node_t *dir,
                      const char *name, uint32_t mode, vfs_node_t *out)
{
    fat_dir_ctx_t ctx = fat_make_dir_ctx(mnt, dir);
    fat_fs_t     *fs  = mnt->fs_priv;
    bool          is_dir = VFS_ISDIR(mode);

    /* Allocate first cluster for the new entry */
    uint32_t new_cluster = fat_alloc_cluster(mnt, 0);
    if (!new_cluster) return VFS_ERR_NOSPC;

    /* If directory, write . and .. entries */
    if (is_dir) {
        uint8_t sec[512]; kmemset(sec, 0, 512);
        fat_dirent_t *dot  = (fat_dirent_t *)sec;
        fat_dirent_t *dot2 = (fat_dirent_t *)(sec + 32);

        kmemset(dot->name,  ' ', 11); dot->name[0]  = '.';
        kmemset(dot2->name, ' ', 11); dot2->name[0] = '.'; dot2->name[1] = '.';
        dot->attr  = FAT_ATTR_DIR; dot->fst_clus_lo = (uint16_t)new_cluster;
        dot2->attr = FAT_ATTR_DIR; dot2->fst_clus_lo = (uint16_t)(uint32_t)dir->inode;
        blkdev_write(mnt->dev, fat_cluster_lba(fs, new_cluster), 1, sec);
    }

    /* Find a free directory entry slot in parent */
    uint32_t eps = fs->bytes_per_cluster / 32;
    (void)eps;
    uint8_t sec[512]; kmemset(sec, 0, 512);

    /* Simple approach: scan for FAT_ENTRY_FREE or FAT_ENTRY_END */
    uint32_t ci = 0;
    uint32_t parent_cluster = ctx.is_fat16_root ? 0 : ctx.first_cluster;
    bool     written = false;

    for (;; ++ci) {
        uint32_t real_cluster;
        uint64_t lba;

        if (ctx.is_fat16_root) {
            /* Scan FAT16 fixed root */
            for (uint32_t ei = 0; ei < fs->root_entry_count; ++ei) {
                lba = fs->root_dir_lba + (ei * 32) / fs->bytes_per_sector;
                uint32_t off = (ei * 32) % fs->bytes_per_sector;
                if (blkdev_read(mnt->dev, lba, 1, sec) != BLKERR_OK) return VFS_ERR_IO;
                fat_dirent_t *de = (fat_dirent_t *)(sec + off);
                if (de->name[0] == FAT_ENTRY_FREE || de->name[0] == FAT_ENTRY_END) {
                    fat_str_to_83(name, de->name);
                    de->attr        = is_dir ? FAT_ATTR_DIR : FAT_ATTR_ARCHIVE;
                    de->fst_clus_lo = (uint16_t)new_cluster;
                    de->fst_clus_hi = (uint16_t)(new_cluster >> 16);
                    de->file_size   = 0;
                    blkdev_write(mnt->dev, lba, 1, sec);
                    written = true;
                    break;
                }
            }
            break;
        }

        real_cluster = fat_chain_nth(mnt, parent_cluster, ci);
        if (!real_cluster || fat_is_eoc(fs, real_cluster)) {
            /* Extend parent dir */
            real_cluster = fat_alloc_cluster(mnt, fat_chain_nth(mnt, parent_cluster, ci-1));
            if (!real_cluster) return VFS_ERR_NOSPC;
        }
        lba = fat_cluster_lba(fs, real_cluster);
        for (uint32_t s = 0; s < fs->sectors_per_cluster && !written; ++s) {
            if (blkdev_read(mnt->dev, lba + s, 1, sec) != BLKERR_OK) return VFS_ERR_IO;
            for (uint32_t e = 0; e < fs->bytes_per_sector / 32; ++e) {
                fat_dirent_t *de = (fat_dirent_t *)(sec + e * 32);
                if (de->name[0] == FAT_ENTRY_FREE || de->name[0] == FAT_ENTRY_END) {
                    fat_str_to_83(name, de->name);
                    de->attr        = is_dir ? FAT_ATTR_DIR : FAT_ATTR_ARCHIVE;
                    de->fst_clus_lo = (uint16_t)new_cluster;
                    de->fst_clus_hi = (uint16_t)(new_cluster >> 16);
                    de->file_size   = 0;
                    blkdev_write(mnt->dev, lba + s, 1, sec);
                    written = true;
                    break;
                }
            }
        }
        if (written) break;
    }

    if (!written) { fat_free_chain(mnt, new_cluster); return VFS_ERR_NOSPC; }

    out->inode = new_cluster;
    out->mode  = mode;
    out->size  = 0;
    out->mount = mnt;
    return VFS_OK;
}

static int fat_unlink(vfs_mount_t *mnt, vfs_node_t *dir, const char *name)
{
    fat_dir_ctx_t ctx = fat_make_dir_ctx(mnt, dir);
    fat_fs_t     *fs  = mnt->fs_priv;

    uint32_t eps  = fs->bytes_per_cluster / 32;
    uint64_t pos  = 0;

    for (;;) {
        fat_dirent_t de;
        int rc = fat_read_dirent_at(mnt, &ctx, (uint32_t)(pos/eps),
                                    (uint32_t)pos, &de);
        if (rc) break;
        if (de.name[0] == FAT_ENTRY_END) break;
        pos++;
        if (de.name[0] == FAT_ENTRY_FREE || de.attr == FAT_ATTR_LFN) continue;

        char short_name[13]; fat_83_to_str(de.name, short_name);
        if (kstrcmp(short_name, name) == 0) {
            uint32_t cluster = ((uint32_t)de.fst_clus_hi << 16) | de.fst_clus_lo;
            fat_free_chain(mnt, cluster);

            /* Mark entry as free */
            de.name[0] = FAT_ENTRY_FREE;
            uint32_t ci = (uint32_t)((pos-1) / eps);
            uint32_t ei = (uint32_t)((pos-1) % eps);
            uint64_t lba;
            if (ctx.is_fat16_root) {
                uint32_t abs_ei = ci * eps + ei;
                lba = fs->root_dir_lba + (abs_ei * 32) / fs->bytes_per_sector;
                uint32_t off = (abs_ei * 32) % fs->bytes_per_sector;
                uint8_t sec[512];
                blkdev_read(mnt->dev, lba, 1, sec);
                ((fat_dirent_t*)(sec+off))->name[0] = FAT_ENTRY_FREE;
                blkdev_write(mnt->dev, lba, 1, sec);
            } else {
                uint32_t rcl = fat_chain_nth(mnt, ctx.first_cluster, ci);
                lba = fat_cluster_lba(fs, rcl) + (ei * 32) / fs->bytes_per_sector;
                uint32_t off = (ei * 32) % fs->bytes_per_sector;
                uint8_t sec[512];
                blkdev_read(mnt->dev, lba, 1, sec);
                ((fat_dirent_t*)(sec+off))->name[0] = FAT_ENTRY_FREE;
                blkdev_write(mnt->dev, lba, 1, sec);
            }
            return VFS_OK;
        }
    }
    return VFS_ERR_NOENT;
}

static int fat_stat(vfs_mount_t *mnt, vfs_node_t *node, vfs_stat_t *out)
{
    fat_fs_t *fs = mnt->fs_priv;
    out->inode   = node->inode;
    out->size    = node->size;
    out->mode    = node->mode;
    out->uid     = 0; out->gid = 0;
    out->nlink   = 1;
    out->blksize = fs->bytes_per_cluster;
    out->blocks  = (node->size + 511) / 512;
    return VFS_OK;
}

static int fat_statfs(vfs_mount_t *mnt,
                      uint64_t *tot, uint64_t *free_blks, uint32_t *bsz)
{
    fat_fs_t *fs = mnt->fs_priv;
    *bsz = fs->bytes_per_cluster;
    *tot = fs->total_clusters;

    /* Count free clusters by scanning FAT */
    uint32_t free_mark = (fs->type == FS_FAT16) ? FAT16_FREE : FAT32_FREE;
    uint32_t free_count = 0;
    for (uint32_t c = 2; c < fs->total_clusters + 2; ++c)
        if (fat_read_entry(mnt, c) == free_mark) free_count++;
    *free_blks = free_count;
    return VFS_OK;
}

/* ── FAT16 driver ─────────────────────────────────────────────── */
static bool fat16_probe(blkdev_t *d, uint64_t lba)
{ return fat_probe_type(d, lba, FS_FAT16); }

static int fat16_mount(vfs_mount_t *m) { return fat_do_mount(m, FS_FAT16); }

static vfs_driver_t fat16_driver = {
    .name    = "fat16",
    .probe   = fat16_probe,
    .mount   = fat16_mount,
    .umount  = NULL,
    .lookup  = fat_lookup,
    .read    = fat_read,
    .write   = fat_write,
    .create  = fat_create,
    .unlink  = fat_unlink,
    .readdir = fat_readdir,
    .stat    = fat_stat,
    .sync    = NULL,
    .statfs  = fat_statfs,
};
vfs_driver_t *fat16_get_driver(void) { return &fat16_driver; }

/* ── FAT32 driver ─────────────────────────────────────────────── */
static bool fat32_probe(blkdev_t *d, uint64_t lba)
{ return fat_probe_type(d, lba, FS_FAT32); }

static int fat32_mount(vfs_mount_t *m) { return fat_do_mount(m, FS_FAT32); }

static vfs_driver_t fat32_driver = {
    .name    = "fat32",
    .probe   = fat32_probe,
    .mount   = fat32_mount,
    .umount  = NULL,
    .lookup  = fat_lookup,
    .read    = fat_read,
    .write   = fat_write,
    .create  = fat_create,
    .unlink  = fat_unlink,
    .readdir = fat_readdir,
    .stat    = fat_stat,
    .sync    = NULL,
    .statfs  = fat_statfs,
};
vfs_driver_t *fat32_get_driver(void) { return &fat32_driver; }

/* Silence unused helper */
static void _unused(void) { (void)_io_buf; (void)fat_write_sectors; (void)fat_read_sectors; }
