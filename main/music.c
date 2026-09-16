/* music.c - menu music: an SPC file played by the emulated SPC700 and DSP with the main CPU idle.
 * An SPC is a dump of the sound chip's 64 KB RAM, its registers and the DSP registers, taken while
 * a game was playing a track; loading it and running the sound CPU replays the track exactly. */
#include "music.h"
#include <string.h>
#include "esp_log.h"
#include "audio.h"
#include "snes9x.h"
#include "spc700.h"

static const char *TAG = "MUSIC";
static bool playing, game_live;
static int16_t pcm[(32000 / 60) * 2];

#ifdef HAVE_MENU_SPC
extern const uint8_t spc_start[] asm("_binary_menu_spc_start");
extern const uint8_t spc_end[]   asm("_binary_menu_spc_end");
#endif

void music_game_live(bool live) { game_live = live; }

void music_start(int track)
{
#ifdef HAVE_MENU_SPC
    (void)track;
    if (playing || game_live) return;   /* a game's sound state is in the APU: leave it alone */
    const uint8_t *spc = spc_start;
    if (spc_end - spc < 0x10200 || memcmp(spc, "SNES-SPC700 Sound File Data", 27) != 0) { ESP_LOGE(TAG, "not an SPC"); return; }
    S9xResetAPU();
    uint8_t iplrom[64];
    memcpy(iplrom, IAPU.RAM + 0xffc0, sizeof iplrom);   /* the boot ROM S9xResetAPU mapped in */
    memcpy(IAPU.RAM, spc + 0x100, 0x10000);
    memcpy(APU.ExtraRAM, spc + 0x101C0, 64);            /* the RAM under the boot ROM */
    APU.ShowROM = !!(IAPU.RAM[0xf1] & 0x80);
    if (APU.ShowROM) memcpy(IAPU.RAM + 0xffc0, iplrom, sizeof iplrom);
    /* DSP registers through the setter so the voices are set up; key-on last */
    for (int reg = 0; reg < 0x80; reg++) {
        if (reg == APU_KON) continue;
        IAPU.RAM[0xf2] = reg;
        S9xSetAPUDSP(spc[0x10100 + reg]);
    }
    IAPU.RAM[0xf2] = APU_KON;
    S9xSetAPUDSP(spc[0x10100 + APU_KON]);
    IAPU.RAM[0xf2] = spc[0x100 + 0xf2];
    /* timers: targets from RAM, then the counters as dumped */
    S9xSetAPUControl(IAPU.RAM[0xf1] & 0x07);
    for (int i = 0; i < 3; i++) IAPU.RAM[0xfd + i] = spc[0x100 + 0xfd + i] & 0x0f;
    /* SPC700 registers */
    IAPU.PC = IAPU.RAM + (spc[0x25] | (spc[0x26] << 8));
    IAPU.Registers.YA.B.A = spc[0x27];
    IAPU.Registers.X = spc[0x28];
    IAPU.Registers.YA.B.Y = spc[0x29];
    IAPU.Registers.P = spc[0x2A];
    IAPU.Registers.S = spc[0x2B];
    S9xAPUUnpackStatus();
    IAPU.DirectPage = IAPU.RAM + ((IAPU.Registers.P & 0x20) ? 0x100 : 0);
    IAPU.APUExecuting = true;
    IAPU.WaitAddress1 = IAPU.WaitAddress2 = NULL;
    IAPU.WaitCounter = 1;
    APU.Cycles = 0;
    playing = true;
    ESP_LOGI(TAG, "playing \"%.32s\" (%.32s)", spc + 0x2E, spc + 0x4E);
#else
    (void)track;
#endif
}

/* one video frame's worth of sound CPU: 262 scanlines of APU cycles, with the timers the main
 * CPU loop would tick (cpuexec.c), then a frame of samples to the DAC */
bool music_tick(void)
{
    if (!playing) return false;
    for (int line = 0; line < 262; line++) {
        CPU.Cycles = Settings.H_Max;
        APU_EXECUTE();
        APU.Cycles -= Settings.H_Max;
        if (APU.TimerEnabled[2]) {
            APU.Timer[2] += 4;
            while (APU.Timer[2] >= APU.TimerTarget[2]) {
                IAPU.RAM[0xff] = (IAPU.RAM[0xff] + 1) & 0xf;
                APU.Timer[2] -= APU.TimerTarget[2];
                IAPU.WaitCounter++;
                IAPU.APUExecuting = true;
            }
        }
        if (line & 1) {
            for (int t = 0; t < 2; t++) {
                if (!APU.TimerEnabled[t]) continue;
                if (++APU.Timer[t] >= APU.TimerTarget[t]) {
                    IAPU.RAM[0xfd + t] = (IAPU.RAM[0xfd + t] + 1) & 0xf;
                    APU.Timer[t] = 0;
                    IAPU.WaitCounter++;
                    IAPU.APUExecuting = true;
                }
            }
        }
    }
    const int samples = 32000 / 60;
    S9xMixSamples(pcm, samples * 2);
    for (int i = 0; i < samples * 2; i++) pcm[i] >>= AUDIO_VOLUME_SHIFT;
    audio_write(pcm, samples);
    return true;
}

void music_stop(void)
{
    if (!playing) return;
    playing = false;
    S9xResetAPU();
}

bool music_playing(void) { return playing; }
