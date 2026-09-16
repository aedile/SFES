# S.F.E.S. — Super Fiesta Entertainment System

A SNES emulator for the Waveshare **ESP32-S3-Touch-LCD-1.85**: dual-core
ESP32-S3 at 240 MHz, 8 MB PSRAM, 16 MB flash, a 360x360 ST77916 panel on QSPI,
a PCM5101 DAC with a speaker, a micro SD slot, touch, IMU and a battery. The
SNES sibling of [NESTOR](https://github.com/aedile/NESTOR), the NES medal.

*Status: a working spike.* Every ROM in `roms/` is packed into a flash
partition; the BOOT button (or `n` on the serial console) reboots into the
next one. Picture and sound work; keys typed into the serial monitor act as
the pad. No controller, no picker, no SD card, no saves yet. Numbers are in
"Performance" below.

## Building

Everything runs in Espressif's Docker image; the host only needs `esptool`
(Homebrew) to flash.

```sh
cp your/games/*.zip roms/        # .zip with an .sfc/.smc inside, or bare .sfc/.smc (roms/ is ignored by git)
./build.sh                       # idf.py build in espressif/idf:v5.3.4 -> build_docker/ (packs roms/ too)
./flash.sh [port]                # bootloader, partition table, app, ROM image
tools/monitor.py [secs] [port] [noreset]   # print the serial log (resets the board unless told not to)
tools/keys.py <keys>             # type pad keys or 'n' (next ROM) into the console
```

Build-time knobs (CMake cache variables, so they stick until changed):

| Knob | Default | Meaning |
|---|---|---|
| `SOUND` | 0 | 1 plays through the PCM5101; the blocking I2S write then paces emulation |
| `FRAMESKIP` | 0 | frames left out of the render log between logged ones (the render core drops what it cannot keep up with anyway) |
| `MEM_LAYOUT` | 1 | 0 all PSRAM, 1 framebuffer + depth buffer in internal RAM, 2 CPU RAMs internal (hangs the board, do not use) |
| `APU_OFF` | 0 | 1 skips SPC700 emulation, for measuring only |

Serial pad: w/a/s/d, j = B, k = A, u = Y, i = X, o = L, p = R, q = Start,
e = Select. Every 300 emulated frames the log prints fps and where the time
went.

## Hardware notes

| Function | Pins |
|---|---|
| LCD, ST77916 QSPI | SCK 40, D0 46, D1 45, D2 42, D3 41, CS 21, TE 18, backlight 5, reset on TCA9554 bit 1 |
| I2C (TCA9554 0x20, QMI8658, PCF85063) | SCL 10, SDA 11 |
| Touch, CST816 | SDA 1, SCL 3, INT 4, reset on TCA9554 bit 0 |
| I2S to PCM5101 | BCLK 48, LRCK 38, DIN 47 |
| SD (SPI) | SCK 14, MISO 16, MOSI 17, CS on TCA9554 bit 2 |
| Buttons | BOOT 0, battery key 6, battery control 7, battery ADC 8 |

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
main/            app: ROM load from the roms partition, emulator loop, serial pad, watchdog, profiling
components/
  display/       ST77916 QSPI driver (Espressif, via Waveshare's demo) + strip push + TCA9554 reset
  audio/         PCM5101 on I2S
  snes9x/        Snes9x 2005 as carried in retro-go, split into emulator and render sides (rlog.c, render.c)
tools/           ROM packer, serial monitor, key sender, bench script
partitions.csv   nvs, phy, app 2 MB, roms 13.9 MB
```

## Licensing

Code written for this project is MIT. The SNES core is Snes9x, which is not
MIT; see `LICENSING.md`. ROMs are not included.

## Credits

Snes9x 2005 by the Snes9x team, as carried in [retro-go](https://github.com/ducalex/retro-go)
by ducalex. `esp_lcd_st77916` by Espressif, via Waveshare's board demo.
ESP-IDF by Espressif. Built with Claude Code.
