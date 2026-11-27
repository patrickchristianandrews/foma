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
lines = [", ".join(f"0x{v:04x}" for v in pixels[i:i+16])+"," for i in range(0,len(pixels),16)]
out.write_text("#ifndef TIDBIT_ROOM_GENERATED_H\n#define TIDBIT_ROOM_GENERATED_H\nstatic const uint16_t tidbitRoomPortrait[80*80]={\n"+"\n".join(lines)+"\n};\n#endif\n")
print("Generated 80 x 80 original Tidbit portrait, distinct colors:", len(set(pixels)))
