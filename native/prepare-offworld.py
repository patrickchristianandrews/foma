#!/usr/bin/env python3
"""Port the pinned MPL-2.0 OFFWORLD engine without changing its sound algorithms.
Allocate full-size audio buffers only while screen seven is active. This is a
feasibility build, not a release. Refuse entry if allocation fails."""
import pathlib, re, sys, shutil
src=pathlib.Path(sys.argv[1])
dst=pathlib.Path(__file__).parent/'offworld'
dst.mkdir(exist_ok=True)
s=(src/'dsp/engine.c').read_text()
shutil.copyfile(src/'dsp/engine.h',dst/'engine.h')
shutil.copyfile(src/'LICENSE',dst/'LICENSE')
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
s+='int lw_offworld_alloc(void){\n'+''.join(f' ow_{n}=malloc(sizeof *ow_{n}); if(!ow_{n})goto fail;\n' for n in names)+' p_seq=p_took=0; notes=thunders=0; duck_on=0; vb_w=vb_full=0; eng_init(); return 1;\nfail: lw_offworld_free(); return 0;\n}\n'
(dst/'engine.c').write_text(s)
