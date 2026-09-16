/* core.h - the ROM table in the 'roms' partition and loading one into the single snes9x machine. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct __attribute__((packed)) { char name[48]; uint32_t off, size; uint32_t art_off; uint16_t art_w, art_h; } rom_entry_t;
extern const rom_entry_t *roms;
extern int nroms;

void core_init(void);              /* reads the table, sizes Memory.ROM for the biggest game; before S9xInitMemory */
bool core_load(int idx);           /* copies the ROM in, LoadROM + reset; false = the core rejected it */
const uint8_t *core_art(int idx, int *w, int *h);   /* box art bitmap in flash, NULL if none */
void short_name(const char *in, char *out, size_t n);   /* "Super Mario World (USA)" -> "Super Mario World" */
