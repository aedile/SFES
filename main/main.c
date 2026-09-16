/*
 * SFES spike: does snes9x run on the Waveshare ESP32-S3-Touch-LCD-1.85, and how fast?
 * ROMs live in the 'roms' partition (tools/pack_roms.py); the chosen one is copied to PSRAM.
 * BOOT button or 'n' on the serial console reboots into the next ROM. Sound through the PCM5101; the blocking I2S
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
#include "sfes_display.h"
#include "audio.h"
#include "snes9x.h"
#include "rlog.h"

static const char *TAG = "SFES";
extern uint32_t s9x_render_cycles;

#include "esp_partition.h"
#include "esp_system.h"
#include "driver/gpio.h"
#define PIN_BOOT 0
static RTC_NOINIT_ATTR uint32_t rom_index;   /* survives the reboot that switches ROM */
static volatile bool want_next_rom;
static void serial_push(uint8_t c);

typedef struct __attribute__((packed)) { char name[48]; uint32_t off, size; } rom_entry_t;

/* copies ROM number rom_index (wrapping) from the roms partition into Memory.ROM; returns its size */
static size_t rom_load(char *name, size_t namelen)
{
    const esp_partition_t *p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "roms");
    assert(p);
    struct { char magic[4]; uint32_t count; } head;
    ESP_ERROR_CHECK(esp_partition_read(p, 0, &head, sizeof head));
    assert(memcmp(head.magic, "SFES", 4) == 0 && head.count > 0);
    if (rom_index >= head.count) rom_index = 0;
    rom_entry_t e;
    ESP_ERROR_CHECK(esp_partition_read(p, 8 + rom_index * sizeof e, &e, sizeof e));
    snprintf(name, namelen, "%.48s", e.name);
    ESP_LOGI(TAG, "ROM %lu/%lu: %s, %lu bytes", rom_index + 1, head.count, name, e.size);
    Memory.ROM_AllocSize = e.size;
    Memory.ROM = heap_caps_malloc(e.size + 0x10000 + 0x200, MALLOC_CAP_SPIRAM);
    assert(Memory.ROM);
    ESP_ERROR_CHECK(esp_partition_read(p, e.off, Memory.ROM, e.size));
    return e.size;
}

#ifndef MEM_LAYOUT
#define MEM_LAYOUT 1  /* bench knob: 0 everything in PSRAM, 1 screen+zbuffer internal, 2 WRAM+VRAM+APU RAM internal */
#endif
#ifndef APU_OFF
#define APU_OFF 0     /* bench knob: 1 = no SPC700 emulation (games may hang waiting for it) */
#endif
#ifndef FRAMESKIP
#define FRAMESKIP 0   /* frames left out of the render log between logged ones; the render core drops what it cannot keep up with anyway */
#endif
#ifndef SOUND
#define SOUND 0       /* 1 = mix and play through the PCM5101 (paces emulation to the DAC); 0 = silent, run flat out */
#endif
#define SAMPLE_RATE   32000
#define VOLUME_SHIFT  2   /* ponytail: software volume, samples >> this; a real volume setting later */

/* ---- pad: serial keys held for 120 ms after each keystroke (read by the watchdog timer) ---- */
static const char keys[] = "wsadjkuiopqe";
static const uint32_t bits[] = { SNES_UP_MASK, SNES_DOWN_MASK, SNES_LEFT_MASK, SNES_RIGHT_MASK,
    SNES_B_MASK, SNES_A_MASK, SNES_Y_MASK, SNES_X_MASK, SNES_TL_MASK, SNES_TR_MASK,
    SNES_START_MASK, SNES_SELECT_MASK };
static volatile int64_t held_until[sizeof keys - 1];

static void serial_push(uint8_t c)
{
    const char *k = memchr(keys, c, sizeof keys - 1);
    if (k) held_until[k - keys] = esp_timer_get_time() + 120000;
}

static uint32_t serial_pad(void)
{
    int64_t now = esp_timer_get_time();
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

static volatile uint32_t frames_total;
static void watchdog(void *arg)
{
    static int ticks; static uint32_t last;
    uint8_t c;
    while (usb_serial_jtag_read_bytes(&c, 1, 0) == 1) {
        if (c == 'n') want_next_rom = true;
        else serial_push(c);
    }
    if (!gpio_get_level(PIN_BOOT)) want_next_rom = true;
    if (want_next_rom) { ESP_LOGI(TAG, "next ROM"); rom_index++; esp_restart(); }
    if (++ticks % 50 == 0) {
        if (frames_total == last) ESP_LOGW(TAG, "emulator stuck: %lu frames total, S9xMainLoop not returning", frames_total);
        last = frames_total;
    }
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
    int frames = 0;
    display_wait_us = 0; render_us = 0;
    for (;;) {
        /* rendering is logged for the render core; FRAMESKIP > 0 leaves frames out of the log */
        IPPU.RenderThisFrame = FRAMESKIP <= 0 || (frames % (FRAMESKIP + 1)) == 0;
        int64_t t0 = esp_timer_get_time();
        S9xMainLoop();
        int64_t t1 = esp_timer_get_time();
        int64_t t2 = esp_timer_get_time();
        if (SOUND) { S9xMixSamples(pcm, samples * 2); for (int i = 0; i < samples * 2; i++) pcm[i] >>= VOLUME_SHIFT; }
        int64_t t3 = esp_timer_get_time();
        if (SOUND) audio_write(pcm, samples);
        else {   /* no DAC to pace us: hold 60 Hz on the timer (sleep the whole ms, spin the rest) */
            static int64_t next;
            int64_t now = esp_timer_get_time();
            if (next == 0 || now - next > 100000) next = now;
            next += frame_us;
            if (next - now > 2000) vTaskDelay((next - now - 1000) / 1000);
            while (esp_timer_get_time() < next) ;
        }
        int64_t t4 = esp_timer_get_time();
        frames_total++; emu_us += t1 - t0; push_us += t2 - t1; mix_us += t3 - t2; audio_wait_us += t4 - t3; frames++;
        if (frames == 300) {   /* fixed frame count, so runs line up on the same moment of the game */
            float sec = (t4 - t_report) / 1e6f;
            ESP_LOGI(TAG, "%.1f fps emulated, %.1f rendered (%lu dropped, %.1f bands/frame) | emu core: %5lld us/frame, mix %4lld, audio wait %4lld | render core: %5lu us/frame (draw %5lu, dma wait %4lu) | %s",
                     frames / sec, rlog_frames / sec, rlog_dropped, rlog_frames ? (float)rlog_bands / rlog_frames : 0.f,
                     emu_us / frames, mix_us / frames, audio_wait_us / frames,
                     rlog_frames ? render_us / rlog_frames : 0, rlog_frames ? s9x_render_cycles / 240 / rlog_frames : 0, rlog_frames ? display_wait_us / rlog_frames : 0,
                     frames / sec >= 59 ? "FULL SPEED" : "slow");
            t_report = t4; frames = 0; emu_us = mix_us = audio_wait_us = 0;
            rlog_frames = rlog_dropped = rlog_bands = 0; render_us = 0; display_wait_us = 0; s9x_render_cycles = 0;
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

    gpio_config_t boot = { .pin_bit_mask = 1ULL << PIN_BOOT, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&boot);
    if (esp_reset_reason() != ESP_RST_SW) rom_index = 0;   /* RTC memory is garbage after power-on */
    /* ROM: flash -> PSRAM. S9xInitMemory keeps a ROM buffer it finds already set. */
    char rom_name[49];
    rom_load(rom_name, sizeof rom_name);

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

    rlog_init();
    render_init();
    log_heap("before tasks");
    assert(xTaskCreatePinnedToCore(render_task, "render", 12288, NULL, 4, (TaskHandle_t *)&render_task_handle, 0) == pdPASS);
    assert(xTaskCreatePinnedToCore(emu_task, "emu", 16384, NULL, 5, NULL, 1) == pdPASS);
    /* independent of the emulator: BOOT button / 'n' switch ROMs even if a game has locked up, and
     * a progress line every 5 s says whether S9xMainLoop is returning at all */
    esp_timer_create_args_t w = { .callback = watchdog, .name = "watchdog" };
    esp_timer_handle_t wh;
    ESP_ERROR_CHECK(esp_timer_create(&w, &wh));
    ESP_ERROR_CHECK(esp_timer_start_periodic(wh, 100000));
}
