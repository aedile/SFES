/* festive.h - the Fiesta dressing shared by the splash, controller screen and picker. */
#pragma once
#include <stdint.h>
#include "ui.h"
extern const uint16_t fiesta_colours[6];
void festive_papel_picado(int frame);          /* string of flags across the top, swaying */
void festive_confetti(int frame);              /* slow drifting confetti over the whole frame */
void festive_dancers(int frame, int floor_y);  /* folklorico dancers and mariachis, feet on floor_y */
