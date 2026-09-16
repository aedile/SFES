# Licensing

S.F.E.S. is a hobby project. This file says what is in the repository, who
owns it, and what you may do with it. Nothing here is legal advice.

## The code written for this project: MIT

Everything under `main/`, `components/audio`, `tools/`,
`components/display/display.c`, `components/display/display.h` and the build
files is Copyright (c) 2026 Jesse Castro and released under the MIT license
(`LICENSE`).

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

## The display driver: Apache-2.0

`components/display/esp_lcd_st77916.c` and `.h` are Espressif's
`esp_lcd_st77916` component (Apache-2.0), copied from Waveshare's
ESP32-S3-Touch-LCD-1.85 demo. `st77916_init_new.c` is the panel init table
from that same demo.

## Not included

ROMs belong to their owners and are ignored by git (`roms/`).
