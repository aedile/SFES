/*
 * S.F.E.S. - a SNES emulator for the Waveshare ESP32-S3-Touch-LCD-1.85, worn as a fiesta medal.
 *
 * Boot -> box-art picker -> game (MENU: resume / save / load / reset / mute / picker).
 * A pad idle for 3 minutes -> demo mode: the games' own attract modes, DEMO_SECONDS each.
 * Boot -> controller screen -> picker. Without a controller the medal's two buttons work everywhere:
 *   PWR  short: next game            long (2 s): power off
 *   BOOT short: lock/unlock the demo to the current game (kept in NVS)
 *        hold 3 s: mute / unmute        hold 10 s: forget the saved controller
 * Emulation runs on core 1 (this task), rendering on core 0 (render.c); ROMs live in the
 * 'roms' partition, battery RAM and save states in the 'saves' NVS partition.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_rom_crc.h"
#include "esp_heap_caps.h"
#include "driver/usb_serial_jtag.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "sfes_display.h"
#include "audio.h"
#include "snes9x.h"
#include "rlog.h"
#include "ble_pad.h"
#include "ui.h"
#include "festive.h"
#include "medal.h"
#include "saves.h"
#include "core.h"
#include "music.h"
#include "splash.h"

static const char *TAG = "SFES";
extern uint32_t s9x_render_cycles;

#define SAMPLE_RATE     32000
#define DEMO_AFTER_US   30000000LL   /* no controller for this long -> demo mode */
#define IDLE_AFTER_US   180000000LL  /* pad untouched this long -> demo mode */
#ifndef DEMO_SECONDS
#define DEMO_SECONDS    120          /* per game in demo mode (idf.py -DDEMO_SECONDS=30) */
#endif
#define BACKLIGHT_PLAY  200
#define BACKLIGHT_DEMO  90
#define ART_W 134
#define ART_H 96
#ifndef MEM_LAYOUT
#define MEM_LAYOUT 1   /* 1: framebuffer in internal RAM, depth buffer in PSRAM (BLE needs the rest); 2: both internal; 0: both PSRAM */
#endif

/* ---- settings in NVS: demo lock, games excluded from the cycle, mute ---- */
#define NVS_NS "sfes"
static int demo_lock = -1;
static bool demo_skip[64];
static bool muted;

static void nvs_key_for(char out[16], char type, const char *rom)
{
    snprintf(out, 16, "%c%08lx", type, (unsigned long)esp_rom_crc32_le(0, (const uint8_t *)rom, strlen(rom)));
}

static void settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    char lock[48] = {0}; size_t n = sizeof lock;
    if (nvs_get_str(h, "demo_lock", lock, &n) == ESP_OK)
        for (int i = 0; i < nroms; i++) if (strcmp(roms[i].name, lock) == 0) demo_lock = i;
    for (int i = 0; i < nroms && i < 64; i++) {
        char k[16]; uint8_t v = 0;
        nvs_key_for(k, 'd', roms[i].name);
        if (nvs_get_u8(h, k, &v) == ESP_OK) demo_skip[i] = v;
    }
    uint8_t m = 0;
    muted = nvs_get_u8(h, "mute", &m) == ESP_OK && m;
    audio_set_mute(muted);
    nvs_close(h);
}

static void demo_set_lock(int idx)
{
    demo_lock = idx;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (idx >= 0) nvs_set_str(h, "demo_lock", roms[idx].name); else nvs_erase_key(h, "demo_lock");
    nvs_commit(h); nvs_close(h);
}

static void demo_set_skip(int idx, bool skip)
{
    demo_skip[idx] = skip;
    char k[16]; nvs_key_for(k, 'd', roms[idx].name);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, k, skip);
    nvs_commit(h); nvs_close(h);
}

static void set_mute(bool m)
{
    muted = m;
    audio_set_mute(m);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "mute", m); nvs_commit(h); nvs_close(h); }
    ESP_LOGI(TAG, "%s", m ? "muted" : "sound on");
}

static int demo_next(int i)
{
    for (int n = (i + 1) % nroms, tries = 0; tries < nroms; n = (n + 1) % nroms, tries++)
        if (!demo_skip[n]) return n;
    return (i + 1) % nroms;
}

/* ---- input: keys typed into the serial monitor, read by the watchdog timer (a BLE pad comes later)
 * w/a/s/d = d-pad, j = B, k = A, u = Y, i = X, o = L, p = R, q = Start, e = Select, m = menu,
 * x = demo now, n / l = the medal's PWR / BOOT short press ---- */
static const char keys[] = "wsadjkuiopqem";
static const uint32_t bits[] = { PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_B, PAD_A, PAD_Y, PAD_X, PAD_L, PAD_R,
                                 PAD_START, PAD_SELECT, PAD_MENU };
static volatile int64_t held_until[sizeof keys - 1];
static volatile int64_t serial_last = -10000000;
static volatile bool serial_demo;
static volatile uint32_t serial_medal;
static bool serial_active(void) { return esp_timer_get_time() - serial_last < 5000000; }

static void serial_push(uint8_t c)
{
    int64_t now = esp_timer_get_time();
    const char *k = memchr(keys, c, sizeof keys - 1);
    if (k) { held_until[k - keys] = now + 120000; serial_last = now; }
    if (c == 'x') { serial_demo = true; serial_last = now; }
    if (c == 'n') { serial_medal |= BTN_PWR_SHORT; serial_last = now; }
    if (c == 'l') { serial_medal |= BTN_BOOT_SHORT; serial_last = now; }
}

static uint32_t serial_pad(void)
{
    int64_t now = esp_timer_get_time();
    uint32_t m = 0;
    for (size_t i = 0; i < sizeof keys - 1; i++) if (held_until[i] > now) m |= bits[i];
    return m;
}

static uint32_t pad_now(void)
{
    uint32_t b = ble_pad_buttons(), raw = ble_pad_raw();
    static uint32_t last_b = 0, last_raw = 0;
    if (b != last_b || raw != last_raw) { ESP_LOGI(TAG, "PAD raw=%04lx buttons=%04lx", raw, b); last_b = b; last_raw = raw; }
    b |= serial_pad();
    if ((b & (PAD_SELECT | PAD_START)) == (PAD_SELECT | PAD_START)) b = (b & ~(PAD_SELECT | PAD_START)) | PAD_MENU;
    return b;
}

static void medal_global(void);

/* newly pressed bits, with key repeat on the d-pad for lists; also runs the medal buttons' always-on jobs */
static uint32_t pad_edges(void)
{
    static uint32_t prev;
    static int64_t repeat_at;
    medal_global();
    int64_t now = esp_timer_get_time();
    uint32_t cur = pad_now(), e = cur & ~prev;
    uint32_t dpad = PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT;
    if (cur & dpad) {
        if (e & dpad) repeat_at = now + 400000;
        else if (now > repeat_at) { e |= cur & dpad; repeat_at = now + 120000; }
    }
    prev = cur;
    return e;
}

static void toast(const char *line1, const char *line2);
static void toast_mute(void);
static uint32_t medal_pending;

static void medal_global(void)
{
    uint32_t ev = medal_poll() | serial_medal;
    serial_medal = 0;
    if (ev & BTN_BOOT_HOLD3) { set_mute(!muted); toast_mute(); }
    if (ev & BTN_BOOT_HOLD10) {
        set_mute(!muted);
        ble_pad_forget(); ble_pad_scan_any(true);
        toast("Controller forgotten", "pair one on the controller screen");
    }
    medal_pending |= ev & (BTN_BOOT_SHORT | BTN_PWR_SHORT);
}

static uint32_t medal_events(void)
{
    medal_global();
    uint32_t ev = medal_pending;
    medal_pending = 0;
    return ev;
}

bool splash_skip_requested(void) { return pad_edges() || (medal_events() & (BTN_BOOT_SHORT | BTN_PWR_SHORT)); }

const uint8_t *splash_cover(const char *sn, int *w, int *h)
{
    for (int i = 0; i < nroms; i++) {
        char n[29]; short_name(roms[i].name, n, sizeof n);
        if (strcmp(n, sn) == 0) return core_art(i, w, h);
    }
    *w = ART_W; *h = ART_H;
    return NULL;
}

/* the SNES pad as the core sees it */
static volatile uint32_t snes_pad;
uint32_t S9xReadJoypad(int32_t port) { return port == 0 ? snes_pad : 0; }
static uint32_t snes_buttons(uint32_t b)
{
    static const struct { uint32_t pad, snes; } map[] = {
        { PAD_A, SNES_A_MASK }, { PAD_B, SNES_B_MASK }, { PAD_X, SNES_X_MASK }, { PAD_Y, SNES_Y_MASK },
        { PAD_L, SNES_TL_MASK }, { PAD_R, SNES_TR_MASK }, { PAD_START, SNES_START_MASK }, { PAD_SELECT, SNES_SELECT_MASK },
        { PAD_UP, SNES_UP_MASK }, { PAD_DOWN, SNES_DOWN_MASK }, { PAD_LEFT, SNES_LEFT_MASK }, { PAD_RIGHT, SNES_RIGHT_MASK },
    };
    uint32_t m = 0;
    for (size_t i = 0; i < sizeof map / sizeof *map; i++) if (b & map[i].pad) m |= map[i].snes;
    return m;
}

/* ---- the rest of what snes9x expects from the host ---- */
bool S9xInitDisplay(void)
{
    GFX.Pitch = SNES_WIDTH * 2;
    GFX.ZPitch = SNES_WIDTH;
    /* 224 rows (the render side clamps PPU.ScreenHeight so overscan games never write past them)
     * plus two rows of slack: hi-res modes 5/6 write 512 pixels per row, running one row past the last */
    GFX.Screen = heap_caps_malloc(GFX.Pitch * (SNES_HEIGHT + 2), MEM_LAYOUT >= 1 ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM);
    GFX.ZBuffer = heap_caps_malloc(GFX.ZPitch * (SNES_HEIGHT + 2), MEM_LAYOUT >= 2 ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM);
    GFX.SubScreen = malloc(GFX.Pitch * SNES_HEIGHT_EXTENDED);
    GFX.SubZBuffer = malloc(GFX.ZPitch * SNES_HEIGHT_EXTENDED);
    return GFX.Screen && GFX.SubScreen && GFX.ZBuffer && GFX.SubZBuffer;
}
void S9xDeinitDisplay(void) {}
bool S9xReadMousePosition(int32_t w, int32_t *x, int32_t *y, uint32_t *b) { return false; }
bool S9xReadSuperScopePosition(int32_t *x, int32_t *y, uint32_t *b) { return false; }
bool JustifierOffscreen(void) { return true; }
void JustifierButtons(uint32_t *j) {}

static void log_heap(const char *when);
/* a boot-time failure must not reboot in a loop: that re-enumerates USB faster than esptool can connect */
static void die(const char *what)
{
    ESP_LOGE(TAG, "FATAL: %s", what);
    log_heap("at failure");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
#define MUST(x) do { if (!(x)) die(#x); } while (0)

static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: internal free %u (largest %u), psram free %u", when,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

/* ---- battery: a warning under 15 %, power-off once a plausible reading sits under 3 % for a minute ---- */
#define BATTERY_WARN_PCT 15
static const char *battery_warning(void)
{
    static int64_t low_since;
    int pct = medal_battery_percent(), mv = medal_battery_mv();
    bool plausible = mv > 2900 && mv < 3400;
    if (pct < 3 && plausible) {
        if (!low_since) low_since = esp_timer_get_time();
        if (esp_timer_get_time() - low_since > 60000000) { ESP_LOGW(TAG, "battery %d mV: powering off", mv); medal_power_off(); }
    } else low_since = 0;
    return pct < BATTERY_WARN_PCT ? "LOW BATTERY" : NULL;
}

/* ---- overlays ---- */
static void toast(const char *line1, const char *line2)
{
    int w = 8 * (int)(strlen(line1) > strlen(line2) ? strlen(line1) : strlen(line2)) + 32;
    if (w > 240) w = 240;
    int x = (256 - w) / 2;
    ui_fill(x, 88, w, 48, UI_BLACK);
    ui_frame(x, 88, w, 48, UI_WHITE);
    ui_text_center(100, line1, UI_YELLOW);
    ui_text_center(116, line2, UI_WHITE);
    ui_present();
    for (int i = 0; i < 60; i++) ui_tick(false);   /* ~1 s */
}

static void toast_mute(void) { toast(muted ? "Muted" : "Sound on", "SELECT or hold BOOT 3 s"); }

static void toggle_lock(int idx)
{
    char name[29]; short_name(roms[idx].name, name, sizeof name);
    if (demo_lock == idx) { demo_set_lock(-1); toast("Demo unlocked", "cycling all games"); }
    else { demo_set_lock(idx); toast("Demo locked on", name); }
}

/* ---- controller screen. Returns false if nothing connected for DEMO_AFTER_US (or PWR pressed). ---- */
static bool controller_screen(bool boot)
{
    int64_t deadline = esp_timer_get_time() + ((boot && ble_pad_has_saved()) ? 5000000 : 0);
    int64_t demo_at = esp_timer_get_time() + DEMO_AFTER_US;
    bool any = false;
    int sel = 0, anim = 0;
    music_start(0);
    ble_pad_scan_rate(true);
    pad_edges();
    for (;;) {
        ble_pad_state_t st = ble_pad_state();
        bool connected = st == PAD_CONNECTED;
        if (!any && !connected && (esp_timer_get_time() > deadline || !ble_pad_has_saved())) { any = true; ble_pad_scan_any(true); }
        if (connected && any) { any = false; ble_pad_scan_any(false); }
        uint32_t mev = medal_events();
        if (mev & BTN_PWR_SHORT) return false;
        uint32_t e = pad_edges();
        if (connected || serial_active()) {
            if (e & PAD_UP) sel = 0;
            if (e & PAD_DOWN) sel = 1;
            if ((e & PAD_A) && sel == 1) { ble_pad_forget(); any = true; ble_pad_scan_any(true); sel = 0; toast("Controller forgotten", "pair one now"); }
            if (((e & PAD_A) && sel == 0) || (e & (PAD_B | PAD_MENU))) return true;
        }
        if (boot && connected) return true;
        if (!connected && !serial_active() && esp_timer_get_time() > demo_at) return false;
        {
            int frame = anim++;
            ui_clear(UI_BLACK);
            festive_confetti(frame);
            festive_papel_picado(frame);
            ui_text_center(30, "CONTROLLER", UI_YELLOW);
            const char *st_s = "Idle"; uint16_t c = UI_GREY;
            if (st == PAD_SCANNING) { st_s = "Scanning..."; c = UI_WHITE; }
            if (st == PAD_CONNECTING) { st_s = "Connecting..."; c = UI_YELLOW; }
            if (connected) { st_s = "Connected"; c = UI_GREEN; }
            ui_text(24, 46, "Status:", UI_GREY); ui_text(96, 46, st_s, c);
            ui_text(24, 58, "Found:", UI_GREY);  ui_text(96, 58, ble_pad_name()[0] ? ble_pad_name() : "-", UI_WHITE);
            ui_text(24, 70, "Saved:", UI_GREY);  ui_text(96, 70, ble_pad_has_saved() ? "yes" : "no", UI_WHITE);
            festive_dancers(frame, 140);
            if (!connected) {
                ui_text_center(150, "Pairing mode on the pad,", UI_WHITE);
                ui_text_center(162, "hold it against the medal", UI_WHITE);
                char d[32]; snprintf(d, sizeof d, "demo mode in %d s", (int)((demo_at - esp_timer_get_time()) / 1000000));
                ui_text_center(178, d, UI_GREY);
            } else {
                ui_text(48, 150, sel == 0 ? ">" : " ", UI_YELLOW); ui_text(64, 150, "Back", sel == 0 ? UI_YELLOW : UI_WHITE);
                ui_text(48, 164, sel == 1 ? ">" : " ", UI_YELLOW); ui_text(64, 164, "Forget this controller", sel == 1 ? UI_YELLOW : UI_WHITE);
            }
            ui_text_center(196, "PWR demo now  hold: power off", UI_GREY);
            ui_text_center(208, "BOOT 3s mute  10s forget pad", UI_GREY);
            ui_tick((frame & 1) == 0);
        }
    }
}

/* ---- box-art picker: the selected cover big in the middle, neighbours half size ---- */
static void draw_cover(int idx, int cx, int cy, int num, int den)
{
    int w, h;
    const uint8_t *art = core_art(idx, &w, &h);
    int ow = w * num / den, oh = h * num / den, x = cx - ow / 2, y = cy - oh / 2;
    if (art) ui_bitmap(x, y, art, w, h, num, den);
    else {
        ui_fill(x, y, ow, oh, UI_GREY);
        if (num == den) { char n[17]; short_name(roms[idx].name, n, sizeof n); ui_text_center(cy - 4, n, UI_WHITE); }
    }
}

static int picker(int sel)
{
    bool has_save[64];
    for (int i = 0; i < nroms && i < 64; i++) has_save[i] = saves_has_sram(roms[i].name);
    int anim = 0;
    int64_t last_input = esp_timer_get_time();
    display_set_backlight(BACKLIGHT_PLAY);
    music_start(0);
    pad_edges();
    for (;;) {
        uint32_t e = pad_edges(), mev = medal_events();
        if (e || mev) last_input = esp_timer_get_time();
        if ((e & PAD_LEFT) && sel > 0) sel--;
        if (((e & PAD_RIGHT) || (mev & BTN_PWR_SHORT)) && sel < nroms - 1) sel++;
        if (e & PAD_A) return sel;
        if (e & PAD_B) demo_set_skip(sel, !demo_skip[sel]);
        if (e & PAD_SELECT) { set_mute(!muted); toast_mute(); }
        if ((mev & BTN_BOOT_SHORT) && nroms) toggle_lock(sel);
        if (serial_demo) { serial_demo = false; return -1; }
        if (esp_timer_get_time() - last_input > IDLE_AFTER_US) return -1;
        if (e & PAD_MENU) { if (!controller_screen(false)) return -1; }
        if (ble_pad_state() != PAD_CONNECTED && !serial_active()) { if (!controller_screen(false)) return -1; }
        {
            int frame = anim++;
            ui_clear(UI_BLACK);
            festive_confetti(frame);
            festive_papel_picado(frame);
            ui_text(8, 30, "S.F.E.S.", UI_YELLOW);
            char bat[8]; snprintf(bat, sizeof bat, "%d%%", medal_battery_percent());
            if (!(battery_warning() && (frame & 32))) ui_text(80, 30, bat, UI_GREY);
            if (nroms == 0) { ui_text_center(100, "No ROMs in partition", UI_RED); ui_tick(true); continue; }
            if (sel > 0) draw_cover(sel - 1, 36, 104, 1, 2);
            if (sel + 1 < nroms) draw_cover(sel + 1, 220, 104, 1, 2);
            draw_cover(sel, 128, 104, 1, 1);
            uint16_t fc = sel == demo_lock ? UI_YELLOW : fiesta_colours[(frame >> 4) % 6];
            ui_frame(128 - ART_W / 2 - 2, 104 - ART_H / 2 - 2, ART_W + 4, ART_H + 4, fc);
            ui_frame(128 - ART_W / 2 - 3, 104 - ART_H / 2 - 3, ART_W + 6, ART_H + 6, fc);
            char name[32]; short_name(roms[sel].name, name, sizeof name);
            ui_text_center(162, name, UI_WHITE);
            char tags[40] = "";
            if (has_save[sel]) strcat(tags, "* saved  ");
            if (demo_skip[sel]) strcat(tags, "no demo  ");
            if (sel == demo_lock) strcat(tags, "demo locked  ");
            if (muted) strcat(tags, "muted");
            ui_text_center(175, tags, has_save[sel] ? UI_GREEN : UI_GREY);
            const char *warn = battery_warning();
            if (warn && (frame & 32)) ui_text(80, 30, warn, UI_RED);
            char pos[24]; snprintf(pos, sizeof pos, "%d/%d", sel + 1, nroms);
            ui_text(248 - 8 * strlen(pos), 30, pos, UI_GREY);
            ui_text_center(196, "A play  B demo  SEL mute", UI_GREY);
            ui_text_center(208, "SEL+START menu  MENU controller", UI_GREY);
            ui_tick((frame & 1) == 0);   /* 30 fps presents */
        }
    }
}

/* ---- in-game menu, over the paused frame ---- */
enum { MENU_RESUME, MENU_SAVE, MENU_LOAD, MENU_RESET, MENU_MUTE, MENU_PICKER, MENU_CONTROLLER, MENU_COUNT };
static int game_menu(const char *rom)
{
    const char *items[MENU_COUNT] = { "Resume", "Save state", "Load state", "Reset game", muted ? "Unmute" : "Mute", "Return to picker", "Controller" };
    bool have_state = saves_has_state(rom);
    int sel = 0;
    bool dirty = true;
    pad_edges();
    for (;;) {
        uint32_t e = pad_edges();
        if ((e & PAD_UP) && sel > 0) { sel--; dirty = true; }
        if ((e & PAD_DOWN) && sel < MENU_COUNT - 1) { sel++; dirty = true; }
        if (e & (PAD_B | PAD_MENU)) return MENU_RESUME;
        if ((e & PAD_A) && !(sel == MENU_LOAD && !have_state)) return sel;
        if (dirty) {
            dirty = false;
            ui_fill(56, 40, 144, 130, UI_BLACK);
            ui_frame(56, 40, 144, 130, UI_WHITE);
            ui_text(72, 52, "MENU", UI_YELLOW);
            for (int i = 0; i < MENU_COUNT; i++) {
                uint16_t c = i == sel ? UI_YELLOW : UI_WHITE;
                if (i == MENU_LOAD && !have_state) c = UI_GREY;
                ui_text(72, 74 + i * 14, i == sel ? ">" : " ", UI_YELLOW);
                ui_text(88, 74 + i * 14, items[i], c);
            }
            ui_present();
        }
        vTaskDelay(pdMS_TO_TICKS(16));
    }
}

/* battery RAM: to NVS once the core has written it and then left it alone for a second, or on the way out */
static size_t sram_bytes(void) { return Memory.SRAMSize ? Memory.SRAMMask + 1 : 0; }
static void sram_flush(const char *rom, bool force)
{
    static bool dirty; static int quiet;
    if (!sram_bytes()) return;
    if (CPU.SRAMModified) { CPU.SRAMModified = false; dirty = true; quiet = 0; }
    else quiet++;
    if (!dirty || (!force && quiet < 60)) return;
    if (saves_store_sram(rom, Memory.SRAM, sram_bytes())) ESP_LOGI(TAG, "battery RAM saved");
    dirty = false;
}

typedef enum { GAME_PICKER, GAME_IDLE, GAME_DEMO_NEXT, GAME_DEMO_EXIT } game_result_t;

/* one frame of emulation with sound; the render core takes the frame from the log */
static void emulate_frame(int16_t *pcm, int samples)
{
    IPPU.RenderThisFrame = true;
    S9xMainLoop();
    S9xMixSamples(pcm, samples * 2);
    for (int i = 0; i < samples * 2; i++) pcm[i] >>= AUDIO_VOLUME_SHIFT;
    audio_write(pcm, samples);   /* blocks on the DAC queue: this is the 60 Hz pacing */
}

static game_result_t run_game(int idx, bool demo)
{
    const char *rom = roms[idx].name;
    music_stop();
    if (demo) {
        char name[29]; short_name(rom, name, sizeof name);
        for (int frame = 0; frame < 150; frame++) {
            ui_clear(UI_BLACK);
            festive_confetti(frame);
            festive_papel_picado(frame);
            draw_cover(idx, 128, 92, 1, 1);
            ui_frame(128 - ART_W / 2 - 2, 92 - ART_H / 2 - 2, ART_W + 4, ART_H + 4, fiesta_colours[(frame >> 4) % 6]);
            ui_text_center(156, name, UI_YELLOW);
            ui_text_center(172, demo_lock == idx ? "demo (locked)" : "demo", UI_GREY);
            const char *warn = battery_warning();
            ui_text_center(200, warn && (frame & 32) ? warn : "press any pad button to play", warn && (frame & 32) ? UI_RED : UI_GREY);
            ui_tick((frame & 1) == 0);
            if (pad_edges()) return GAME_DEMO_EXIT;
        }
    }
    music_game_live(true);
    if (!core_load(idx)) {
        music_game_live(false);
        ui_clear(UI_BLACK); ui_text_center(108, "Unsupported ROM", UI_RED); ui_present();
        vTaskDelay(pdMS_TO_TICKS(1500));
        return demo ? GAME_DEMO_NEXT : GAME_PICKER;
    }
    if (sram_bytes() && !demo && saves_load_sram(rom, Memory.SRAM, sram_bytes())) ESP_LOGI(TAG, "battery RAM loaded");
    CPU.SRAMModified = false;
    display_set_backlight(demo ? BACKLIGHT_DEMO : BACKLIGHT_PLAY);
    ESP_LOGI(TAG, "running %s%s", rom, demo ? " (demo)" : "");
    log_heap("in game");

    const int samples = SAMPLE_RATE / Memory.ROMFramesPerSecond;
    int16_t *pcm = malloc(samples * 4);
    assert(pcm);
    int frames = 0;
    int64_t t_report = esp_timer_get_time(), emu_us = 0, audio_us = 0;
    rlog_frames = rlog_dropped = rlog_bands = 0; render_us = 0; s9x_render_cycles = 0;
    int64_t t_start = t_report, last_input = t_start;
    uint32_t prev = 0;
    pad_edges();
    game_result_t result;
    for (;;) {
        int64_t f0 = esp_timer_get_time();
        uint32_t b = 0, mev = medal_events();
        if (mev & BTN_BOOT_SHORT) toggle_lock(idx);
        if (demo) {
            if (pad_edges()) { result = GAME_DEMO_EXIT; break; }
            if (mev & BTN_PWR_SHORT) { result = GAME_DEMO_NEXT; break; }
            if (demo_lock != idx && f0 - t_start > (int64_t)DEMO_SECONDS * 1000000) { result = GAME_DEMO_NEXT; break; }
        } else {
            b = pad_now();
            if (b != prev) last_input = f0;
            if (f0 - last_input > IDLE_AFTER_US) { result = GAME_IDLE; break; }
            if (mev & BTN_PWR_SHORT) { result = GAME_PICKER; break; }
        }
        if (!demo && (b & PAD_MENU) && !(prev & PAD_MENU)) {
            sram_flush(rom, true);
            while (render_busy) vTaskDelay(1);
            int a = game_menu(rom);
            if (a == MENU_SAVE) { saves_save_state(rom); }
            if (a == MENU_LOAD) { saves_load_state(rom); rlog_reset(); }
            if (a == MENU_RESET) { S9xReset(); rlog_reset(); }
            if (a == MENU_MUTE) set_mute(!muted);
            if (a == MENU_CONTROLLER) controller_screen(false);
            if (a == MENU_PICKER) { result = GAME_PICKER; break; }
            prev = pad_now();
            last_input = esp_timer_get_time();
            continue;
        }
        prev = b;
        snes_pad = snes_buttons(b);
        int64_t e0 = esp_timer_get_time();
        emulate_frame(pcm, samples);
        int64_t e1 = esp_timer_get_time();
        emu_us += e1 - e0; frames++;
        if (!demo) sram_flush(rom, false);
        if (frames == 300) {
            float sec = (e1 - t_report) / 1e6f;
            ESP_LOGI(TAG, "%.1f fps, %.1f rendered (%lu dropped, %.1f bands) | emu core %5lld us/frame (incl. audio wait) | render core %5lu us/frame (draw %5lu)",
                     frames / sec, rlog_frames / sec, rlog_dropped, rlog_frames ? (float)rlog_bands / rlog_frames : 0.f, emu_us / frames,
                     rlog_frames ? render_us / rlog_frames : 0, rlog_frames ? s9x_render_cycles / 240 / rlog_frames : 0);
            t_report = e1; frames = 0; emu_us = audio_us = 0; rlog_frames = rlog_dropped = rlog_bands = 0; render_us = 0; s9x_render_cycles = 0;
        }
    }
    if (!demo) sram_flush(rom, true);
    while (render_busy) vTaskDelay(1);
    free(pcm);
    music_game_live(false);
    return result;
}

/* attract card at the top of each demo cycle */
static bool cycle_card(void)
{
    char games[32]; snprintf(games, sizeof games, "%d GAMES ON BOARD", nroms);
    music_start(0);
    for (int frame = 0; frame < 60 * 18; frame++) {
        ui_clear(UI_BLACK);
        festive_confetti(frame);
        festive_papel_picado(frame);
        ui_text_scaled(128 - 5 * 12, 36, "FIESTA", (frame >> 3) & 1 ? CUBE(5,5,0) : CUBE(5,1,3), 3);   /* 5 letters wide: S.F.E.S. below */
        ui_text_center(66, "SUPER FIESTA", UI_WHITE);
        ui_text_center(78, "ENTERTAINMENT SYSTEM", UI_WHITE);
        ui_text_center(94, "SAN ANTONIO 2027", UI_YELLOW);
        festive_dancers(frame, 160);
        ui_text_center(176, games, (frame >> 4) & 1 ? UI_WHITE : UI_GREEN);
        ui_text_center(196, "grab a controller to play", UI_GREY);
        ui_text_center(208, "or just watch the show", UI_GREY);
        ui_tick((frame & 1) == 0);
        if (pad_edges()) { music_stop(); return true; }
    }
    music_stop();
    return false;
}

static void demo_loop(void)
{
    serial_demo = false;
    if (!nroms) { vTaskDelay(pdMS_TO_TICKS(1000)); return; }
    int first = demo_next(nroms - 1);
    int i = demo_lock >= 0 ? demo_lock : first;
    display_set_backlight(BACKLIGHT_DEMO);
    ble_pad_scan_rate(false);   /* unattended: the radio listens 3 % of the time */
    for (;;) {
        if (demo_lock < 0 && i == first && cycle_card()) break;
        if (run_game(i, true) == GAME_DEMO_EXIT) break;
        i = demo_next(i);
        if (demo_lock >= 0) demo_set_lock(i);
    }
    display_set_backlight(BACKLIGHT_PLAY);
    ble_pad_scan_rate(true);
}

/* ---- console + BOOT button from a timer: keys reach the pad, and a stuck emulator gets noticed ---- */
static volatile uint32_t frames_total;
static void watchdog(void *arg)
{
    uint8_t c;
    while (usb_serial_jtag_read_bytes(&c, 1, 0) == 1) serial_push(c);
}

static void app_task(void *arg)
{
    settings_load();
    saves_init();
    log_heap("app start");
    music_start(0);
    splash_run();
    int sel = 0;
    bool have_pad = controller_screen(true);
    for (;;) {
        if (!have_pad || sel < 0) { demo_loop(); have_pad = true; sel = 0; }
        sel = picker(sel);
        if (sel < 0) continue;
        game_result_t r = run_game(sel, false);
        if (r == GAME_IDLE) sel = -1;
    }
}

void app_main(void)
{
    medal_init();
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&usb);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { ESP_ERROR_CHECK(nvs_flash_erase()); ret = nvs_flash_init(); }
    ESP_ERROR_CHECK(ret);
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
    Settings.InterpolatedSound = false;

    core_init();
    MUST(S9xInitDisplay());   /* the framebuffer wants the biggest contiguous block: before BLE carves the heap up */
    ble_pad_init();
    log_heap("with BLE");
    MUST(S9xInitMemory());
    MUST(S9xInitAPU());
    MUST(S9xInitSound(0, 0));
    MUST(S9xInitGFX());
    ui_init();
    audio_init(SAMPLE_RATE, SAMPLE_RATE / 60);   /* four frames of DMA queue: 67 ms of slack, 8.5 KB of internal RAM */
    rlog_init();
    render_init();
    log_heap("before tasks");
    /* the render task's stack lives in PSRAM: it never writes flash, and internal RAM is spoken for */
    MUST(xTaskCreatePinnedToCoreWithCaps(render_task, "render", 12288, NULL, 4, (TaskHandle_t *)&render_task_handle, 0, MALLOC_CAP_SPIRAM) == pdPASS);
    MUST(xTaskCreatePinnedToCore(app_task, "app", 16384, NULL, 5, NULL, 1) == pdPASS);
    esp_timer_create_args_t w = { .callback = watchdog, .name = "console" };
    esp_timer_handle_t wh;
    ESP_ERROR_CHECK(esp_timer_create(&w, &wh));
    ESP_ERROR_CHECK(esp_timer_start_periodic(wh, 50000));
}
