/* SFES: emulator-side half of the PPU render log. Replaces S9xStartScreenRefresh, RenderLine,
 * FLUSH_REDRAW and S9xEndScreenRefresh on the emulator core with snapshots into a log that the
 * render core draws from (render.c). Two logs: one being written, one being drawn. */
#include <string.h>
#include "snes9x.h"
#include "rlog.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uint8_t sfes_vram_dirty[4096];
uint32_t rlog_frames, rlog_dropped, rlog_bands;
static rlog_frame_t *logs[2];
static int cur;
static bool sticky;   /* a dropped frame's "changed" flags must reach the next frame that is drawn */

void rlog_emu_state(uint8_t **tc, uint8_t **tcd, int32_t *fps)
{
    *tc = IPPU.TileCache; *tcd = IPPU.TileCached; *fps = Memory.ROMFramesPerSecond;
}

void rlog_init(void)
{
    logs[0] = heap_caps_calloc(1, sizeof(rlog_frame_t), MALLOC_CAP_SPIRAM);
    logs[1] = heap_caps_calloc(1, sizeof(rlog_frame_t), MALLOC_CAP_SPIRAM);
    assert(logs[0] && logs[1]);
    memset(sfes_vram_dirty, 1, sizeof sfes_vram_dirty);   /* first hand-off copies all of VRAM */
    sticky = true;
}

/* S9xStartScreenRefresh minus the GFX pitch setup, which the render side redoes from the snapshot */
void rlog_start_frame(void)
{
    if (IPPU.RenderThisFrame) {
        IPPU.PreviousLine = IPPU.CurrentLine = 0;
        if (PPU.BGMode == 5 || PPU.BGMode == 6)
            IPPU.Interlace = (Memory.FillRAM[0x2133] & 1);
        if (PPU.BGMode == 5 || PPU.BGMode == 6 || IPPU.Interlace) {
            IPPU.RenderedScreenWidth = 512;
            IPPU.DoubleWidthPixels = true;
            IPPU.HalfWidthPixels = false;
            IPPU.RenderedScreenHeight = IPPU.Interlace ? PPU.ScreenHeight << 1 : PPU.ScreenHeight;
            IPPU.DoubleHeightPixels = IPPU.Interlace;
        } else {
            IPPU.RenderedScreenWidth = 256;
            IPPU.RenderedScreenHeight = PPU.ScreenHeight;
            IPPU.DoubleWidthPixels = false;
            IPPU.HalfWidthPixels = false;
            IPPU.DoubleHeightPixels = false;
        }
        PPU.RecomputeClipWindows = true;
        logs[cur]->nbands = 0;
        if (sticky) { IPPU.OBJChanged = true; IPPU.ColorsChanged = true; sticky = false; }
    }
    if (++IPPU.FrameCount == (uint32_t)Memory.ROMFramesPerSecond)
        IPPU.FrameCount = 0;
}

/* RenderLine: the per-line scroll and Mode 7 matrix capture, into the log.
 * ponytail: the not-rendering branch of RenderLine kept PPU.RangeTimeOver (sprite overflow flags)
 * current; nothing does now. Add an emulator-side S9xSetupOBJ if a game is found to poll $213E. */
void rlog_render_line(uint8_t C)
{
    if (!IPPU.RenderThisFrame) return;
    rlog_frame_t *f = logs[cur];
    SLineData *ld = &f->line[C];
    ld->BG[0].VOffset = PPU.BG[0].VOffset + 1;
    ld->BG[0].HOffset = PPU.BG[0].HOffset;
    ld->BG[1].VOffset = PPU.BG[1].VOffset + 1;
    ld->BG[1].HOffset = PPU.BG[1].HOffset;
    if (PPU.BGMode == 7) {
        SLineMatrixData *p = &f->matrix[C];
        p->MatrixA = PPU.MatrixA; p->MatrixB = PPU.MatrixB;
        p->MatrixC = PPU.MatrixC; p->MatrixD = PPU.MatrixD;
        p->CentreX = PPU.CentreX; p->CentreY = PPU.CentreY;
    } else if (Settings.StarfoxHack && PPU.BG[2].VOffset == 0 && PPU.BG[2].HOffset == 0xe000) {
        ld->BG[2].VOffset = 0xe1;
        ld->BG[2].HOffset = 0;
    } else {
        ld->BG[2].VOffset = PPU.BG[2].VOffset + 1;
        ld->BG[2].HOffset = PPU.BG[2].HOffset;
        ld->BG[3].VOffset = PPU.BG[3].VOffset + 1;
        ld->BG[3].HOffset = PPU.BG[3].HOffset;
    }
    IPPU.CurrentLine = C + 1;
}

/* FLUSH_REDRAW: snapshot the state for the lines emulated since the last band */
void rlog_flush(void)
{
    if (IPPU.PreviousLine == IPPU.CurrentLine) return;
    rlog_frame_t *f = logs[cur];
    if (f->nbands == RLOG_MAX_BANDS) {
        /* ponytail: log full; the last band just grows, so its lines render with slightly stale state */
        f->band[RLOG_MAX_BANDS - 1].end = IPPU.CurrentLine;
    } else {
        rlog_band_t *b = &f->band[f->nbands++];
        b->start = IPPU.PreviousLine;
        b->end = IPPU.CurrentLine;
        memcpy(&b->ppu, &PPU, sizeof PPU);
        memcpy(&b->ippu, &IPPU, sizeof IPPU);
        memcpy(b->regs, Memory.FillRAM + 0x2100, sizeof b->regs);
        memcpy(b->colors, IPPU.ScreenColors, sizeof b->colors);
        rlog_bands++;
    }
    IPPU.PreviousLine = IPPU.CurrentLine;
    /* delivered: the renderer acts on these from the snapshot */
    IPPU.OBJChanged = false;
    IPPU.ColorsChanged = false;
    PPU.RecomputeClipWindows = false;
}

/* S9xEndScreenRefresh: hand the frame to the render core, or drop it if the core is still busy */
void rlog_end_frame(void)
{
    if (IPPU.RenderThisFrame) {
        rlog_flush();
        if (render_busy) {
            rlog_dropped++;
            sticky = true;
        } else {
            /* VRAM blocks written since the last hand-off, and their tile cache entries */
            const uint32_t *dirty = (const uint32_t *)sfes_vram_dirty;
            for (int w = 0; w < 4096 / 4; w++) {
                if (!dirty[w]) continue;
                for (int i = w * 4; i < w * 4 + 4; i++) {
                    if (!sfes_vram_dirty[i]) continue;
                    sfes_vram_dirty[i] = 0;
                    memcpy(render_vram + i * 16, Memory.VRAM + i * 16, 16);
                    render_tile_invalidate(i);
                }
            }
            render_pending = logs[cur];
            render_busy = 1;
            xTaskNotifyGive((TaskHandle_t)render_task_handle);
            cur ^= 1;
            rlog_frames++;
        }
    }
    /* CPU.SRAMModified is left for the app: it flushes battery RAM when it sees it and clears it */
}

void rlog_reset(void)
{
    memset(sfes_vram_dirty, 1, sizeof sfes_vram_dirty);
    sticky = true;
}
