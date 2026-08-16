/* exfat.h – exFAT on-disk structures & driver interface */
#pragma once
#include "../../include/types.h"
#include "../../include/vfs.h"

/* ── Boot sector ─────────────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  jmp_boot[3];
    uint8_t  oem_name[8];        /* "EXFAT   "                     */
    uint8_t  must_be_zero[53];
    uint64_t partition_offset;
    uint64_t volume_length;      /* in sectors                      */
    uint32_t fat_offset;         /* in sectors, from partition start*/
    uint32_t fat_length;         /* in sectors                      */
    uint32_t cluster_heap_offset;/* in sectors                      */
    uint32_t cluster_count;
    uint32_t root_dir_cluster;
    uint32_t volume_serial;
    uint16_t fs_revision;        /* 0x0100 = exFAT 1.0             */
    uint16_t volume_flags;
    uint8_t  bytes_per_sector_shift;    /* 512 = shift of 9         */
    uint8_t  sectors_per_cluster_shift; /* 1..25                    */
    uint8_t  num_fats;           /* 1 or 2                          */
    uint8_t  drive_select;
    uint8_t  percent_in_use;
    uint8_t  reserved[7];
    uint8_t  boot_code[390];
    uint16_t boot_signature;     /* 0xAA55                          */
} exfat_boot_t;

/* ── Directory entry types ───────────────────────────────────── */
#define EXFAT_ET_ALLOC_BITMAP   0x81
#define EXFAT_ET_UPCASE_TABLE   0x82
#define EXFAT_ET_VOLUME_LABEL   0x83
#define EXFAT_ET_FILE           0x85
#define EXFAT_ET_STREAM_EXT     0xC0
#define EXFAT_ET_FILE_NAME      0xC1

/* Generic entry (32 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t data[31];
} exfat_entry_t;

/* File entry (type=0x85) */
typedef struct __attribute__((packed)) {
    uint8_t  type;               /* 0x85                            */
    uint8_t  secondary_count;    /* number of secondary entries     */
    uint16_t set_checksum;
    uint16_t file_attributes;
    uint16_t reserved1;
    uint32_t create_time;
    uint32_t modified_time;
    uint32_t access_time;
    uint8_t  create_time_10ms;
    uint8_t  modified_time_10ms;
    uint8_t  create_utc_offset;
    uint8_t  modified_utc_offset;
    uint8_t  access_utc_offset;
    uint8_t  reserved2[7];
} exfat_file_entry_t;

#define EXFAT_FA_READ_ONLY  0x0001
#define EXFAT_FA_HIDDEN     0x0002
#define EXFAT_FA_SYSTEM     0x0004
#define EXFAT_FA_DIR        0x0010
#define EXFAT_FA_ARCHIVE    0x0020

/* Stream extension (type=0xC0) */
typedef struct __attribute__((packed)) {
    uint8_t  type;               /* 0xC0                            */
    uint8_t  general_flags;
    uint8_t  reserved1;
    uint8_t  name_length;        /* characters in filename (UTF-16) */
    uint16_t name_hash;
    uint16_t reserved2;
    uint64_t valid_data_length;
    uint32_t reserved3;
    uint32_t first_cluster;
    uint64_t data_length;
} exfat_stream_ext_t;

/* Filename entry (type=0xC1) – up to 15 UTF-16 chars per entry */
typedef struct __attribute__((packed)) {
    uint8_t  type;               /* 0xC1                            */
    uint8_t  general_flags;
    uint16_t name[15];           /* UTF-16LE                        */
} exfat_name_entry_t;

/* FAT values */
#define EXFAT_CLUSTER_FREE  0x00000000UL
#define EXFAT_CLUSTER_BAD   0xFFFFFFF7UL
#define EXFAT_CLUSTER_EOC   0xFFFFFFFFUL

/* ── Driver private data ─────────────────────────────────────── */
typedef struct {
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t fat_offset;         /* sectors from partition start    */
    uint32_t cluster_heap_offset;
    uint32_t cluster_count;
    uint32_t root_dir_cluster;
    uint64_t part_lba;
} exfat_fs_t;

static inline uint64_t exfat_cluster_lba(exfat_fs_t *fs, uint32_t cluster)
{
    return fs->part_lba + fs->cluster_heap_offset
           + (uint64_t)(cluster - 2) * fs->sectors_per_cluster;
}

vfs_driver_t *exfat_get_driver(void);
