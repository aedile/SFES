#include "core.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "snes9x.h"
#include "rlog.h"

static const char *TAG = "CORE";
const rom_entry_t *roms;
int nroms;
static const esp_partition_t *part;
static const uint8_t *image;   /* the packed image, memory-mapped: art is read straight from flash */
static size_t rom_max;

void core_init(void)
{
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "roms");
    assert(part);
    struct { char magic[4]; uint32_t count; } head;
    ESP_ERROR_CHECK(esp_partition_read(part, 0, &head, sizeof head));
    if (memcmp(head.magic, "SFES", 4) != 0) { ESP_LOGE(TAG, "no ROM image in the roms partition"); nroms = 0; }
    else nroms = head.count;
    rom_entry_t *tab = calloc(nroms + 1, sizeof *tab);
    ESP_ERROR_CHECK(esp_partition_read(part, 8, tab, nroms * sizeof *tab));
    roms = tab;
    size_t used = 8 + nroms * sizeof *tab;
    for (int i = 0; i < nroms; i++) {
        if (tab[i].size > rom_max) rom_max = tab[i].size;
        size_t e = tab[i].art_off ? tab[i].art_off + (size_t)tab[i].art_w * tab[i].art_h : 0;
        if (e > used) used = e;
        ESP_LOGI(TAG, "  [%d] %-44s %5lu KB art %ux%u", i, tab[i].name, tab[i].size / 1024, tab[i].art_w, tab[i].art_h);
    }
    esp_partition_mmap_handle_t h;
    ESP_ERROR_CHECK(esp_partition_mmap(part, 0, used ? used : 4096, ESP_PARTITION_MMAP_DATA, (const void **)&image, &h));
    /* one ROM buffer for the life of the app; S9xInitMemory keeps a buffer it finds already set */
    Memory.ROM = heap_caps_malloc(rom_max + 0x10000 + 0x200, MALLOC_CAP_SPIRAM);
    Memory.ROM_AllocSize = rom_max;
    assert(Memory.ROM);
    ESP_LOGI(TAG, "%d ROMs, largest %lu KB", nroms, (unsigned long)rom_max / 1024);
}

bool core_load(int idx)
{
    if (idx < 0 || idx >= nroms) return false;
    const rom_entry_t *e = &roms[idx];
    if (Memory.ROM_Offset) { Memory.ROM -= Memory.ROM_Offset; Memory.ROM_Offset = 0; }
    ESP_ERROR_CHECK(esp_partition_read(part, e->off, Memory.ROM, e->size));
    Memory.ROM_AllocSize = e->size;
    if (!LoadROM(NULL)) { ESP_LOGE(TAG, "%s: LoadROM failed", e->name); return false; }
    memset(Memory.SRAM, 0, SRAM_SIZE);
    S9xReset();
    S9xSetPlaybackRate(Settings.SoundPlaybackRate);
    rlog_reset();   /* the render side's VRAM copy and tile cache are stale */
    ESP_LOGI(TAG, "loaded %s: %s, %s, SRAM %u bytes", e->name, Memory.ROMName, Memory.HiROM ? "HiROM" : "LoROM", Memory.SRAMSize ? Memory.SRAMMask + 1 : 0);
    return true;
}

const uint8_t *core_art(int idx, int *w, int *h)
{
    if (idx < 0 || idx >= nroms || !roms[idx].art_off) { *w = 134; *h = 96; return NULL; }
    *w = roms[idx].art_w; *h = roms[idx].art_h;
    return image + roms[idx].art_off;
}

void short_name(const char *in, char *out, size_t n)
{
    const char *p = strstr(in, " (");
    const char *q = strstr(in, " [");
    if (q && (!p || q < p)) p = q;
    size_t len = p ? (size_t)(p - in) : strlen(in);
    if (len >= n) len = n - 1;
    memcpy(out, in, len);
    out[len] = 0;
}
