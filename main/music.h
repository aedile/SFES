/* music.h - menu music. ponytail: no SPC player yet; music_tick() reports whether it paced the frame. */
#pragma once
#include <stdbool.h>
void music_start(int track);
bool music_tick(void);         /* one frame of music if playing: returns true when the DAC paced us */
void music_stop(void);
bool music_playing(void);
