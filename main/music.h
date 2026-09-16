/* music.h - menu music: an SPC played by the emulated sound chip while no game is running. */
#pragma once
#include <stdbool.h>
void music_start(int track);
bool music_tick(void);         /* one frame of music if playing: returns true when the DAC paced us */
void music_stop(void);
bool music_playing(void);
void music_game_live(bool live);   /* a game owns the APU: music_start is a no-op while true */
