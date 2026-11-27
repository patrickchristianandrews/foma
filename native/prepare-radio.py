#!/usr/bin/env python3
"""Prepare the pinned fm1-radio source for the experimental Wi-Fi target.
Preserves upstream copyright and license files; replaces only the competing mixer.
"""
import pathlib, shutil, sys
root=pathlib.Path(__file__).resolve().parent
src=pathlib.Path(sys.argv[1])/"app"
dest=root/"reference"
if dest.exists(): shutil.rmtree(dest)
dest.mkdir()
for name in ("third_party","net"):
    shutil.copytree(src/name,dest/name)
shutil.copyfile(src.parent/"LICENSE",dest/"LICENSE")
shutil.copyfile(src.parent/"LICENSING.md",dest/"LICENSING.md")
s=(src/"radio.c").read_text()
# Keep the real decoder, network buffering and resampler; not the radio's second synth or punch-in FX.
marker="/* ---- punch-in FX:"
assert marker in s
s=s[:s.index(marker)]
s=s.replace("fm1_ticks() / FM1_TICKS_PER_US","plat_ticks24() / PLAT_TICKS_PER_US")
s=s.replace("static int16_t *fx_hist;","")
start=s.index("    if (!fx_hist) {")
end=s.index("    return 0;",start)
s=s[:start]+s[end:]
# Avoid permanently reserving 80 KiB for AAC; its real decoder allocates on demand.
# The worldwide directory is not downloaded in this first integrated build.
s=s.replace("static uint8_t lin_buf[LIN_MAX];","static uint8_t *lin_buf;")
s=s.replace("net_buf = malloc(NET_RING);","net_buf = malloc(NET_RING);\n    lin_buf = malloc(LIN_MAX);")
s=s.replace("if (!net_buf || !pcm_buf) {","if (!net_buf || !pcm_buf || !lin_buf) {")
s=s.replace("free(net_buf); free(pcm_buf);","free(net_buf); free(pcm_buf); free(lin_buf); lin_buf=0;")
# Volatile playback state: shared between main loop and render.
s=s.replace("static uint8_t radio_st;","static volatile uint8_t radio_st;")
(dest/"radio_core.c").write_text(s)

# C99 inline functions need internal linkage in each decoder translation unit.
header=dest/"third_party/pvaac/getbits.h"
s=header.read_text()
s=s.replace("__inline UInt", "static __inline UInt")
header.write_text(s)
