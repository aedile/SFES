# S.F.E.S. — Super Fiesta Entertainment System

A SNES emulator for the Waveshare **ESP32-S3-Touch-LCD-1.85**: dual-core
ESP32-S3 at 240 MHz, 8 MB PSRAM, 16 MB flash, a 360x360 ST77916 panel on QSPI,
a PCM5101 DAC with a speaker, a micro SD slot, touch, IMU and a battery. The
SNES sibling of [NESTOR](https://github.com/aedile/NESTOR), the NES medal.

*Status: a working spike.* One ROM embedded in the app runs with picture and
(optionally) sound; keys typed into the serial monitor act as the pad. No
controller, no picker, no SD card, no saves yet. The numbers so far are in
"Performance" below.

## Building

Everything runs in Espressif's Docker image; the host only needs `esptool`
(Homebrew) to flash.

```sh
cp your/game.zip roms/           # .zip with an .sfc/.smc inside, or a bare .sfc (roms/ is ignored by git)
./build.sh                       # idf.py build in espressif/idf:v5.3.4 -> build_docker/
./flash.sh [port]                # bootloader, partition table, app
tools/monitor.py [secs]          # reset and print the serial log
```

The default ROM is `Legend of Zelda, The_ A Link to the Past.zip`; pick another
with `./build.sh -DROM="name.zip" build`. Build-time knobs (CMake cache
variables, so they stick until changed):

| Knob | Default | Meaning |
|---|---|---|
| `SOUND` | 0 | 1 plays through the PCM5101; the blocking I2S write then paces emulation |
| `FRAMESKIP` | -1 | -1 auto (a frame that overruns drops the next draw), else fixed skip count |
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

## Performance

Zelda: A Link to the Past attract sequence, every 300 emulated frames, no sound:

| Segment | Emulation per frame | Push per drawn frame |
|---|---|---|
| Title | 9.9 ms | 4.3 ms |
| Intro | 12.1 ms | 4.3 ms |
| Attract play | 14.7 to 18.3 ms | 4.3 ms |

Budget is 16.7 ms. Title and intro run at full speed; gameplay is at 70 to 90
percent with frameskip. Full history and what was tried: see the commit log.

## Layout

```
main/            app: ROM load, emulator loop, serial pad, profiling
components/
  display/       ST77916 QSPI driver (Espressif, via Waveshare's demo) + strip push + TCA9554 reset
  audio/         PCM5101 on I2S
  snes9x/        Snes9x 2005 as carried in retro-go
tools/           ROM extractor, serial monitor, bench script
```

## Licensing

Code written for this project is MIT. The SNES core is Snes9x, which is not
MIT; see `LICENSING.md`. ROMs are not included.

## Credits

Snes9x 2005 by the Snes9x team, as carried in [retro-go](https://github.com/ducalex/retro-go)
by ducalex. `esp_lcd_st77916` by Espressif, via Waveshare's board demo.
ESP-IDF by Espressif. Built with Claude Code.
