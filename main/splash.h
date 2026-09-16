/* splash.h - boot splash: the cold open, then fireworks over the Tower of the Americas. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
void splash_run(void);   /* ~20 s, or until splash_skip_requested() says so */
const uint8_t *splash_cover(const char *short_name, int *w, int *h);   /* main.c: box art of the game whose short name matches */
bool splash_skip_requested(void);
