#include "ui.h"
#include <string.h>
#include <assert.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sfes_display.h"
#include "snes9x.h"
#include "rlog.h"
#include "music.h"
#include "font8x8.h"

uint16_t *ui_fb;
static uint16_t cube[180];

void ui_init(void)
{
    ui_fb = (uint16_t *)GFX.Screen;
    assert(ui_fb && GFX.Pitch == FB_PITCH * 2);
    for (int i = 0; i < 180; i++) cube[i] = CUBE(i / 30, (i / 5) % 6, i % 5);
}

void ui_clear(uint16_t colour) { ui_fill(0, 0, FB_PITCH, FB_LINES, colour); }

void ui_fill(int x, int y, int w, int h, uint16_t colour)
{
    if (x < 0) { w += x; x = 0; }
    if (x + w > FB_PITCH) w = FB_PITCH - x;
    if (w <= 0) return;
    for (int r = y < 0 ? 0 : y; r < y + h && r < FB_LINES; r++) {
        uint16_t *p = ui_fb + r * FB_PITCH + x;
        for (int i = 0; i < w; i++) p[i] = colour;
    }
}

void ui_text(int x, int y, const char *s, uint16_t colour)
{
    for (; *s; s++, x += 8) {
        if (*s < 32 || *s > 126 || x < 0 || x > FB_PITCH - 8 || y < 0 || y > FB_LINES - 8) continue;
        const uint8_t *g = font8x8[*s - 32];
        uint16_t *row = ui_fb + y * FB_PITCH + x;
        for (int r = 0; r < 8; r++, row += FB_PITCH)
            for (int c = 0; c < 8; c++)
                if (g[r] & (0x80 >> c)) row[c] = colour;
    }
}

void ui_text_center(int y, const char *s, uint16_t colour)
{
    ui_text((FB_PITCH - 8 * (int)strlen(s)) / 2, y, s, colour);
}

void ui_text_scaled(int x, int y, const char *s, uint16_t colour, int scale)
{
    for (; *s; s++, x += 8 * scale) {
        if (*s < 32 || *s > 126) continue;
        const uint8_t *g = font8x8[*s - 32];
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                if (g[r] & (0x80 >> c)) ui_fill(x + c * scale, y + r * scale, scale, scale, colour);
    }
}

void ui_present(void)
{
    while (render_busy) vTaskDelay(1);   /* the render core owns the panel while a frame is in flight */
    display_push_rgb565(ui_fb, FB_PITCH * 2);
}

void ui_tick(bool present)
{
    if (present) ui_present();
    if (music_tick()) return;   /* the DAC paced us */
    static int64_t next;
    int64_t now = esp_timer_get_time();
    if (next == 0 || now - next > 100000) next = now;
    next += 16667;
    if (next - now > 2000) vTaskDelay((next - now - 1000) / 1000);
    while (esp_timer_get_time() < next) ;
}

void ui_bitmap(int x, int y, const uint8_t *px, int w, int h, int num, int den)
{
    int ow = w * num / den, oh = h * num / den;
    int ox0 = x < 0 ? -x : 0, ox1 = x + ow > FB_PITCH ? FB_PITCH - x : ow;
    if (ox1 <= ox0) return;
    static uint16_t xmap[512];
    for (int ox = ox0; ox < ox1; ox++) xmap[ox] = ox * den / num;
    for (int oy = 0; oy < oh; oy++) {
        int sy = y + oy;
        if (sy < 0 || sy >= FB_LINES) continue;
        const uint8_t *row = px + (oy * den / num) * w;
        uint16_t *dst = ui_fb + sy * FB_PITCH + x;
        for (int ox = ox0; ox < ox1; ox++) dst[ox] = cube[row[xmap[ox]]];
    }
}

void ui_frame(int x, int y, int w, int h, uint16_t colour)
{
    ui_fill(x, y, w, 1, colour); ui_fill(x, y + h - 1, w, 1, colour);
    ui_fill(x, y, 1, h, colour); ui_fill(x + w - 1, y, 1, h, colour);
}
