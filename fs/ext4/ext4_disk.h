#ifndef EXT4_DISK_H
#define EXT4_DISK_H

/* ============================================================================
 * kernel/fs/ext4/ext4_disk.h  —  Ext4 on-disk structures
 *
 * All structures are packed and match the Linux kernel / ext4 specification
 * exactly.  See: https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
 * ========================================================================== */

#include "types.h"

/* ── Magic numbers & constants ───────────────────────────────────────────── */

#define EXT4_SUPER_MAGIC        0xEF53u
#define EXT4_EXTENT_MAGIC       0xF30Au

/* Inode flags */
#define EXT4_EXTENTS_FL         0x00080000u
#define EXT4_INLINE_DATA_FL     0x10000000u

/* Feature flags (ro_compat) */
#define EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER  0x0001u
#define EXT4_FEATURE_RO_COMPAT_LARGE_FILE    0x0002u
#define EXT4_FEATURE_RO_COMPAT_HUGE_FILE     0x0008u
#define EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE   0x0040u

/* Feature flags (incompat) */
#define EXT4_FEATURE_INCOMPAT_FILETYPE       0x0002u
#define EXT4_FEATURE_INCOMPAT_EXTENTS        0x0040u
#define EXT4_FEATURE_INCOMPAT_64BIT          0x0080u
#define EXT4_FEATURE_INCOMPAT_FLEX_BG        0x0200u

/* File types (dir entry) */
#define EXT4_FT_UNKNOWN   0
#define EXT4_FT_REG_FILE  1
#define EXT4_FT_DIR       2
#define EXT4_FT_SYMLINK   7

/* Inode modes */
#define EXT4_S_IFMT    0xF000u
#define EXT4_S_IFREG   0x8000u
#define EXT4_S_IFDIR   0x4000u
#define EXT4_DEFM_DIR  0x01EDu   /* 0755 */
#define EXT4_DEFM_REG  0x01A4u   /* 0644 */

/* Reserved inodes */
#define EXT4_ROOT_INO       2u
#define EXT4_LOST_FOUND_INO 11u
#define EXT4_FIRST_INO      11u

/* ── Superblock (at byte offset 1024 from partition start) ───────────────── */

typedef struct __attribute__((packed)) {
    uint32_t s_inodes_count;        /* Total inodes                        */
    uint32_t s_blocks_count_lo;     /* Total blocks (low 32 bits)          */
    uint32_t s_r_blocks_count_lo;   /* Reserved blocks                     */
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;    /* 0 for >1KB blocks, 1 for 1KB       */
    uint32_t s_log_block_size;      /* log2(block_size) - 10              */
    uint32_t s_log_cluster_size;    /* same as block for no bigalloc       */
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;               /* last mount time                     */
    uint32_t s_wtime;               /* last write time                     */
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;       /* 0xFFFF = no limit                   */
    uint16_t s_magic;               /* 0xEF53                              */
    uint16_t s_state;               /* 1=clean                             */
    uint16_t s_errors;              /* 1=continue                          */
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;          /* 0=Linux                             */
    uint32_t s_rev_level;           /* 1=dynamic rev                       */
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    /* EXT4_DYNAMIC_REV fields */
    uint32_t s_first_ino;           /* First non-reserved inode            */
    uint16_t s_inode_size;          /* Size of inode structure             */
    uint16_t s_block_group_nr;      /* Block group this SB is in           */
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    /* Journaling */
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;           /* Block group descriptor size          */
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint32_t s_mkfs_time;
    uint32_t s_jnl_blocks[17];
    /* 64-bit support */
    uint32_t s_blocks_count_hi;
    uint32_t s_r_blocks_count_hi;
    uint32_t s_free_blocks_count_hi;
    uint16_t s_min_extra_isize;
    uint16_t s_want_extra_isize;
    uint32_t s_flags;
    uint16_t s_raid_stride;
    uint16_t s_mmp_update_interval;
    uint64_t s_mmp_block;
    uint32_t s_raid_stripe_width;
    uint8_t  s_log_groups_per_flex;
    uint8_t  s_checksum_type;
    uint8_t  s_encryption_level;
    uint8_t  s_reserved_pad;
    uint64_t s_kbytes_written;
    uint32_t s_snapshot_inum;
    uint32_t s_snapshot_id;
    uint64_t s_snapshot_r_blocks_count;
    uint32_t s_snapshot_list;
    uint32_t s_error_count;
    uint32_t s_first_error_time;
    uint32_t s_first_error_ino;
    uint64_t s_first_error_block;
    uint8_t  s_first_error_func[32];
    uint32_t s_first_error_line;
    uint32_t s_last_error_time;
    uint32_t s_last_error_ino;
    uint32_t s_last_error_line;
    uint64_t s_last_error_block;
    uint8_t  s_last_error_func[32];
    uint8_t  s_mount_opts[64];
    uint32_t s_usr_quota_inum;
    uint32_t s_grp_quota_inum;
    uint32_t s_overhead_clusters;
    uint32_t s_backup_bgs[2];
    uint8_t  s_encrypt_algos[4];
    uint8_t  s_encrypt_pw_salt[16];
    uint32_t s_lpf_ino;
    uint32_t s_prj_quota_inum;
    uint32_t s_checksum_seed;
    uint8_t  s_wtime_hi;
    uint8_t  s_mtime_hi;
    uint8_t  s_mkfs_time_hi;
    uint8_t  s_lastcheck_hi;
    uint8_t  s_first_error_time_hi;
    uint8_t  s_last_error_time_hi;
    uint8_t  s_pad[2];
    uint16_t s_encoding;
    uint16_t s_encoding_flags;
    uint32_t s_reserved[95];
    uint32_t s_checksum;
} ext4_super_t;

/* ── Block group descriptor (64-bit capable, 64-byte form) ───────────────── */

typedef struct __attribute__((packed)) {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
    /* 64-bit extension (when s_desc_size > 32) */
    uint32_t bg_block_bitmap_hi;
    uint32_t bg_inode_bitmap_hi;
    uint32_t bg_inode_table_hi;
    uint16_t bg_free_blocks_count_hi;
    uint16_t bg_free_inodes_count_hi;
    uint16_t bg_used_dirs_count_hi;
    uint16_t bg_itable_unused_hi;
    uint32_t bg_exclude_bitmap_hi;
    uint16_t bg_block_bitmap_csum_hi;
    uint16_t bg_inode_bitmap_csum_hi;
    uint32_t bg_reserved;
} ext4_gd_t;

/* ── Inode (256-byte form) ───────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;       /* 512-byte blocks used (for !huge_file)   */
    uint32_t i_flags;
    uint32_t i_osd1;
    uint8_t  i_block[60];       /* extent tree root / direct block ptrs    */
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;       /* high 32 bits of file size               */
    uint32_t i_obso_faddr;
    uint8_t  i_osd2[12];
    uint16_t i_extra_isize;     /* 28 typically (extra bytes beyond 128)   */
    uint16_t i_checksum_hi;
    uint32_t i_ctime_extra;
    uint32_t i_mtime_extra;
    uint32_t i_atime_extra;
    uint32_t i_crtime;
    uint32_t i_crtime_extra;
    uint32_t i_version_hi;
    uint32_t i_projid;
} ext4_inode_t;

/* ── Extent tree ──────────────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint16_t eh_magic;      /* 0xF30A                                       */
    uint16_t eh_entries;    /* valid entries following header               */
    uint16_t eh_max;        /* capacity of this node (4 for inline root)    */
    uint16_t eh_depth;      /* 0 = leaf node, >0 = index node              */
    uint32_t eh_generation;
} ext4_extent_header_t;

/* Leaf extent — maps logical blocks [ee_block, ee_block+ee_len) */
typedef struct __attribute__((packed)) {
    uint32_t ee_block;      /* first logical block                          */
    uint16_t ee_len;        /* number of blocks (≤32768; >32768 = unwritten)*/
    uint16_t ee_start_hi;   /* physical block hi                            */
    uint32_t ee_start_lo;   /* physical block lo                            */
} ext4_extent_t;

/* Index node entry */
typedef struct __attribute__((packed)) {
    uint32_t ei_block;      /* first logical block covered                  */
    uint32_t ei_leaf_lo;    /* physical block of child extent node lo       */
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
} ext4_extent_idx_t;

/* ── Directory entry (htree-compatible linear format) ────────────────────── */

typedef struct __attribute__((packed)) {
    uint32_t inode;         /* inode number; 0 = unused                     */
    uint16_t rec_len;       /* total length of this entry                   */
    uint8_t  name_len;      /* length of file name                          */
    uint8_t  file_type;     /* EXT4_FT_*                                    */
    char     name[];        /* file name (NOT null-terminated on disk)      */
} ext4_dirent_t;

/* Minimum dir entry size (aligned to 4 bytes) */
#define EXT4_DIR_REC_LEN(name_len)  \
    (((sizeof(ext4_dirent_t) + (name_len) + 3u) & ~3u))

#endif /* EXT4_DISK_H */
