#!/usr/bin/env python3
"""Render the actual original website sprite frame as a larger FM-1 RGB565 portrait.
Run with Python 3 and Pillow before building. No external network requests.
"""
import base64
import io
import re
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
svg = (root / "src/art/tidbit-original.svg").read_text()
payload = re.search(r'data:image/webp;base64,([A-Za-z0-9+/=]+)', svg)
if not payload:
    raise ValueError("Missing original mascot sprite sheet")
sheet = Image.open(io.BytesIO(base64.b64decode(payload.group(1)))).convert("RGBA")
frame = sheet.crop((0, 0, 128, 128))
bounds = frame.getchannel("A").getbbox()
if not bounds:
    raise ValueError("Empty original Tidbit frame")
frame = frame.crop(bounds)
frame.thumbnail((78, 78), Image.Resampling.LANCZOS)
canvas = Image.new("RGBA", (80, 80))
canvas.paste(frame, ((80-frame.width)//2, 80-frame.height))
pixels = []
for r,g,b,a in canvas.getdata():
    pixels.append(max(1, ((r>>3)<<11)|((g>>2)<<5)|(b>>3)) if a>=100 else 0)
if len(set(pixels)) < 100:
    raise ValueError("Original portrait conversion lost color detail")
out = root / "src/tidbit_room_generated.h"
# Lossless transparent-edge compression. Keep exactly the original 80x80 sprite.
spans = []
packed = []
for y in range(80):
    row = pixels[y*80:(y+1)*80]
    used = [i for i, v in enumerate(row) if v]
    lo = used[0] if used else 0
    hi = used[-1]+1 if used else 0
    spans.append((lo, hi-lo))
    packed.extend(row[lo:hi])
assert sum(n for _, n in spans) == len(packed)
restored = []
pos = 0
for lo, n in spans:
    restored.extend([0]*lo + packed[pos:pos+n] + [0]*(80-lo-n))
    pos += n
assert restored == pixels
lines = [", ".join(f"0x{v:04x}" for v in packed[i:i+16])+"," for i in range(0,len(packed),16)]
out.write_text("#ifndef TIDBIT_ROOM_GENERATED_H\n#define TIDBIT_ROOM_GENERATED_H\n#define TIDBIT_ROOM_SPANS 1\nstatic const uint8_t tidbitRoomSpan[80][2]={\n"+",\n".join("{%d,%d}" % span for span in spans)+"\n};\nstatic const uint16_t tidbitRoomPortrait[]={\n"+"\n".join(lines)+"\n};\n#endif\n")
print("Generated lossless 80 x 80 original Tidbit portrait, bytes saved:", 12800-160-2*len(packed))
