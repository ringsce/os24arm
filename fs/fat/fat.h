/* fat.h – FAT16 / FAT32 shared on-disk structures & driver interface */
#pragma once
#include "../../include/types.h"
#include "../../include/vfs.h"

/* ── BPB (BIOS Parameter Block) – common to FAT16 and FAT32 ─── */
typedef struct __attribute__((packed)) {
    uint8_t  jmp_boot[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;   /* FAT16: 512; FAT32: 0           */
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;        /* FAT16: sectors per FAT; FAT32:0*/
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} fat_bpb_t;

/* FAT16 extended BPB (after common BPB) */
typedef struct __attribute__((packed)) {
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;           /* 0x29 if following fields valid  */
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];         /* "FAT16   " or "FAT     "        */
} fat16_ebpb_t;

/* FAT32 extended BPB */
typedef struct __attribute__((packed)) {
    uint32_t fat_size_32;        /* sectors per FAT                 */
    uint16_t ext_flags;
    uint16_t fs_version;         /* 0x0000                          */
    uint32_t root_cluster;       /* usually 2                       */
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];         /* "FAT32   "                      */
} fat32_ebpb_t;

/* ── On-disk directory entry (32 bytes) ─────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  name[11];           /* 8.3 format, space-padded        */
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;        /* FAT32 high cluster word         */
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;        /* cluster of first data block     */
    uint32_t file_size;
} fat_dirent_t;

/* Long filename entry (LFN) */
typedef struct __attribute__((packed)) {
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attr;               /* 0x0F = LFN                      */
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t fst_clus;           /* always 0                        */
    uint16_t name3[2];
} fat_lfn_t;

/* Attribute flags */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIR        0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F

/* Special directory entry markers */
#define FAT_ENTRY_FREE      0xE5
#define FAT_ENTRY_END       0x00

/* ── FAT cluster chain values ─────────────────────────────────── */
#define FAT16_EOC           0xFFF8   /* end of cluster chain        */
#define FAT16_BAD           0xFFF7
#define FAT16_FREE          0x0000
#define FAT32_EOC           0x0FFFFFF8UL
#define FAT32_FREE          0x00000000UL
#define FAT32_BAD           0x0FFFFFF7UL
#define FAT32_MASK          0x0FFFFFFFUL

/* FAT type enum */
typedef enum { FS_FAT16, FS_FAT32 } fat_type_t;

/* ── Driver private data ─────────────────────────────────────── */
typedef struct {
    fat_type_t type;
    uint32_t   bytes_per_sector;
    uint32_t   sectors_per_cluster;
    uint32_t   bytes_per_cluster;
    uint32_t   reserved_sectors;
    uint32_t   num_fats;
    uint32_t   fat_size_sectors;
    uint32_t   root_entry_count;   /* FAT16: 512; FAT32: 0          */
    uint32_t   root_cluster;       /* FAT32 root cluster (= 2)      */
    uint32_t   first_data_sector;
    uint32_t   total_clusters;
    uint64_t   part_lba;           /* partition start LBA           */
    uint32_t   fat_lba_start;      /* LBA of FAT #0                 */
    uint32_t   root_dir_lba;       /* FAT16 only                    */
    uint32_t   root_dir_sectors;   /* FAT16 only                    */
} fat_fs_t;

/* Convert cluster to absolute LBA */
static inline uint64_t fat_cluster_lba(fat_fs_t *fs, uint32_t cluster)
{
    return fs->part_lba + fs->first_data_sector
           + (uint64_t)(cluster - 2) * fs->sectors_per_cluster;
}

vfs_driver_t *fat16_get_driver(void);
vfs_driver_t *fat32_get_driver(void);
