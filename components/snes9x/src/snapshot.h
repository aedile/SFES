#pragma once
#include <stdio.h>

#include <stdbool.h>
#include <stdint.h>

bool S9xSaveState(const char *filename);
bool S9xLoadState(const char *filename);
bool S9xSaveStateFile(FILE *fp);   /* SFES: to/from an open stream */
bool S9xLoadStateFile(FILE *fp);
