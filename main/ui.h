/* ui.h - text screens drawn into the RGB565 framebuffer the renderer also uses (GFX.Screen). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define FB_PITCH  256     /* pixels per row of GFX.Screen (GFX.Pitch is 512 bytes) */
#define FB_LINES  224

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define UI_BLACK  RGB565(0, 0, 0)
#define UI_WHITE  RGB565(255, 255, 255)
#define UI_GREY   RGB565(128, 128, 128)
#define UI_YELLOW RGB565(255, 220, 0)
#define UI_GREEN  RGB565(40, 220, 40)
#define UI_RED    RGB565(240, 40, 40)
#define UI_BLUE   RGB565(40, 80, 220)
/* the 6x6x5 colour cube the box art is quantised to (r, g in 0..5, b in 0..4) */
#define CUBE(r, g, b) RGB565((r) * 51, (g) * 51, (b) * 63)

extern uint16_t *ui_fb;

void ui_init(void);            /* framebuffer = GFX.Screen; call after S9xInitDisplay */
void ui_clear(uint16_t colour);
void ui_fill(int x, int y, int w, int h, uint16_t colour);
void ui_text(int x, int y, const char *s, uint16_t colour);
void ui_text_center(int y, const char *s, uint16_t colour);
void ui_text_scaled(int x, int y, const char *s, uint16_t colour, int scale);   /* 8*scale px glyphs, clipped */
void ui_present(void);         /* push the framebuffer to the panel (waits for the render core first) */
void ui_tick(bool present);    /* one frame of a menu screen: music if playing, present if asked, paced to 60 Hz */
/* blit an 8-bit cube-indexed bitmap (from flash) scaled by num/den, nearest neighbour, clipped */
void ui_bitmap(int x, int y, const uint8_t *px, int w, int h, int num, int den);
void ui_frame(int x, int y, int w, int h, uint16_t colour);   /* 1 px rectangle outline */
static inline void ui_px(int x, int y, uint16_t c) { if (x >= 0 && x < FB_PITCH && y >= 0 && y < FB_LINES) ui_fb[y * FB_PITCH + x] = c; }
