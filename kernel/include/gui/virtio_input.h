/**
 * @file virtio_input.h
 * @brief Minimal virtio-input driver: keyboard (Cmd+Escape workplace
 * hotkey) and tablet (visible mouse cursor)
 *
 * All shell/DOS input still goes through the UART console as before -
 * this is a SEPARATE input source specifically for real events from the
 * QEMU display window's own keyboard/pointer focus (not the serial
 * console). Requires the VM to be started with
 * "-device virtio-keyboard-device -device virtio-tablet-device" (see the
 * run-gui CMake target); each safely does nothing if its device isn't
 * attached.
 *
 * virtio-tablet reports absolute pointer position (unlike virtio-mouse's
 * relative deltas), which maps directly onto screen coordinates with no
 * accumulator/clamping needed - much simpler for a GUI cursor.
 */

#ifndef GUI_VIRTIO_INPUT_H
#define GUI_VIRTIO_INPUT_H

#include "types.h"

/* Probe the QEMU "virt" machine's virtio-mmio slots for virtio-input
 * devices and bring up whichever are found (keyboard and/or tablet,
 * distinguished by querying each device's supported event types - see
 * virtio_input.c). Safe to call with either, both, or neither attached. */
void vinput_init(void);

/* Non-blocking: drain and process any pending keyboard events, tracking
 * Left/Right Meta (Cmd on a Mac keyboard, via QEMU's Cocoa display)
 * modifier state internally. Returns true exactly once, on the instant
 * Escape is pressed while Meta is held down. Call every iteration of the
 * main loop; never blocks, safely does nothing if no keyboard was found. */
bool vinput_poll_workplace_hotkey(void);

/* Non-blocking: drain and process any pending tablet events, forwarding
 * position/button changes to wl_seat_pointer_motion()/
 * wl_seat_pointer_button() (see gui/wayland.h) so the compositor can draw
 * a real cursor and route clicks/drags. Call every iteration of the main
 * loop; never blocks, safely does nothing if no tablet was found. Returns
 * true if anything changed (so the caller knows to re-flush the
 * compositor - the cursor otherwise only moves on the next unrelated
 * redraw). */
bool vinput_poll_pointer(void);

#endif /* GUI_VIRTIO_INPUT_H */
