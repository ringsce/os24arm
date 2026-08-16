/* ============================================================================
 * fs/blkdev.c — Block Device Management
 *
 * Add this file to your fs/ folder to support kernel VFS mounting
 * ========================================================================== */

#include "fs/blkdev.h"
#include "fs/virtio_blk.h"  /* Your VirtIO block driver */
#include "kio.h"
#include "string.h"

/* Block device table */
#define MAX_BLOCK_DEVICES 16
static blkdev_t g_devices[MAX_BLOCK_DEVICES];
static int g_device_count = 0;

/* Initialize block device subsystem */
void blk_init(void)
{
    kprintf("[BLK] Initializing block device subsystem...\n");
    
    /* Clear device table */
    kmemset(g_devices, 0, sizeof(g_devices));
    g_device_count = 0;
    
    /* Initialize VirtIO block device driver */
    kprintf("[BLK] Initializing VirtIO block driver...\n");
    virtio_blk_init();  /* This should detect and enumerate devices */
    
    /* VirtIO driver should have called blk_register() for each device/partition */
    
    kprintf("[BLK] Block device initialization complete (%d devices)\n", g_device_count);
    
    /* Debug: List detected devices */
    for (int i = 0; i < g_device_count; i++) {
        kprintf("[BLK]   - %s (%lu MB)\n", 
                g_devices[i].name,
                blk_bytes(&g_devices[i]) / (1024 * 1024));
    }
}

/* Register a block device (called by VirtIO driver) */
int blk_register(blkdev_t *dev)
{
    if (g_device_count >= MAX_BLOCK_DEVICES) {
        kprintf("[BLK] device table full!\n");
        return -1;
    }
    
    /* Copy device structure to table */
    g_devices[g_device_count] = *dev;
    g_device_count++;
    
    kprintf("[BLK] registered device: %s\n", dev->name);
    return 0;
}

/* Get block device by name */
blkdev_t *blk_get(const char *name)
{
    for (int i = 0; i < g_device_count; i++) {
        if (kstrcmp(g_devices[i].name, name) == 0) {
            return &g_devices[i];
        }
    }
    
    kprintf("[BLK] device not found: %s\n", name);
    return NULL;
}

/* Get device size in bytes */
uint64_t blk_bytes(blkdev_t *dev)
{
    if (!dev) return 0;
    return (uint64_t)dev->num_sectors * (uint64_t)dev->sector_size;
}

/* ============================================================================
 * Example: What your VirtIO driver should do
 * ========================================================================== */

/*
void virtio_blk_init(void)
{
    // 1. Initialize VirtIO transport
    virtio_init();
    
    // 2. Probe for VirtIO block devices
    for (int i = 0; i < MAX_VIRTIO_DEVICES; i++) {
        if (virtio_devices[i].type == VIRTIO_TYPE_BLOCK) {
            // 3. Get device info
            uint64_t num_sectors = virtio_blk_capacity(&virtio_devices[i]);
            
            // 4. Create blkdev_t structure
            blkdev_t dev;
            kmemset(&dev, 0, sizeof(dev));
            kstrncpy(dev.name, "vda", sizeof(dev.name));
            dev.num_sectors = num_sectors;
            dev.sector_size = 512;
            dev.driver_data = &virtio_devices[i];
            dev.read = virtio_blk_read;
            dev.write = virtio_blk_write;
            
            // 5. Register the device
            blk_register(&dev);
            
            // 6. Read partition table and create partition devices
            gpt_partition_t partitions[8];
            int num_parts = gpt_read_table(&dev, partitions, 8);
            
            for (int p = 0; p < num_parts; p++) {
                blkdev_t part;
                kmemset(&part, 0, sizeof(part));
                
                // Name: vda1, vda2, etc.
                char part_name[16];
                ksnprintf(part_name, sizeof(part_name), "vda%d", p + 1);
                kstrncpy(part.name, part_name, sizeof(part.name));
                
                part.start_sector = partitions[p].first_lba;
                part.num_sectors = partitions[p].last_lba - partitions[p].first_lba + 1;
                part.sector_size = 512;
                part.parent = &g_devices[g_device_count - 1];  // Point to vda
                part.driver_data = &virtio_devices[i];
                part.read = virtio_blk_read;
                part.write = virtio_blk_write;
                
                blk_register(&part);
            }
        }
    }
}
*/
