/* saves.h - battery RAM and save states in the dedicated 'saves' NVS partition, keyed by ROM name. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void saves_init(void);
bool saves_has_sram(const char *rom);
bool saves_has_state(const char *rom);
bool saves_load_sram(const char *rom, uint8_t *buf, size_t len);
bool saves_store_sram(const char *rom, const uint8_t *buf, size_t len);
bool saves_load_state(const char *rom);    /* into the running snes9x machine */
bool saves_save_state(const char *rom);    /* from the running snes9x machine */
