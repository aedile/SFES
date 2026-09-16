# Licensing

S.F.E.S. is a hobby project. This file says what is in the repository, who
owns it, and what you may do with it. Nothing here is legal advice.

## The code written for this project: MIT

Everything under `main/`, `components/audio`, `components/ble_pad`, `tools/`,
`components/display/display.c`, `components/display/sfes_display.h` and the
build files is Copyright (c) 2026 Jesse Castro and released under the MIT
license (`LICENSE`). `components/esp_hid` is ESP-IDF's HID host component
(Apache-2.0), vendored via NESTOR with NimBLE fixes.

## The SNES core: Snes9x 2005

`components/snes9x` is Snes9x 2005 as carried in
[retro-go](https://github.com/ducalex/retro-go). Its `LICENSE` is the
CATSFC/ndssfc GPL-2.0 notice on top of the original Snes9x license, which
permits non-commercial use only. Treat the component as **non-commercial,
copyleft**: a distributed firmware image is bound by those terms, and this
project is not sold.

Modifications for this port (September 2026):

| File | Change |
|---|---|
| `src/port.h` | added `stdio.h`, `stdlib.h`, `stdint.h` includes (retro-go supplied them through `rg_system.h`) |
| `src/ppu.h` | `FLUSH_REDRAW` logs a band on the emulator side (`rlog_flush`) instead of rendering; VRAM writes mark `sfes_vram_dirty` instead of the tile cache |
| `src/cpuexec.c` | frame start/end and per-line hooks call `rlog_*` |
| `src/gfx.c` | `S9xSetLineData` (render side loads the logged per-line data); `S9xUpdateScreen` timed in CPU cycles |
| `src/gfx.h` | declaration of the above |
| `src/rlog.h`, `src/rlog.c`, `src/render.c` | new: the two-core split |
| `src/snapshot.c`, `src/snapshot.h` | `S9xSaveStateFile` / `S9xLoadStateFile` take an open `FILE *` (states go to NVS) |

## The display driver: Apache-2.0

`components/display/esp_lcd_st77916.c` and `.h` are Espressif's
`esp_lcd_st77916` component (Apache-2.0), copied from Waveshare's
ESP32-S3-Touch-LCD-1.85 demo. `st77916_init_new.c` is the panel init table
from that same demo.

## Not included

ROMs belong to their owners and are ignored by git (`roms/`).
