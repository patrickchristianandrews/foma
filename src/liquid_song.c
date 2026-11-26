/* SPDX-License-Identifier: GPL-3.0-only */
/* Composition rules ported from RandomCandyLand.com/public/liquid-wave/index.html. */
#include "liquid_song.h"

#define STEP(i) (1u << (i))
#define QUARTERS 0x1111u
#define EVENS 0x5555u
#define BACKBEAT (STEP(4) | STEP(12))
#define OFFBEAT (STEP(2) | STEP(6) | STEP(10) | STEP(14))

typedef struct {
    uint8_t low, high;
    uint8_t chords[2][4];
    uint16_t kick, snare, hat, bass, chord;
} Style;

static const Style styles[] = {
    {98,114, {{0,5,2,6},{0,5,3,6}}, QUARTERS,BACKBEAT,EVENS,EVENS,OFFBEAT},
    {102,116,{{0,3,0,4},{0,3,6,3}}, STEP(0)|STEP(3)|STEP(6)|STEP(10),BACKBEAT,
        EVENS|STEP(3)|STEP(11),STEP(0)|STEP(3)|STEP(6)|STEP(8)|STEP(10)|STEP(12)|STEP(14),
        STEP(0)|STEP(6)|STEP(10)},
    {68,82,  {{0,4,5,3},{0,5,3,4}}, STEP(0)|STEP(8),STEP(8),QUARTERS,STEP(0)|STEP(8),STEP(0)},
    {120,128,{{0,0,5,6},{0,3,0,4}}, QUARTERS,BACKBEAT,OFFBEAT,OFFBEAT,OFFBEAT}
};
static const uint8_t arrangements[][LW_ARRANGEMENT] = {
    {0,1,2,1,2,3,2,4}, {0,1,1,2,3,2,2,4}, {0,2,1,2,1,3,2,4}, {0,1,2,3,1,2,3,4}
};

/* The website uses Mulberry32. Keep each draw in the website's order. */
static uint32_t next(uint32_t *state) {
    uint32_t t = (*state += 0x6D2B79F5u);
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return t ^ (t >> 14);
}

static uint8_t pick(uint32_t *state, uint8_t count) {
    return (uint8_t)(((uint64_t)next(state) * count) >> 32);
}

/* Probability in ten-thousandths, rounded up to the first excluded RNG value. */
static int chance(uint32_t *state, unsigned probability) {
    /* 2^32 = 429496 * 10000 + 7296. No 64-bit division on the FM-1. */
    uint32_t limit = probability * 429496u + (probability * 7296u + 9999u) / 10000u;
    return next(state) < limit;
}

static int vary(uint32_t *state, unsigned hits, unsigned step, unsigned keep, unsigned add) {
    return chance(state, hits & STEP(step) ? keep : add);
}

uint8_t liquid_song_generate(uint8_t style, uint32_t state,
        uint8_t pattern[LW_SECTIONS][LW_TRACKS][LW_STEPS], LiquidHarmony *harmony) {
    static const int8_t roots[] = {-4,-2,0,2,3,5};
    static const uint8_t energy[] = {40,72,100,61,31};
    static const uint8_t motif_notes[] = {0,1,2,3,4,5,7,9};
    static const uint8_t bass[] = {0,0,0,2,4,7};
    static const uint8_t chords[] = {0,0,2,4};
    static const uint8_t extra[] = {0,2,4,7,9};
    const Style *rules = &styles[style % 4];
    uint8_t bpm = rules->low + pick(&state, rules->high - rules->low + 1);
    harmony->root = roots[pick(&state, 6)];
    const uint8_t *home = rules->chords[pick(&state, 2)];
    for (unsigned s = 0; s < LW_SECTIONS; ++s)
        for (unsigned bar = 0; bar < 4; ++bar)
            harmony->progression[s][bar] = home[bar];
    for (unsigned bar = 1; bar < 4; ++bar)
        harmony->progression[2][bar] = 1 + pick(&state, 6);
    harmony->progression[3][0] = 1 + pick(&state, 6);
    harmony->progression[3][2] = 1 + pick(&state, 6);
    harmony->progression[3][3] = home[0];

    for (unsigned s = 0; s < LW_SECTIONS; ++s) {
        unsigned e = energy[s];
        uint8_t motif[5];
        for (unsigned i = 0; i < 5; ++i) motif[i] = motif_notes[pick(&state, 8)];
        unsigned lead = QUARTERS << pick(&state, 4);
        unsigned has_drums = 0;
        for (unsigned i = 0; i < LW_STEPS; ++i) {
            unsigned beat = 0;
            if (vary(&state,rules->kick,i,5800+38*e,400+9*e)) beat |= 1;
            if (vary(&state,rules->snare,i,4500+42*e,4*e)) beat |= 2;
            if (vary(&state,rules->hat,i,2200+65*e,10*e)) beat |= 4;
            pattern[s][0][i] = (uint8_t)beat;
            has_drums |= beat;
            pattern[s][1][i] = vary(&state,rules->bass,i,2500+68*e,8*e) ? bass[pick(&state,6)] : LW_REST;
            pattern[s][2][i] = vary(&state,rules->chord,i,3000+50*e,4*e) ? chords[pick(&state,4)] : LW_REST;
            pattern[s][3][i] = vary(&state,lead,i,2600+56*e,9*e) ? motif[(i+s)%5] : LW_REST;
            pattern[s][4][i] = vary(&state,STEP(0)|STEP(8),i,4000+36*e,200) ? chords[pick(&state,4)] : LW_REST;
            pattern[s][5][i] = vary(&state,EVENS,i,2000+50*e,600) ? motif[pick(&state,5)] : LW_REST;
            pattern[s][6][i] = chance(&state,600+10*e) ? extra[pick(&state,5)] : LW_REST;
            pattern[s][7][i] = chance(&state,400+6*e) ? extra[pick(&state,4)] : LW_REST;
        }
        if (!has_drums) pattern[s][0][0] = 1;
    }
    const uint8_t *order = arrangements[pick(&state,4)];
    for (unsigned i = 0; i < LW_ARRANGEMENT; ++i) harmony->arrangement[i] = order[i];
    return bpm;
}

void liquid_song_legacy_harmony(uint8_t style, LiquidHarmony *harmony) {
    const uint8_t *chords = styles[style % 4].chords[0];
    harmony->root = 0;
    for (unsigned s = 0; s < LW_SECTIONS; ++s)
        for (unsigned bar = 0; bar < 4; ++bar)
            harmony->progression[s][bar] = chords[bar];
    for (unsigned i = 0; i < LW_ARRANGEMENT; ++i) harmony->arrangement[i] = arrangements[0][i];
}
