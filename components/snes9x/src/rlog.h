/* SFES: PPU render log. The emulator core records, per band of scanlines, everything the
 * renderer reads; the render core replays it. See rlog.c (emulator side) and render.c. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ppu.h"
#include "gfx.h"

#define RLOG_MAX_BANDS 32

typedef struct {
    int32_t start, end;        /* scanlines [start, end) rendered with this state */
    SPPU ppu;
    InternalPPU ippu;
    uint8_t regs[0x40];        /* Memory.FillRAM[0x2100..0x213F] */
    uint16_t colors[256];      /* IPPU.ScreenColors */
} rlog_band_t;

typedef struct {
    int nbands;
    rlog_band_t band[RLOG_MAX_BANDS];
    SLineData line[240];
    SLineMatrixData matrix[240];
} rlog_frame_t;

/* emulator side (cpuexec.c hooks) */
void rlog_init(void);
void rlog_reset(void);   /* after a ROM load or state load: resend all of VRAM and the changed flags */
void rlog_start_frame(void);
void rlog_render_line(uint8_t line);
void rlog_flush(void);
void rlog_end_frame(void);
extern uint32_t rlog_frames, rlog_dropped, rlog_bands;
void rlog_emu_state(uint8_t **tilecache, uint8_t **tilecached, int32_t *fps);   /* what the render side borrows from the emulator side */   /* handed over, dropped (render core busy), bands logged */

/* render side */
void render_init(void);
void render_task(void *arg);
void render_tile_invalidate(int block);   /* 16-byte VRAM block changed; called by the emulator side while the renderer is idle */
extern uint8_t *render_vram;
extern volatile int render_busy;
extern rlog_frame_t *render_pending;
extern void *render_task_handle;
extern uint32_t render_us;                /* accumulated render+push time on core 0 */
