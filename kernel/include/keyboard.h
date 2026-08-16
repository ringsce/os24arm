#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "os2.h"

/* Special key codes returned by kbd_getc() above 0x7F */
#define KEY_UP        0x80
#define KEY_DOWN      0x81
#define KEY_LEFT      0x82
#define KEY_RIGHT     0x83
#define KEY_HOME      0x84
#define KEY_END       0x85
#define KEY_PGUP      0x86
#define KEY_PGDN      0x87
#define KEY_DEL       0x88
#define KEY_F1        0x91
#define KEY_F2        0x92
#define KEY_F3        0x93
#define KEY_F4        0x94
#define KEY_F5        0x95
#define KEY_F6        0x96
#define KEY_F7        0x97
#define KEY_F8        0x98
#define KEY_F9        0x99
#define KEY_F10       0x9A
#define KEY_F11       0x9B
#define KEY_F12       0x9C
#define KEY_NONE      0xFF   /* no key ready (non-blocking) */

/* ASCII aliases */
#define KEY_ENTER     '\r'
#define KEY_BACKSPACE '\x7f'
#define KEY_TAB       '\t'
#define KEY_ESC       '\x1b'
#define KEY_CTRL_C    '\x03'
#define KEY_CTRL_D    '\x04'
#define KEY_CTRL_L    '\x0C'

void  kbd_init(void);
ULONG kbd_getc(void);
ULONG kbd_getc_nb(void);
BOOL  kbd_ready(void);

#endif /* KEYBOARD_H */
