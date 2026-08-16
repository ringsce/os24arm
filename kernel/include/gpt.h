#ifndef GPT_H
#define GPT_H

#include "types.h"
#include "blkdev.h"

/**
 * @brief Probe a disk for a GPT partition table and register any
 * partitions found as child block devices (name = the partition's GPT
 * name, e.g. "BOOT" or "C:"). No-op if no valid protective MBR / GPT
 * header is present.
 */
void gpt_probe(blkdev_t *disk);

#endif /* GPT_H */
