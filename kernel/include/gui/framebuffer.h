/**
 * @file framebuffer.h
 * @brief Framebuffer driver for bare-metal GUI
 */

#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "types.h"

#define FB_WIDTH    1024
#define FB_HEIGHT   768
#define FB_BPP      32

typedef uint32_t fb_color_t;

#define FB_COLOR(r, g, b) (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))

/* OS/2 colors */
#define WPS_DESKTOP_BG      FB_COLOR(0, 85, 127)
#define WPS_WINDOW_BG       FB_COLOR(192, 192, 192)
#define WPS_TITLEBAR_ACTIVE FB_COLOR(0, 0, 128)
#define WPS_TEXT_WHITE      FB_COLOR(255, 255, 255)
#define WPS_TEXT_BLACK      FB_COLOR(0, 0, 0)

typedef struct { int x, y, width, height; } fb_rect_t;
typedef struct { int x, y; } fb_point_t;

void fb_init(void);
void fb_clear(fb_color_t color);
void fb_putpixel(int x, int y, fb_color_t color);
void fb_fillrect(const fb_rect_t *rect, fb_color_t color);
void fb_drawstring(int x, int y, const char *str, fb_color_t fg, fb_color_t bg);
const uint8_t *fb_get_glyph(char c);
void fb_draw_button(const fb_rect_t *rect, const char *text, bool pressed);
void fb_draw_titlebar(const fb_rect_t *rect, const char *title, bool active);
int fb_get_width(void);
int fb_get_height(void);

/* Copy the back buffer (everything fb_set_pixel/fb_fill_rect/etc. draw
 * into) to the front buffer. Only the front buffer is ever actually
 * visible - to a real display via ramfb (see gui/ramfb.h), and this was
 * never being called anywhere, so nothing drawn ever reached it. */
void fb_present(void);

#endif
