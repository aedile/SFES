/* SFES: render-side half of the PPU render log. Compiled with -DPPU=render_PPU -DIPPU=render_IPPU
 * -DMemory=render_Memory alongside gfx.c, tile.c and clip.c, so the renderer works on its own
 * copies of the PPU state, loaded from each band's snapshot, and its own VRAM copy. Runs on
 * core 0; pushes finished strips to the panel as it goes. */
#include <string.h>
#include "snes9x.h"
#include "rlog.h"
#include "sfes_display.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

SPPU PPU;                 /* render_PPU */
InternalPPU IPPU;         /* render_IPPU */
CMemory Memory;           /* render_Memory: only VRAM, FillRAM and ROMFramesPerSecond are used */

uint8_t *render_vram;
volatile int render_busy;
rlog_frame_t *render_pending;
void *render_task_handle;
uint32_t render_us;

void render_init(void)
{
    uint8_t *tc, *tcd; int32_t fps;
    rlog_emu_state(&tc, &tcd, &fps);
    render_vram = heap_caps_malloc(0x10000, MALLOC_CAP_SPIRAM);
    Memory.VRAM = render_vram;
    Memory.FillRAM = heap_caps_calloc(0x2200, 1, MALLOC_CAP_INTERNAL);
    Memory.ROMFramesPerSecond = fps;
    /* the emulator side no longer uses its tile cache; take it over */
    IPPU.TileCache = tc;
    IPPU.TileCached = tcd;
    IPPU.ScreenColors = heap_caps_calloc(256 * 9, sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    IPPU.DirectColors = IPPU.ScreenColors + 256;
    assert(render_vram && Memory.FillRAM && IPPU.ScreenColors);
    for (int p = 0; p < 8; p++)   /* constant table (S9xFixColourBrightness ignores brightness for it) */
        for (int c = 0; c < 256; c++)
            IPPU.DirectColors[p * 256 + c] = BUILD_PIXEL(((c & 7) << 2) | ((p & 1) << 1), ((c & 0x38) >> 1) | (p & 2), ((c & 0xc0) >> 3) | (p & 4));
}

void render_tile_invalidate(int i)
{
    IPPU.TileCached[i] = false;
    IPPU.TileCached[i >> 1] = false;
    IPPU.TileCached[i >> 2] = false;
}

static void load_band(const rlog_band_t *b)
{
    uint8_t *tc = IPPU.TileCache, *tcd = IPPU.TileCached;
    uint16_t *sc = IPPU.ScreenColors, *dc = IPPU.DirectColors;
    memcpy(&PPU, &b->ppu, sizeof PPU);
    memcpy(&IPPU, &b->ippu, sizeof IPPU);
    IPPU.TileCache = tc; IPPU.TileCached = tcd; IPPU.ScreenColors = sc; IPPU.DirectColors = dc;
    memcpy(IPPU.ScreenColors, b->colors, sizeof b->colors);
    memcpy(Memory.FillRAM + 0x2100, b->regs, sizeof b->regs);
}

static void render_frame(const rlog_frame_t *f)
{
    int pushed = 0;
    for (int i = 0; i < f->nbands; i++) {
        const rlog_band_t *b = &f->band[i];
        load_band(b);
        if (i == 0) {
            IPPU.RenderThisFrame = true;
            S9xStartScreenRefresh();   /* GFX pitch/pointers from this frame's mode */
            S9xSetLineData(f->line, f->matrix);
        }
        IPPU.PreviousLine = b->start;
        IPPU.CurrentLine = b->end;
        S9xUpdateScreen();
        /* rows below b->end are final: send them while the next band renders */
        for (; pushed + STRIP_ROWS <= b->end && pushed < GAME_HEIGHT; pushed += STRIP_ROWS)
            display_push_strip((const uint16_t *)GFX.Screen, GFX.Pitch, pushed);
    }
    if (f->nbands) S9xEndScreenRefresh();
    for (; pushed < GAME_HEIGHT; pushed += STRIP_ROWS)
        display_push_strip((const uint16_t *)GFX.Screen, GFX.Pitch, pushed);
}

void render_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int64_t t0 = esp_timer_get_time();
        render_frame(render_pending);
        render_us += esp_timer_get_time() - t0;
        render_busy = 0;
    }
}
