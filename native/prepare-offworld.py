#!/usr/bin/env python3
"""Port the pinned MPL-2.0 OFFWORLD engine without changing its sound algorithms.
Allocate compact device audio buffers only while screen seven is active.
Preserve eight voices, chorus, reverb decay/freeze and stereo delay, with shorter
physical delay lines and memory capture. Refuse entry if allocation fails."""
import pathlib, re, sys, shutil
src=pathlib.Path(sys.argv[1])
dst=pathlib.Path(__file__).parent/'offworld'
dst.mkdir(exist_ok=True)
s=(src/'dsp/engine.c').read_text()
shutil.copyfile(src/'dsp/engine.h',dst/'engine.h')
shutil.copyfile(src/'LICENSE',dst/'LICENSE')
# Low-heap profile for the Wi-Fi device. Keep all eight voices and effects,
# using shorter physical delay/reverb lines and ~0.37 s grain capture.
# MEM_N must exceed the upstream 6000-sample grain-position guard.
# Use exactly the same profile for the host regression and device build.
for old,new in [
 ('#define MEM_N 66150u','#define MEM_N 8192u'),
 ('#define DL_L 15876u','#define DL_L 2205u'),
 ('#define DL_R 23814u','#define DL_R 3307u'),
 ('{1153, 1361, 1597, 1847, 2083, 2399, 2711, 3023}',
  '{193, 227, 269, 311, 349, 401, 457, 509}'),
 ('#define RV_TOTAL 39000u','#define RV_TOTAL 6800u'),
]:
    assert s.count(old)==1,old
    s=s.replace(old,new)
assert 8192>6000
assert sum(int(x*2.4)+8 for x in (193,227,269,311,349,401,457,509))<=6800
# Never advance the producer ring into the reader when the input queue fills.
old='static void commit(void) { q_w = (q_w + 1u) % NQ; }'
assert s.count(old)==1
s=s.replace(old,'static void commit(void) { uint32_t nx=(q_w+1u)%NQ; if(nx!=q_r)q_w=nx; }')

# Move arrays off the permanently reserved pool and BSS. Pointer-to-array
# macros preserve indexing and sizeof exactly as in the original.
decls=[
 ('static prm_t P, T, P_next;', [('prm_t',n,'') for n in ('P','T','P_next')]),
 ('static voice_t V[ENG_NV];',[('voice_t','V','[ENG_NV]')]),
 ('static ev_t Q[NQ];',[('ev_t','Q','[NQ]')]),
 ('static int8_t mem_buf[MEM_N] PLAT_POOL;',[('int8_t','mem_buf','[MEM_N]')]),
 ('static float mem_tab[256];',[('float','mem_tab','[256]')]),
 ('static float mem_h[16];',[('float','mem_h','[16]')]),
 ('static float ens_buf[ENS_N] PLAT_POOL;',[('float','ens_buf','[ENS_N]')]),
 ('static int16_t dl_l[DL_L] PLAT_POOL, dl_r[DL_R] PLAT_POOL;',[('int16_t','dl_l','[DL_L]'),('int16_t','dl_r','[DL_R]')]),
 ('static int16_t rv_mem[RV_TOTAL] PLAT_POOL;',[('int16_t','rv_mem','[RV_TOTAL]')]),
 ('static float ap_buf[142 + 107 + 379 + 277] PLAT_POOL;',[('float','ap_buf','[142 + 107 + 379 + 277]')]),
 ('static float vb_l[2][NBLK], vb_r[2][NBLK];',[('float','vb_l','[2][NBLK]'),('float','vb_r','[2][NBLK]')]),
 ('static float blk_lfo[NCHK], blk_wow[NCHK];',[('float','blk_lfo','[NCHK]'),('float','blk_wow','[NCHK]')]),
]
names=[]
for old,fields in decls:
    assert s.count(old)==1,old
    rep=[]
    for typ,name,dims in fields:
        names.append(name)
        rep.extend([f'static {typ} (*ow_{name}){dims};',f'#define {name} (*ow_{name})'])
    s=s.replace(old,'\n'.join(rep))
for name in ('W','E'):
    pat=r'static struct \{([^{}]*)\} '+name+r';'
    matches=list(re.finditer(pat,s,re.S))
    assert len(matches)==1,name
    m=matches[0]
    s=s[:m.start()]+f'typedef struct {{{m.group(1)}}} ow_{name}_t;\nstatic ow_{name}_t *ow_{name};\n#define {name} (*ow_{name})'+s[m.end():]
    names.append(name)
s=s.replace('#include "plat.h"','#include "plat.h"\n#include <stdlib.h>')
# Existing Wi-Fi base has no enabled CPU2 pipeline. Test the upstream
# single-core fallback first; real-time capacity is a separate device gate.
s=s.replace('on2 = plat_cpu2_on() && n == NBLK_USE && plat_cpu2_run(job_voices, vb_w);','on2 = 0;')
s=s.replace('if (plat_cpu2_wait() != 0)','if (0)')
s+='\nvoid lw_offworld_free(void){\n'+''.join(f' free(ow_{n}); ow_{n}=0;\n' for n in names)+'}\n'
s+='static unsigned ow_failed_bytes;\nunsigned lw_offworld_failed_bytes(void){return ow_failed_bytes;}\n'
# Request the big delay and capture buffers first, before small allocations.
first=['rv_mem','mem_buf','ens_buf','dl_r','dl_l','vb_l','vb_r','ap_buf']
order=first+[n for n in names if n not in first]
assert sorted(order)==sorted(names)
s+='int lw_offworld_alloc(void){\n ow_failed_bytes=0;\n'+''.join(f' ow_{n}=malloc(sizeof *ow_{n}); if(!ow_{n}){{ow_failed_bytes=(unsigned)sizeof *ow_{n};goto fail;}}\n' for n in order)+' p_seq=p_took=0; notes=thunders=0; duck_on=0; vb_w=vb_full=0; eng_init(); return 1;\nfail: lw_offworld_free(); return 0;\n}\n'
(dst/'engine.c').write_text(s)
