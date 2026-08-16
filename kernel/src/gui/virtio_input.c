/**
 * @file virtio_input.c
 * @brief Minimal virtio-input driver — see gui/virtio_input.h
 *
 * Register offsets, status/feature bit values, and the split-virtqueue
 * (descriptor/avail/used ring) layout are cross-checked against QEMU's
 * own standard-headers (linux/virtio_mmio.h, virtio_config.h,
 * virtio_ring.h, virtio_input.h) and hw/virtio/virtio-mmio.c /
 * hw/input/virtio-input-hid.c - same rigor as gui/ramfb.c, since a wrong
 * offset here silently does nothing rather than erroring cleanly.
 *
 * virtio-keyboard and virtio-tablet are both VIRTIO_ID_INPUT (18) - the
 * only way to tell them apart is by querying each device's config space
 * for which event types it supports (VIRTIO_INPUT_CFG_EV_BITS): a
 * keyboard has no EV_ABS axes, a tablet does. Unlike the fw_cfg DMA
 * protocol, virtio device-config-space (offset 0x100+) is plain
 * little-endian MMIO with no barrier/generation protocol required for a
 * simple synchronous select-then-read (confirmed against QEMU's own
 * virtio-mmio.c) - much simpler than the eventq itself.
 *
 * No interrupts (matching this project's existing MMIO drivers, e.g.
 * drivers/video/virtiofb.c) - events are drained by polling the used ring.
 */

#include "gui/virtio_input.h"
#include "gui/wayland.h"
#include "uart.h"
#include "kprintf.h"
#include "string.h"

/* types.h leaves uint64_t undefined under this kernel's -nostdinc build
 * (see fs/btrfs/btrfs_disk.h / kernel/src/lx_loader.c / gui/ramfb.c for
 * the same workaround); needed here for virtqueue guest-physical
 * addresses. */
#ifndef uint64_t
typedef unsigned long uint64_t;
#endif

/* QEMU "virt" machine: up to 32 virtio-mmio slots, 0x200 bytes apart. */
#define VIRTIO_MMIO_SLOT_BASE   0x0A000000UL
#define VIRTIO_MMIO_SLOT_STRIDE 0x200UL
#define VIRTIO_MMIO_SLOT_COUNT  32

#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_READY         0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW    0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH   0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW    0x0a0
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH   0x0a4
#define VIRTIO_MMIO_CONFIG              0x100

#define VIRTIO_ID_INPUT   18

#define VIRTIO_CONFIG_S_ACKNOWLEDGE  1
#define VIRTIO_CONFIG_S_DRIVER       2
#define VIRTIO_CONFIG_S_DRIVER_OK    4
#define VIRTIO_CONFIG_S_FEATURES_OK  8

#define VRING_DESC_F_WRITE  2

#define QUEUE_SIZE 16

/* virtio_input_config select values (VIRTIO_INPUT_CFG_*) */
#define VIRTIO_INPUT_CFG_EV_BITS  0x11

/* Linux evdev event types/codes (input-event-codes.h) - stable, decades-old
 * values. */
#define EV_KEY  0x01
#define EV_ABS  0x03

#define KEY_ESC       1
#define KEY_LEFTMETA  125
#define KEY_RIGHTMETA 126

#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111

#define ABS_X  0x00
#define ABS_Y  0x01

/* virtio-tablet always reports absolute position scaled to this fixed
 * range regardless of actual screen resolution (QEMU's own
 * INPUT_EVENT_ABS_MIN/MAX, include/ui/input.h) - stable across every
 * virtio-input backend QEMU has, so hardcoding it (rather than querying
 * VIRTIO_INPUT_CFG_ABS_INFO for the real min/max) is a reasonable
 * simplification here. */
#define TABLET_ABS_MIN 0
#define TABLET_ABS_MAX 0x7FFF

#pragma pack(push, 1)
typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} vring_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QUEUE_SIZE];
} vring_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} vring_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    vring_used_elem_t ring[QUEUE_SIZE];
} vring_used_t;

typedef struct {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} virtio_input_event_t;
#pragma pack(pop)

/* One instance of this per virtio-input device (keyboard, tablet). Each
 * device has its own independent MMIO base and eventq - nothing here is
 * shared between them. */
typedef struct {
    uintptr_t base;
    bool ready;
    uint16_t last_used_idx;
    vring_desc_t desc[QUEUE_SIZE] __attribute__((aligned(16)));
    vring_avail_t avail __attribute__((aligned(2)));
    vring_used_t used __attribute__((aligned(4)));
    virtio_input_event_t bufs[QUEUE_SIZE] __attribute__((aligned(8)));
} vinput_dev_t;

static vinput_dev_t g_keyboard;
static vinput_dev_t g_tablet;
static bool g_meta_down = false;

static inline uint32_t mmio_read(uintptr_t base, uint32_t off)
{
    return *(volatile uint32_t *)(base + off);
}

static inline void mmio_write(uintptr_t base, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(base + off) = val;
}

static inline uint8_t mmio_read8(uintptr_t base, uint32_t off)
{
    return *(volatile uint8_t *)(base + off);
}

static inline void mmio_write8(uintptr_t base, uint32_t off, uint8_t val)
{
    *(volatile uint8_t *)(base + off) = val;
}

/* Query whether this device advertises any EV_ABS axes: plain
 * synchronous config-space access (offset 0x100+), no DMA/barriers
 * needed (see file header). A keyboard reports size=0 (no absolute
 * axes); a tablet reports size>0. */
static bool device_has_ev_abs(uintptr_t base)
{
    mmio_write8(base, VIRTIO_MMIO_CONFIG + 0, VIRTIO_INPUT_CFG_EV_BITS); /* select */
    mmio_write8(base, VIRTIO_MMIO_CONFIG + 1, EV_ABS);                  /* subsel */
    uint8_t size = mmio_read8(base, VIRTIO_MMIO_CONFIG + 2);
    return size > 0;
}

/* Hand descriptor `idx` back to the device as an available (writable)
 * receive buffer, so the next event can be delivered into it. */
static void requeue_buffer(vinput_dev_t *dev, uint16_t idx)
{
    uint16_t slot = (uint16_t)(dev->avail.idx % QUEUE_SIZE);
    dev->avail.ring[slot] = idx;
    __asm__ volatile("dsb sy" ::: "memory");
    dev->avail.idx++;
    __asm__ volatile("dsb sy" ::: "memory");
    mmio_write(dev->base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);
}

/* Standard virtio-mmio handshake + one receive virtqueue, shared by
 * keyboard and tablet setup. */
static bool vinput_dev_setup(vinput_dev_t *dev, uintptr_t base)
{
    memset(dev, 0, sizeof(*dev));
    dev->base = base;

    mmio_write(base, VIRTIO_MMIO_STATUS, 0);
    mmio_write(base, VIRTIO_MMIO_STATUS, VIRTIO_CONFIG_S_ACKNOWLEDGE);
    mmio_write(base, VIRTIO_MMIO_STATUS, VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);

    /* Negotiate only VIRTIO_F_VERSION_1 (feature bit 32 = word 1, bit 0);
     * reject every other offered feature (in particular no EVENT_IDX, no
     * PACKED ring) to keep the split-ring layout above valid. */
    mmio_write(base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    mmio_write(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    mmio_write(base, VIRTIO_MMIO_DRIVER_FEATURES, 0);
    mmio_write(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
    mmio_write(base, VIRTIO_MMIO_DRIVER_FEATURES, 1); /* bit 0 of word 1 */

    mmio_write(base, VIRTIO_MMIO_STATUS,
               VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER | VIRTIO_CONFIG_S_FEATURES_OK);
    if (!(mmio_read(base, VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK)) {
        uart_puts("[KBD] virtio-input: feature negotiation rejected\r\n");
        return false;
    }

    mmio_write(base, VIRTIO_MMIO_QUEUE_SEL, 0);
    if (mmio_read(base, VIRTIO_MMIO_QUEUE_NUM_MAX) < QUEUE_SIZE) {
        uart_puts("[KBD] virtio-input: eventq too small\r\n");
        return false;
    }
    mmio_write(base, VIRTIO_MMIO_QUEUE_NUM, QUEUE_SIZE);

    for (int i = 0; i < QUEUE_SIZE; i++) {
        dev->desc[i].addr = (uint64_t)(uintptr_t)&dev->bufs[i];
        dev->desc[i].len = sizeof(virtio_input_event_t);
        dev->desc[i].flags = VRING_DESC_F_WRITE; /* device writes the event into it */
        dev->avail.ring[i] = (uint16_t)i;
    }
    dev->avail.idx = QUEUE_SIZE; /* every buffer available up front */
    dev->last_used_idx = 0;

    __asm__ volatile("dsb sy" ::: "memory");

    uint64_t desc_addr = (uint64_t)(uintptr_t)dev->desc;
    uint64_t avail_addr = (uint64_t)(uintptr_t)&dev->avail;
    uint64_t used_addr = (uint64_t)(uintptr_t)&dev->used;

    mmio_write(base, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc_addr);
    mmio_write(base, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));
    mmio_write(base, VIRTIO_MMIO_QUEUE_DRIVER_LOW, (uint32_t)avail_addr);
    mmio_write(base, VIRTIO_MMIO_QUEUE_DRIVER_HIGH, (uint32_t)(avail_addr >> 32));
    mmio_write(base, VIRTIO_MMIO_QUEUE_DEVICE_LOW, (uint32_t)used_addr);
    mmio_write(base, VIRTIO_MMIO_QUEUE_DEVICE_HIGH, (uint32_t)(used_addr >> 32));
    mmio_write(base, VIRTIO_MMIO_QUEUE_READY, 1);

    mmio_write(base, VIRTIO_MMIO_STATUS,
               VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER |
               VIRTIO_CONFIG_S_FEATURES_OK | VIRTIO_CONFIG_S_DRIVER_OK);

    mmio_write(base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    dev->ready = true;
    return true;
}

/* Drain at most one pending event from `dev` into *out. Returns false
 * when the used ring is empty (nothing pending right now). */
static bool vinput_dev_poll(vinput_dev_t *dev, virtio_input_event_t *out)
{
    if (!dev->ready) return false;
    if (dev->used.idx == dev->last_used_idx) return false;

    uint16_t slot = (uint16_t)(dev->last_used_idx % QUEUE_SIZE);
    uint32_t desc_id = dev->used.ring[slot].id;
    dev->last_used_idx++;

    if (desc_id >= QUEUE_SIZE) return false; /* corrupt id; drop */

    *out = dev->bufs[desc_id];
    requeue_buffer(dev, (uint16_t)desc_id);
    return true;
}

void vinput_init(void)
{
    bool kbd_found = false, tablet_found = false;

    for (int i = 0; i < VIRTIO_MMIO_SLOT_COUNT && !(kbd_found && tablet_found); i++) {
        uintptr_t base = VIRTIO_MMIO_SLOT_BASE + (uintptr_t)i * VIRTIO_MMIO_SLOT_STRIDE;
        if (mmio_read(base, VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976UL) continue; /* "virt" */
        if (mmio_read(base, VIRTIO_MMIO_VERSION) != 2) continue;
        if (mmio_read(base, VIRTIO_MMIO_DEVICE_ID) != VIRTIO_ID_INPUT) continue;

        if (device_has_ev_abs(base)) {
            if (tablet_found) continue;
            if (vinput_dev_setup(&g_tablet, base)) {
                tablet_found = true;
                kprintf("[KBD] virtio-tablet ready at %p\r\n", (void *)base);
            }
        } else {
            if (kbd_found) continue;
            if (vinput_dev_setup(&g_keyboard, base)) {
                kbd_found = true;
                kprintf("[KBD] virtio-keyboard ready at %p\r\n", (void *)base);
            }
        }
    }

    if (!kbd_found) {
        uart_puts("[KBD] No virtio-keyboard found "
                   "(boot with -device virtio-keyboard-device for Cmd+Escape)\r\n");
    }
    if (!tablet_found) {
        uart_puts("[KBD] No virtio-tablet found "
                   "(boot with -device virtio-tablet-device for a mouse cursor)\r\n");
    }
}

bool vinput_poll_workplace_hotkey(void)
{
    bool hotkey = false;
    virtio_input_event_t ev;

    while (vinput_dev_poll(&g_keyboard, &ev)) {
        if (ev.type != EV_KEY) continue;
        if (ev.code == KEY_LEFTMETA || ev.code == KEY_RIGHTMETA) {
            g_meta_down = (ev.value != 0); /* 1=press, 2=repeat, 0=release */
        } else if (ev.code == KEY_ESC && ev.value == 1 && g_meta_down) {
            hotkey = true;
        }
    }

    return hotkey;
}

bool vinput_poll_pointer(void)
{
    static int abs_x = (TABLET_ABS_MAX - TABLET_ABS_MIN) / 2;
    static int abs_y = (TABLET_ABS_MAX - TABLET_ABS_MIN) / 2;

    virtio_input_event_t ev;
    bool moved = false;
    bool changed = false;

    while (vinput_dev_poll(&g_tablet, &ev)) {
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) { abs_x = (int)ev.value; moved = true; }
            else if (ev.code == ABS_Y) { abs_y = (int)ev.value; moved = true; }
        } else if (ev.type == EV_KEY && (ev.code == BTN_LEFT || ev.code == BTN_RIGHT)) {
            int sx = fb_get_width() * (abs_x - TABLET_ABS_MIN) / (TABLET_ABS_MAX - TABLET_ABS_MIN);
            int sy = fb_get_height() * (abs_y - TABLET_ABS_MIN) / (TABLET_ABS_MAX - TABLET_ABS_MIN);
            wl_seat_pointer_button(sx, sy, (ev.code == BTN_LEFT) ? 0 : 1, ev.value != 0);
            changed = true;
        }
    }

    if (moved) {
        int sx = fb_get_width() * (abs_x - TABLET_ABS_MIN) / (TABLET_ABS_MAX - TABLET_ABS_MIN);
        int sy = fb_get_height() * (abs_y - TABLET_ABS_MIN) / (TABLET_ABS_MAX - TABLET_ABS_MIN);
        wl_seat_pointer_motion(sx, sy);
        changed = true;
    }

    return changed;
}
