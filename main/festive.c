#include "festive.h"
#include <stdbool.h>

#define L 0
#define R 256
#define CX 128
#define festive_px ui_px

const uint16_t fiesta_colours[6] = { CUBE(5,1,3), CUBE(0,5,4), CUBE(5,5,0), CUBE(1,5,1), CUBE(5,3,0), CUBE(3,0,4) };

void festive_papel_picado(int frame)
{
    static const int8_t sway[8] = { 0, 1, 2, 1, 0, -1, -2, -1 };
    for (int x = L; x < R; x++) {
        int d = x - CX;
        festive_px(x, 6 + (d * d) / 1400, UI_GREY);   /* the string sags in the middle */
    }
    for (int i = 0; i < 8; i++) {
        int x = L + 6 + i * 30 + sway[((frame >> 3) + i) & 7];
        int d = x + 9 - CX, y = 7 + (d * d) / 1400;
        uint16_t c = fiesta_colours[i % 6];
        ui_fill(x, y, 18, 16, c);
        for (int k = 0; k < 4; k++) ui_fill(x + 8, y + 3 + k * 3, 2, 1, UI_BLACK);   /* punched pattern */
        ui_fill(x + 5, y + 6, 8, 1, UI_BLACK); ui_fill(x + 5, y + 10, 8, 1, UI_BLACK);
        ui_fill(x, y + 13, 18, 3, c);
        for (int k = 0; k < 18; k += 3) festive_px(x + k, y + 16, c);   /* scalloped edge */
    }
}

void festive_confetti(int frame)
{
    static const int8_t sway[16] = { 0, 1, 1, 2, 2, 2, 1, 1, 0, -1, -1, -2, -2, -2, -1, -1 };
    for (int i = 0; i < 48; i++) {
        uint32_t h = (uint32_t)(i + 1) * 2654435761u;
        int speed = 1 + (h & 1), period = FB_LINES + 40;
        int y = (((h >> 8) % period) + frame * speed / 2) % period - 20;
        int x = L + (h >> 16) % (R - L) + sway[((frame >> 2) + i) & 15];
        uint16_t c = fiesta_colours[i % 6];
        festive_px(x, y, c); festive_px(x + 1, y, c);
        if (((frame >> 3) + i) & 1) { festive_px(x, y + 1, c); festive_px(x + 1, y + 1, c); }
    }
}

/* ---- dancers: 20x24 folklorico dancer (skirt swept to one side; mirrored for the other),
 * 16x24 mariachi (guitar strums). Letters index the colour key below. ---- */
static const char *const dancer[24] = {
    "....rryrr...........",
    "...rhhhhhr..........",
    "...hhhhhhh..........",
    "....kkkkk...........",
    "....kkkkk...........",
    ".....kkk............",
    "...wwwwwww..........",
    "..kwwwwwwwk.........",
    ".kk.wwwww.kk........",
    "k...wwwww...k.......",
    "....ppppp...........",
    "...ppppppp..........",
    "..pppppppppp........",
    ".pppppppppppppp.....",
    "pppppppppppppppppp..",
    "ppgppppgppppgppppgp.",
    "pppppppppppppppppppp",
    "ppppppppppppppppppp.",
    ".pppppppppppppppp...",
    "..pppppppppppp......",
    "....k...k...........",
    "....k...k...........",
    "...dd...dd..........",
    "....................",
};
static const char *const mariachi[24] = {
    "......ssss......",
    ".....ssssss.....",
    "..ssssssssssss..",
    ".syyyyyyyyyyyys.",
    "..ssssssssssss..",
    ".....kkkkkk.....",
    ".....kkkkkk.....",
    ".....kkhhkk.....",
    "......kkkk......",
    "....ddwwwwdd....",
    "...dddwddwddd...",
    "..kddddddddddk..",
    "..k.ddddddddd.k.",
    "....dddttttd....",
    "...ttttttttttt..",
    "...tttttdttttt..",
    "....ttttttttt...",
    ".....ddddddd....",
    ".....ddd.ddd....",
    ".....ddd.ddd....",
    ".....ddd.ddd....",
    ".....ddd.ddd....",
    "....dddd.dddd...",
    "................",
};

static uint16_t key(char ch, uint16_t skirt)
{
    switch (ch) {
    case 'k': return CUBE(5,4,2);   /* skin */
    case 'h': return CUBE(1,0,0);   /* hair, moustache */
    case 'r': return CUBE(5,0,0);   /* flowers */
    case 'y': return CUBE(5,5,0);   /* sombrero braid */
    case 'w': return UI_WHITE;
    case 'p': return skirt;
    case 'g': return CUBE(1,5,1);
    case 'd': return CUBE(1,1,3);   /* charro suit, shoes: navy */
    case 's': return CUBE(4,3,1);   /* sombrero */
    case 't': return CUBE(5,4,1);   /* guitar */
    }
    return 0;
}

#define SCALE 2
static void sprite(const char *const *rows, int w, int x0, int y0, bool mirror, int shift_from, int shift_by, uint16_t skirt)
{
    for (int y = 0; y < 24; y++) {
        int dx = (shift_from && y >= shift_from && y < shift_from + 4) ? shift_by : 0;
        for (int x = 0; x < w; x++) {
            char ch = rows[y][x];
            if (ch == '.') continue;
            int sx = x0 + ((mirror ? w - 1 - x : x) + dx) * SCALE, sy = y0 + y * SCALE;
            ui_fill(sx, sy, SCALE, SCALE, key(ch, skirt));
        }
    }
}

void festive_dancers(int frame, int floor_y)
{
    int beat = (frame >> 3) & 3;                 /* ~2 Hz step */
    int bounce = beat == 1 || beat == 3 ? -3 : 0;
    bool swing = (frame >> 4) & 1;               /* skirt / strum alternates ~every half second */
    int top = floor_y - 24 * SCALE;
    /* stage: dancer, mariachi, dancer, mariachi across the width */
    sprite(dancer, 20, 12, top + bounce, swing, 0, 0, CUBE(5,1,3));
    sprite(mariachi, 16, 70, top - bounce, false, 13, swing ? 1 : 0, 0);
    sprite(dancer, 20, 134, top + bounce, !swing, 0, 0, CUBE(0,5,4));
    sprite(mariachi, 16, 196, top - bounce, true, 13, swing ? 0 : 1, 0);
    ui_fill(L, floor_y, R - L, 1, CUBE(4,3,1));  /* the floor */
}
