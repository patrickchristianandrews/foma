/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include "liquid_song.h"

/* The website generator at source blob 476a8b9bbc20561b90d4558776bf445c2cd6c1ef.
 * Each FNV-1a fingerprint covers BPM, root, harmony, arrangement and all steps. */
static const struct {
    uint8_t style;
    uint32_t seed, expected;
} cases[] = {
    {0, 0x0u, 0xde799a4bu},
    {0, 0x1u, 0xa693e4d7u},
    {0, 0xdeadbeefu, 0xfa440b01u},
    {0, 0xffffffffu, 0x5961de3cu},
    {1, 0x0u, 0x7b46896du},
    {1, 0x1u, 0x5e799b62u},
    {1, 0xdeadbeefu, 0x9682bba8u},
    {1, 0xffffffffu, 0x26baacb6u},
    {2, 0x0u, 0x36ee8cafu},
    {2, 0x1u, 0x7d829b72u},
    {2, 0xdeadbeefu, 0xdcce2d00u},
    {2, 0xffffffffu, 0xd953fa1eu},
    {3, 0x0u, 0x255de554u},
    {3, 0x1u, 0xa93b21deu},
    {3, 0xdeadbeefu, 0x5f92e85cu},
    {3, 0xffffffffu, 0x794d3f37u},
};

static uint32_t hash_bytes(uint32_t hash, const uint8_t *bytes, unsigned length) {
    for (unsigned i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

int main(void) {
    for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        uint8_t pattern[LW_SECTIONS][LW_TRACKS][LW_STEPS];
        LiquidHarmony harmony;
        uint8_t header[2];
        header[0] = liquid_song_generate(cases[i].style, cases[i].seed, pattern, &harmony);
        header[1] = (uint8_t)harmony.root;
        uint32_t hash = hash_bytes(2166136261u, header, sizeof(header));
        hash = hash_bytes(hash, (const uint8_t *)harmony.progression, sizeof(harmony.progression));
        hash = hash_bytes(hash, harmony.arrangement, sizeof(harmony.arrangement));
        hash = hash_bytes(hash, (const uint8_t *)pattern, sizeof(pattern));
        if (hash != cases[i].expected) {
            fprintf(stderr, "style %u, seed %08x: expected %08x, got %08x\n",
                cases[i].style, (unsigned)cases[i].seed, (unsigned)cases[i].expected, (unsigned)hash);
            return 1;
        }
    }
    puts("All 16 browser-generator parity vectors passed.");
    return 0;
}
