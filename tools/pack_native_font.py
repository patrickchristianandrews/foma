#!/usr/bin/env python3
"""Losslessly pack the original native display font. Every original alpha nibble
is either 0 or 15, so one bit per pixel preserves every displayed pixel.
The stock firmware's font header is not modified."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
s=(root/'platform/gen/felucca_font.h').read_text()
before=after=0
for name in ('S','L'):
    m=re.search(r'FONT_'+name+r'_DATA\[(\d+)\] = \{([\s\S]*?)\};',s)
    assert m
    raw=bytes(int(v,16) for v in re.findall(r'0x[0-9a-fA-F]+',m[2]))
    assert len(raw)==int(m[1]) and len(raw)%4==0
    bits=[]
    for b in raw:
        for n in (b>>4,b&15):
            assert n in (0,15), 'non-binary alpha: refuse lossy packing'
            bits.append(n//15)
    packed=bytes(sum(bits[i+j]<<(7-j) for j in range(8)) for i in range(0,len(bits),8))
    recovered=[]
    for b in packed:
        recovered.extend(15*((b>>(7-j))&1) for j in range(8))
    assert bytes((recovered[i]<<4)|recovered[i+1] for i in range(0,len(recovered),2))==raw
    before+=len(raw);after+=len(packed)
    block='FONT_'+name+'_DATA['+str(len(packed))+'] = {\n'
    block+='\n'.join(', '.join(f'0x{x:02x}' for x in packed[i:i+24])+',' for i in range(0,len(packed),24))
    s=s[:m.start()]+block+'\n};'+s[m.end():]
    m=re.search(r'FONT_'+name+r'_OFF\[(\d+)\] = \{([^}]+)\};',s)
    assert m
    offsets=[int(x.strip()) for x in m[2].split(',') if x.strip()]
    assert len(offsets)==int(m[1]) and all(x%4==0 for x in offsets)
    s=s[:m.start()]+'FONT_'+name+'_OFF['+m[1]+'] = {'+', '.join(str(x//4) for x in offsets)+'};'+s[m.end():]
(root/'native/font_packed.h').write_text(s)
print(f'Exact original font packing: {before} -> {after} bytes; saved {before-after} bytes, verified bit-for-bit')
