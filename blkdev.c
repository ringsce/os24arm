/* blkdev.c – Block device registry & I/O dispatcher (C11) */
#include "include/blkdev.h"
#include "include/string.h"
#include "include/terminal.h"

static blkdev_t s_devs[BLKDEV_MAX];
static uint32_t s_count;

void blkdev_init(void)
{
    kmemset(s_devs, 0, sizeof(s_devs));
    s_count = 0;
}

blkerr_t blkdev_register(const blkdev_t *dev)
{
    if (s_count >= BLKDEV_MAX) return BLKERR_NOMEM;
    s_devs[s_count] = *dev;
    s_devs[s_count].present = true;
    term_printf("[BLKDEV] Registered: %s  sectors=%llu  bs=%u%s\n",
                dev->name, dev->total_sectors, dev->sector_size,
                dev->read_only ? " [RO]" : "");
    s_count++;
    return BLKERR_OK;
}

blkdev_t *blkdev_get(const char *name)
{
    for (uint32_t i = 0; i < s_count; ++i)
        if (kstrcmp(s_devs[i].name, name) == 0) return &s_devs[i];
    return NULL;
}

blkdev_t *blkdev_get_by_index(uint32_t idx)
{
    return (idx < s_count) ? &s_devs[idx] : NULL;
}

uint32_t blkdev_count(void) { return s_count; }

blkerr_t blkdev_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    if (!dev || !dev->present)  return BLKERR_NODEV;
    if (!dev->ops.read)         return BLKERR_IO;
    if (lba + count > dev->total_sectors) return BLKERR_RANGE;
    return dev->ops.read(dev, lba, count, buf);
}

blkerr_t blkdev_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf)
{
    if (!dev || !dev->present)  return BLKERR_NODEV;
    if (dev->read_only)         return BLKERR_RDONLY;
    if (!dev->ops.write)        return BLKERR_IO;
    if (lba + count > dev->total_sectors) return BLKERR_RANGE;
    return dev->ops.write(dev, lba, count, buf);
}

blkerr_t blkdev_flush(blkdev_t *dev)
{
    if (!dev || !dev->present) return BLKERR_NODEV;
    if (dev->ops.flush) return dev->ops.flush(dev);
    return BLKERR_OK;
}

/* ── RAM disk ─────────────────────────────────────────────────── */
static blkerr_t ramdisk_read(blkdev_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    uint8_t *base = (uint8_t*)dev->priv;
    kmemcpy(buf, base + lba * 512, count * 512);
    return BLKERR_OK;
}
static blkerr_t ramdisk_write(blkdev_t *dev, uint64_t lba, uint32_t count, const void *buf)
{
    uint8_t *base = (uint8_t*)dev->priv;
    kmemcpy(base + lba * 512, buf, count * 512);
    return BLKERR_OK;
}

blkerr_t blkdev_register_ramdisk(const char *name, void *base, uint64_t size_bytes)
{
    blkdev_t dev = {0};
    kstrncpy(dev.name, name, BLKDEV_NAME_LEN - 1);
    dev.total_sectors  = size_bytes / 512;
    dev.sector_size    = 512;
    dev.block_size     = 4096;
    dev.read_only      = false;
    dev.ops.read       = ramdisk_read;
    dev.ops.write      = ramdisk_write;
    dev.priv           = base;
    return blkdev_register(&dev);
}

void blkdev_list_all(void)
{
    term_puts("  NAME            SECTORS         SECT_SZ  RO\n");
    for (uint32_t i = 0; i < s_count; ++i) {
        blkdev_t *d = &s_devs[i];
        term_printf("  %-14s  %-16llu  %-7u  %s\n",
                    d->name, d->total_sectors, d->sector_size,
                    d->read_only ? "yes" : "no");
    }
}

/* ── GPT partition probe ─────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  signature[8];   /* "EFI PART" */
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t my_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t size_of_partition_entry;
    uint32_t partition_entry_array_crc32;
} gpt_header_t;

int blkdev_probe_partitions(blkdev_t *dev, part_info_t *out, uint32_t max_parts)
{
    uint8_t sector[512];
    /* LBA 1 = GPT header */
    if (blkdev_read(dev, 1, 1, sector) != BLKERR_OK) return -1;
    gpt_header_t *hdr = (gpt_header_t*)sector;
    if (kmemcmp(hdr->signature, "EFI PART", 8) != 0) return 0;  /* no GPT */

    uint32_t n = hdr->num_partition_entries;
    if (n > max_parts) n = max_parts;
    uint32_t entry_sz = hdr->size_of_partition_entry;

    uint32_t found = 0;
    uint64_t entry_lba = hdr->partition_entry_lba;

    for (uint32_t i = 0; i < n; ++i) {
        /* Each entry is entry_sz bytes; entries start at entry_lba */
        uint64_t byte_off = (uint64_t)i * entry_sz;
        uint64_t sec_lba  = entry_lba + byte_off / 512;
        uint32_t sec_off  = (uint32_t)(byte_off % 512);

        if (blkdev_read(dev, sec_lba, 1, sector) != BLKERR_OK) break;
        gpt_entry_t *e = (gpt_entry_t*)(sector + sec_off);

        /* Check if partition GUID is all zero (empty slot) */
        bool empty = true;
        for (int b = 0; b < 16; ++b) if (e->part_guid[b]) { empty = false; break; }
        if (empty) continue;

        out[found].lba_start = e->lba_start;
        out[found].lba_end   = e->lba_end;
        out[found].flags     = e->flags;
        kmemcpy(out[found].type_guid, e->type_guid, 16);
        kmemcpy(out[found].part_guid, e->part_guid, 16);
        /* Convert UTF-16LE name to ASCII */
        for (int c = 0; c < 36 && c < 72; ++c)
            out[found].name[c] = (char)(e->name[c] & 0x7F);
        out[found].name[36] = '\0';
        found++;
    }
    return (int)found;
}
