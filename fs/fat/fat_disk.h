#ifndef FAT_DISK_H
#define FAT_DISK_H

/* ============================================================================
 * kernel/fs/fat/fat_disk.h  —  FAT12/FAT16/FAT32 on-disk structures
 *
 * All structures are __attribute__((packed)) to match exact disk layout.
 * Reference: Microsoft FAT Specification (December 2005)
 * ========================================================================== */

#include "types.h"

/* ── BIOS Parameter Block — common prefix for FAT12/16/32 ───────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  jmp_boot[3];         /* EB xx 90 */
    char     oem_name[8];         /* "MSDOS5.0" / "OS2WARP " */
    uint16_t bytes_per_sector;    /* always 512 */
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sector_count; /* includes BPB sector */
    uint8_t  num_fats;            /* always 2 */
    uint16_t root_entry_count;    /* 0 for FAT32, 512 for FAT16 */
    uint16_t total_sectors_16;    /* 0 if > 65535 */
    uint8_t  media_type;          /* 0xF8 = fixed disk */
    uint16_t fat_size_16;         /* 0 for FAT32 */
    uint16_t sectors_per_track;   /* 63 */
    uint16_t num_heads;           /* 255 */
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} fat_bpb_common_t;

/* ── FAT16 extended BPB ──────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    fat_bpb_common_t bpb;
    /* Extended boot record (FAT12/16 only) */
    uint8_t  drive_number;        /* 0x80 */
    uint8_t  reserved1;           /* 0x00 */
    uint8_t  boot_sig;            /* 0x29 */
    uint32_t volume_id;
    char     volume_label[11];    /* padded with spaces */
    char     fs_type[8];          /* "FAT16   " or "FAT12   " */
    uint8_t  boot_code[448];
    uint16_t boot_sector_sig;     /* 0xAA55 */
} fat16_boot_t;

/* ── FAT32 extended BPB ──────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    fat_bpb_common_t bpb;
    /* FAT32-specific */
    uint32_t fat_size_32;         /* sectors per FAT */
    uint16_t ext_flags;           /* bit 7: active FAT; bits 0-3: FAT# */
    uint16_t fs_version;          /* 0x0000 */
    uint32_t root_cluster;        /* first cluster of root dir (usually 2) */
    uint16_t fs_info;             /* sector number of FSInfo struct */
    uint16_t backup_boot_sector;  /* usually 6 */
    uint8_t  reserved[12];
    /* Extended boot record (same as FAT16) */
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;            /* 0x29 */
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];          /* "FAT32   " */
    uint8_t  boot_code[420];
    uint16_t boot_sector_sig;     /* 0xAA55 */
} fat32_boot_t;

/* ── FSInfo sector (FAT32 only, usually sector 1) ────────────────────────── */

typedef struct __attribute__((packed)) {
    uint32_t lead_sig;            /* 0x41615252 */
    uint8_t  reserved1[480];
    uint32_t struc_sig;           /* 0x61417272 */
    uint32_t free_count;          /* 0xFFFFFFFF = unknown */
    uint32_t nxt_free;            /* 0xFFFFFFFF = unknown */
    uint8_t  reserved2[12];
    uint32_t trail_sig;           /* 0xAA550000 */
} fat32_fsinfo_t;

/* ── Directory entry (32 bytes, shared FAT12/16/32) ─────────────────────── */

#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LONG_NAME  0x0F   /* RO|HIDDEN|SYSTEM|VOLUME_ID */

typedef struct __attribute__((packed)) {
    char     name[8];             /* 8.3 name; 0xE5 = deleted, 0x00 = end */
    char     ext[3];
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;         /* always 0 for FAT16 */
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} fat_dirent_t;

/* ── Long filename entry ─────────────────────────────────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  ord;                 /* sequence number; 0x40 | n for last     */
    uint16_t name1[5];            /* UTF-16LE characters 1-5                */
    uint8_t  attr;                /* FAT_ATTR_LONG_NAME                     */
    uint8_t  type;                /* 0x00                                   */
    uint8_t  chksum;              /* checksum of short name                 */
    uint16_t name2[6];            /* UTF-16LE characters 6-11               */
    uint16_t fst_clus_lo;         /* must be 0                              */
    uint16_t name3[2];            /* UTF-16LE characters 12-13              */
} fat_lfn_t;

/* ── FAT entry sentinel values ───────────────────────────────────────────── */
#define FAT12_EOC     0x0FF8u
#define FAT16_EOC     0xFFF8u
#define FAT16_FREE    0x0000u
#define FAT16_BAD     0xFFF7u
#define FAT32_EOC     0x0FFFFFF8u
#define FAT32_FREE    0x00000000u
#define FAT32_BAD     0x0FFFFFF7u
#define FAT32_MASK    0x0FFFFFFFu

/* ── Date/time helpers ───────────────────────────────────────────────────── */

/* DOS date: bits 15-9=year-1980, 8-5=month, 4-0=day */
#define FAT_DATE(y,m,d)  (uint16_t)(((uint16_t)((y)-1980)<<9)|((uint16_t)(m)<<5)|(uint16_t)(d))
/* DOS time: bits 15-11=hour, 10-5=min, 4-0=sec/2 */
#define FAT_TIME(h,mi,s) (uint16_t)(((uint16_t)(h)<<11)|((uint16_t)(mi)<<5)|((uint16_t)(s)>>1))

#define FAT_DEFAULT_DATE FAT_DATE(2026,2,15)
#define FAT_DEFAULT_TIME FAT_TIME(0,0,0)

#endif /* FAT_DISK_H */
