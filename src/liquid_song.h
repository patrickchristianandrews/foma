/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LIQUID_SONG_H
#define LIQUID_SONG_H
#include <stdint.h>

#define LW_SECTIONS 5
#define LW_TRACKS 8
#define LW_STEPS 16
#define LW_ARRANGEMENT 8
#define LW_REST 255

typedef struct {
    int8_t root;
    uint8_t progression[LW_SECTIONS][4];
    uint8_t arrangement[LW_ARRANGEMENT];
} LiquidHarmony;

uint8_t liquid_song_generate(uint8_t style, uint32_t seed,
    uint8_t pattern[LW_SECTIONS][LW_TRACKS][LW_STEPS], LiquidHarmony *harmony);
void liquid_song_legacy_harmony(uint8_t style, LiquidHarmony *harmony);

#endif
