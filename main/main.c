/*
 * SFES spike: does snes9x run on the Waveshare ESP32-S3-Touch-LCD-1.85, and how fast?
 * One ROM embedded in the app, copied to PSRAM. Sound through the PCM5101; the blocking I2S
 * write paces emulation to the DAC clock, and a frame that ran long skips drawing the next one.
 * No BLE yet. Keys typed into the serial monitor act as the pad:
 *   w/a/s/d = d-pad, j = B, k = A, u = Y, i = X, o = L, p = R, q = Start, e = Select
 * Every 5 s the log prints frames per second and where the time went.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display.h"
#include "audio.h"
#include "snes9x.h"

static const char *TAG = "SFES";

extern const uint8_t rom_start[] asm("_binary_rom_sfc_start");
extern const uint8_t rom_end[]   asm("_binary_rom_sfc_end");

#ifndef MEM_LAYOUT
#define MEM_LAYOUT 1  /* bench knob: 0 everything in PSRAM, 1 screen+zbuffer internal, 2 WRAM+VRAM+APU RAM internal */
#endif
#ifndef APU_OFF
#define APU_OFF 0     /* bench knob: 1 = no SPC700 emulation (games may hang waiting for it) */
#endif
#ifndef FRAMESKIP
#define FRAMESKIP -1  /* -1 auto (skip the draw after a long frame), else frames skipped between drawn ones */
#endif
#ifndef SOUND
#define SOUND 0       /* 1 = mix and play through the PCM5101 (paces emulation to the DAC); 0 = silent, run flat out */
#endif
#define SAMPLE_RATE   32000
#define VOLUME_SHIFT  2   /* ponytail: software volume, samples >> this; a real volume setting later */
#define MAX_AUTO_SKIP 2

/* ---- pad: serial keys held for 120 ms after each keystroke ---- */
static uint32_t serial_pad(void)
{
    static const char keys[] = "wsadjkuiopqe";
    static const uint32_t bits[] = { SNES_UP_MASK, SNES_DOWN_MASK, SNES_LEFT_MASK, SNES_RIGHT_MASK,
        SNES_B_MASK, SNES_A_MASK, SNES_Y_MASK, SNES_X_MASK, SNES_TL_MASK, SNES_TR_MASK,
        SNES_START_MASK, SNES_SELECT_MASK };
    static int64_t held_until[sizeof keys - 1];
    int64_t now = esp_timer_get_time();
    uint8_t c;
    while (usb_serial_jtag_read_bytes(&c, 1, 0) == 1) {
        const char *k = memchr(keys, c, sizeof keys - 1);
        if (k) held_until[k - keys] = now + 120000;
    }
    uint32_t m = 0;
    for (size_t i = 0; i < sizeof keys - 1; i++) if (held_until[i] > now) m |= bits[i];
    return m;
}

/* ---- what snes9x expects the host to provide ---- */
bool S9xInitDisplay(void)
{
    GFX.Pitch = SNES_WIDTH * 2;
    GFX.ZPitch = SNES_WIDTH;
    /* main screen and its depth buffer are touched every pixel: internal RAM */
    uint32_t caps = MEM_LAYOUT == 1 ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM;
    GFX.Screen = heap_caps_malloc(GFX.Pitch * SNES_HEIGHT_EXTENDED, caps);
    GFX.ZBuffer = heap_caps_malloc(GFX.ZPitch * SNES_HEIGHT_EXTENDED, caps);
    GFX.SubScreen = malloc(GFX.Pitch * SNES_HEIGHT_EXTENDED);
    GFX.SubZBuffer = malloc(GFX.ZPitch * SNES_HEIGHT_EXTENDED);
    return GFX.Screen && GFX.SubScreen && GFX.ZBuffer && GFX.SubZBuffer;
}
void S9xDeinitDisplay(void) {}
uint32_t S9xReadJoypad(int32_t port) { return port == 0 ? serial_pad() : 0; }
bool S9xReadMousePosition(int32_t w, int32_t *x, int32_t *y, uint32_t *b) { return false; }
bool S9xReadSuperScopePosition(int32_t *x, int32_t *y, uint32_t *b) { return false; }
bool JustifierOffscreen(void) { return true; }
void JustifierButtons(uint32_t *j) {}

/* ---- display push on core 0, straight out of the live framebuffer ----
 * No copy: snes9x renders scanlines top to bottom over the course of a frame (10 ms or more),
 * and the push walks the same rows top to bottom in about 4 ms starting the moment the frame
 * is done, so it stays ahead of the next frame's rendering of every row. If the push has not
 * finished when the next frame completes, that frame is not drawn. */
static TaskHandle_t push_task_h;
static volatile int push_busy;
static uint32_t push_skipped;

static void push_task(void *arg)
{
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        display_push_rgb565((const uint16_t *)GFX.Screen, GFX.Pitch);
        push_busy = 0;
    }
}

static bool push_frame(void)
{
    if (push_busy) { push_skipped++; return false; }
    push_busy = 1;
    xTaskNotifyGive(push_task_h);
    return true;
}

static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: internal free %u (largest %u), psram free %u", when,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void emu_task(void *arg)
{
    const int frame_us = 1000000 / Memory.ROMFramesPerSecond;
    const int samples = SAMPLE_RATE / Memory.ROMFramesPerSecond;   /* stereo frames per video frame */
    int16_t *pcm = malloc(samples * 4);
    assert(pcm);
    int64_t t_report = esp_timer_get_time(), emu_us = 0, push_us = 0, mix_us = 0, audio_wait_us = 0;
    int frames = 0, drawn = 0, skip = 0, streak = 0;
    display_wait_us = 0;
    for (;;) {
        bool draw = skip == 0;
        if (FRAMESKIP >= 0) skip = draw ? FRAMESKIP : skip - 1;
        else skip = 0;
        IPPU.RenderThisFrame = draw;
        int64_t t0 = esp_timer_get_time();
        S9xMainLoop();
        int64_t t1 = esp_timer_get_time();
        if (draw && push_frame()) drawn++;
        int64_t t2 = esp_timer_get_time();
        if (SOUND) { S9xMixSamples(pcm, samples * 2); for (int i = 0; i < samples * 2; i++) pcm[i] >>= VOLUME_SHIFT; }
        int64_t t3 = esp_timer_get_time();
        if (SOUND) audio_write(pcm, samples);
        int64_t t4 = esp_timer_get_time();
        emu_us += t1 - t0; push_us += t2 - t1; mix_us += t3 - t2; audio_wait_us += t4 - t3; frames++;
        /* auto frameskip: a frame that overran the budget (before the paced audio wait) drops the next draw */
        if (FRAMESKIP < 0) {
            if (t3 - t0 > frame_us && draw && streak < MAX_AUTO_SKIP) { skip = 1; streak++; }
            else if (draw) streak = 0;
        }
        if (frames == 300) {   /* fixed frame count, so runs line up on the same moment of the game */
            float s = (t4 - t_report) / 1e6f;
            ESP_LOGI(TAG, "%.1f fps emulated, %.1f drawn | per frame: emu %5lld us, notify %4lld us (core 0 dma wait %4lu us, %lu busy-skips), mix %4lld us, audio wait %4lld us | %s",
                     frames / s, drawn / s, emu_us / frames, push_us / (drawn ? drawn : 1), display_wait_us / (drawn ? drawn : 1), push_skipped, mix_us / frames, audio_wait_us / frames,
                     frames / s >= 59 ? "FULL SPEED" : "slow");
            t_report = t4; frames = drawn = 0; emu_us = push_us = mix_us = audio_wait_us = 0; display_wait_us = 0; push_skipped = 0;
        }
    }
}

void app_main(void)
{
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&usb);
    log_heap("at boot");
    display_init();

    Settings.CyclesPercentage = 100;
    Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.ControllerOption = SNES_JOYPAD;
    Settings.HBlankStart = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;
    Settings.SoundPlaybackRate = SAMPLE_RATE;
    Settings.SoundInputRate = SAMPLE_RATE;
    Settings.DisableSoundEcho = false;
    Settings.InterpolatedSound = false;   /* cheaper; turn on if there is CPU to spare */

    /* ROM: flash -> PSRAM. S9xInitMemory keeps a ROM buffer it finds already set. */
    size_t rom_len = rom_end - rom_start;
    Memory.ROM_AllocSize = rom_len;
    Memory.ROM = heap_caps_malloc(rom_len + 0x10000 + 0x200, MALLOC_CAP_SPIRAM);
    assert(Memory.ROM);
    memcpy(Memory.ROM, rom_start, rom_len);
    ESP_LOGI(TAG, "ROM %u bytes copied to PSRAM", (unsigned)rom_len);

    assert(S9xInitDisplay());
    assert(S9xInitMemory());
    assert(S9xInitAPU());
    assert(S9xInitSound(0, 0));
    assert(S9xInitGFX());
    {   /* bench: move the CPU-side RAMs (malloc'd by the core, so into PSRAM) to internal RAM */
        uint32_t caps = MEM_LAYOUT == 2 ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM;
        free(Memory.RAM);  Memory.RAM  = heap_caps_calloc(1, RAM_SIZE, caps);
        free(Memory.VRAM); Memory.VRAM = heap_caps_calloc(1, VRAM_SIZE, caps);
        free(IAPU.RAM);    IAPU.RAM    = heap_caps_calloc(1, 0x10000, caps);
        assert(Memory.RAM && Memory.VRAM && IAPU.RAM);
    }
    assert(LoadROM(NULL));
    Settings.APUEnabled = !APU_OFF;
    IAPU.APUExecuting = Settings.APUEnabled;
    S9xSetPlaybackRate(Settings.SoundPlaybackRate);
    ESP_LOGI(TAG, "bench: MEM_LAYOUT=%d APU_OFF=%d FRAMESKIP=%d", MEM_LAYOUT, APU_OFF, FRAMESKIP);
    ESP_LOGI(TAG, "loaded: %s (%s, %u fps)", Memory.ROMName, Memory.HiROM ? "HiROM" : "LoROM", (unsigned)Memory.ROMFramesPerSecond);
    log_heap("after init");
    if (SOUND) audio_init(SAMPLE_RATE, SAMPLE_RATE / Memory.ROMFramesPerSecond);

    xTaskCreatePinnedToCore(push_task, "push", 4096, NULL, 4, &push_task_h, 0);
    xTaskCreatePinnedToCore(emu_task, "emu", 16384, NULL, 5, NULL, 1);
}
