# S.F.E.S. — Super Fiesta Entertainment System

A SNES emulator for the Waveshare **ESP32-S3-Touch-LCD-1.85**: dual-core
ESP32-S3 at 240 MHz, 8 MB PSRAM, 16 MB flash, a 360x360 ST77916 panel on QSPI,
a PCM5101 DAC with a speaker, a micro SD slot, touch, IMU and a battery. The
SNES sibling of [NESTOR](https://github.com/aedile/NESTOR), the NES medal.

## What it does

* Runs SNES games straight out of flash at a locked 60 fps with sound; the
  panel gets 30 to 60 of those frames depending on the game (see
  "Performance").
* Pairs with a BLE HID gamepad (Xbox Wireless Controller), remembers it,
  reconnects on boot.
* Box-art picker, in-game menu with save states and battery saves.
* **Demo mode** when nobody is playing: an attract card, then every game's own
  attract sequence in turn. Lock it to one game with a button, or leave a game
  out of the rotation.
* Works without a controller: the two onboard buttons skip, lock, mute and
  power off.
* A splash worth watching: FIESTA races past, hard cuts to box art with
  ENTERTAINMENT scrolling through, then fireworks over the Tower of the
  Americas, with Super Mario World's overworld theme played by the emulated
  sound chip from an SPC.

## Building

Everything runs in Espressif's Docker image; the host only needs `esptool`
(Homebrew) to flash.

```sh
cp your/games/*.zip roms/        # .zip with an .sfc/.smc inside, or bare .sfc/.smc (roms/ is ignored by git)
tools/fetch_art.py               # box art -> roms/art/<name>.png (libretro-thumbnails)
tools/fetch_music.py             # menu music -> roms/music/menu.spc (Josh W's SPC archive)
./build.sh                       # idf.py build in espressif/idf:v5.3.4 -> build_docker/ (packs roms/ too)
./flash.sh [port]                # bootloader, partition table, app, ROM image
tools/monitor.py [secs] [port] [noreset]   # print the serial log (resets the board unless told not to)
tools/drive.py dk log:10 m ...   # script the serial pad and print the log
```

The build packs every ROM and its cover into a flash image for the `roms`
partition. Nothing is hard-coded: add a zip, rebuild, and it appears in the
picker. Knobs: `-DMEM_LAYOUT` (1 framebuffer internal, 2 depth buffer too, 0
all PSRAM) and `-DDEMO_SECONDS`.

Bench testing without a controller: keys typed into the serial monitor act as
a pad (w/a/s/d, j = B, k = A, u = Y, i = X, o = L, p = R, q = Start,
e = Select, m = menu; n and l stand in for the PWR and BOOT buttons, x jumps to
demo mode).

## Which controllers work

BLE HID only, as with NESTOR: the Xbox Wireless Controller (2016 on) is the
tested one. 8BitDo, PS4/PS5 and Switch pads speak Bluetooth Classic, which
this chip cannot hear.

## Controls

Gamepad, Nintendo positions (on an Xbox pad, B is SNES A, A is SNES B, X is
SNES Y, Y is SNES X):

| Screen | Controls |
|---|---|
| Picker | D-pad left/right, A play, B toggle demo rotation, Select mute, MENU = controller screen |
| Game | SELECT+START together opens the menu: Resume, Save state, Load state, Reset, Mute, Return to picker, Controller |
| Controller screen | Back, Forget this controller |

Medal buttons, no controller needed:

| Button | Short press | Hold |
|---|---|---|
| PWR | next game (demo), move right (picker), leave game | 2 s: power off |
| BOOT | lock/unlock the demo to the current game | 3 s: mute, 10 s: forget the controller |

## The flow

Splash (any button skips) → controller screen → picker → game. If no pad
connects within 30 s, or a connected pad is idle for 3 minutes, demo mode
starts: an 18 s attract card, then each game for 2 minutes with a title card
between. Any pad button returns to the picker. Settings in NVS: the paired
controller, demo lock, per-game demo exclusion, mute.

## Hardware notes

| Function | Pins |
|---|---|
| LCD, ST77916 QSPI | SCK 40, D0 46, D1 45, D2 42, D3 41, CS 21, TE 18, backlight 5, reset on TCA9554 bit 1 |
| I2C (TCA9554 0x20, QMI8658, PCF85063) | SCL 10, SDA 11 |
| Buttons, battery | BOOT 0, power key 6, battery rail hold 7, battery ADC 8 |
| Touch, CST816 | SDA 1, SCL 3, INT 4, reset on TCA9554 bit 0 |
| I2S to PCM5101 | BCLK 48, LRCK 38, DIN 47 |
| SD (SPI) | SCK 14, MISO 16, MOSI 17, CS on TCA9554 bit 2 |

Waveshare ships two panel revisions with different init tables; the driver
reads ID register 04h and picks (00 7F 7F 7F = the driver default, 00 02 7F 7F
= the newer table in `st77916_init_new.c`).

The SNES 256x224 frame is pushed 1:1, centred, in 16-row strips that are
byte-swapped into DMA buffers while the previous strip is on the wire.

## Two cores

Emulation (65816, SPC700, DSP mixing) runs on core 1; rendering on core 0.
snes9x renders lazily, in bands of scanlines whenever a PPU register write
changes what the next lines would look like. Here the emulator side
(`rlog.c`) records a snapshot of the PPU state, OAM, CGRAM and palette at each
of those points, plus the per-line scroll data, into a log; VRAM writes mark
16-byte blocks that are copied across at hand-off. The render side (`gfx.c`,
`tile.c`, `clip.c`, `render.c`) is compiled with its own copies of the PPU
globals (`-DPPU=render_PPU` and friends), replays the log a frame behind, and
pushes finished strips between bands. Frames the render core cannot take are
dropped. The emulator core is therefore never waiting on the panel or the
renderer, and audio stays continuous.

Internal RAM is the tight resource once Bluetooth is up: the framebuffer
(224 rows plus two of slack for the hi-res modes) is allocated first and kept
internal; the depth buffer, NimBLE's heap and the render task's stack live in
PSRAM. About 25 KB of internal RAM is left at runtime.

## Performance

Emulator core at a paced 60 Hz, no sound, per 300 frames:

| Game | Emulator core per frame | Render core per frame | Rendered fps |
|---|---|---|---|
| Zelda: A Link to the Past, title / attract play | 7.5 ms | 8.7 / 26 ms | 58 / 30 |
| Donkey Kong Country, intro / attract | 4 to 6 ms (+2 ms mixing with sound) | 12 / 30 ms | 60 / 30 |
| Street Fighter II | 4.6 ms | 13 to 18 ms | 33 to 46 |
| Super Mario Kart | 5 ms | 9 to 17 ms | 41 to 60 |
| Super Mario World, title / play | 6 to 8 ms | 9 / 23 ms | 60 / 30 |

Game logic and sound run at full speed in every case; what varies is how many
of those frames the panel gets. Star Fox needs the SuperFX chip, which
Snes9x 2005 does not emulate.

## Layout

```
main/            app: flow, picker, menus, demo, splash, festive drawing, saves, SPC music, medal buttons
components/
  display/       ST77916 QSPI driver (Espressif, via Waveshare's demo) + strip push + TCA9554 reset
  audio/         PCM5101 on I2S
  ble_pad/       NimBLE scanning, HID report parsing, pad mapping, NVS pairing (from NESTOR)
  esp_hid/       ESP-IDF's HID host, vendored with NimBLE fixes (from NESTOR)
  snes9x/        Snes9x 2005 as carried in retro-go, split into emulator and render sides (rlog.c, render.c)
tools/           ROM/art packer, art and music fetchers, serial monitor and driver
partitions.csv   nvs, phy, app 2 MB, roms 11.4 MB, saves (NVS) 3 MB
```

## Licensing

Code written for this project is MIT. The SNES core is Snes9x, which is not
MIT; see `LICENSING.md`. ROMs are not included.

## Credits

Snes9x 2005 by the Snes9x team, as carried in [retro-go](https://github.com/ducalex/retro-go)
by ducalex. `esp_lcd_st77916` by Espressif, via Waveshare's board demo.
ESP-IDF by Espressif. font8x8 by Daniel Hepper. Box art via
libretro-thumbnails. SPC via Josh W's archive. The BLE pad, HID host, UI,
splash and festive drawing came over from NESTOR. Built with Claude Code.
