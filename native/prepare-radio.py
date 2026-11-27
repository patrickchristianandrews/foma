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
# MP3-only first device build: the combined application exceeds the flash slot with AAC.
s=s.replace('#include "third_party/pvaac/pv_fm1.h"','// #include "third_party/pvaac/pv_fm1.h"')
s=s.replace('#include "third_party/pvaac/pvmp4audiodecoder_api.h"','')
start=s.index("static tPVMP4AudioDecoderExternal *aac_ext;")
end=s.index("static int radio_alloc(void)",start)
s=s[:start]+"""static void dec_drop(void){
    free(mp3); free(pcm_tmp); mp3=0; pcm_tmp=0; dec_kind=0;
}
static int dec_use(uint8_t kind){
    if(kind!=1)return -1;
    if(dec_kind==1)return 0;
    mp3=malloc(sizeof *mp3);
    pcm_tmp=malloc(MINIMP3_MAX_SAMPLES_PER_FRAME*sizeof *pcm_tmp);
    if(!mp3||!pcm_tmp){dec_drop();return -1;}
    mp3dec_init(mp3);dec_kind=1;return 0;
}
"""+s[end:]
start=s.index("        if (dec_kind == 2) {")
end=s.index("        samples = mp3dec_decode_frame",start)
s=s[:start]+s[end:]
s=s.replace("        src = pcm_tmp;\n        }","        src = pcm_tmp;")
s=s.replace("        if (dec_use(radio_mode != 2 && radio_net_aac ? 2 : 1)) {","""        if (radio_net_aac) {
            radio_net_stop();
            w_cpy(radio_why, "AAC not supported", sizeof radio_why);
            radio_st=RS_ERROR; return;
        }
        if (dec_use(1)) {""")
(dest/"radio_core.c").write_text(s)


# Reuse the existing network transport on the same worker budget. The result
# buffer is owned by the weather worker, not the audio callback.
net=dest/"net/radio_net.c"
# Capture HTTP Date for weather only. Open-Meteo current.time is a model
# observation interval, not the wall clock. Keep the existing radio protocol.
ns=net.read_text()
marker="static int http_open(conn_t *c, char *u, unsigned umax, int json)"
assert ns.count(marker)==1
ns=ns.replace(marker,r"""
static int lw_http_utc=-1;
static void lw_http_date(const char *p){
    /* IMF-fixdate: Wed, 25 Nov 2026 05:14:03 GMT */
    while(*p==' ')p++;
    if(strlen(p)!=29 || p[19]!=':' || p[22]!=':' || strcmp(p+25," GMT"))return;
    const unsigned char ix[]={17,18,20,21,23,24};
    for(unsigned i=0;i<sizeof ix;i++)if(p[ix[i]]<'0'||p[ix[i]]>'9')return;
    int h=(p[17]-'0')*10+p[18]-'0',m=(p[20]-'0')*10+p[21]-'0',s=(p[23]-'0')*10+p[24]-'0';
    if(h<24&&m<60&&s<60)lw_http_utc=h*3600+m*60+s;
}
"""+marker)
marker='        } else if (ieq_prefix(line, "content-type:")) {'
assert ns.count(marker)==1
ns=ns.replace(marker,'        } else if (json==2 && ieq_prefix(line, "date:")) {\n            lw_http_date(line+5);\n'+marker)
net.write_text(ns)
with net.open("a") as f:
    f.write(r"""
/* Bounded public JSON request; caller serializes all radio/weather tasks. */
int lw_net_json(char *url, unsigned url_cap, char *out, unsigned cap, char *why, int *utc)
{
    conn_t c = {-1, 0, 0, why};
    int rc, got, tries;
    unsigned used=0;
    uint32_t start=shim_ms();
    if(cap<2)return -1;
    why[0]=0;
    for(tries=0;tries<3;tries++){
        lw_http_utc=-1;
        rc=http_open(&c,url,url_cap,2);
        if(rc!=1)break;
    }
    if(rc!=0)return -1;
    while(used<cap-1 && shim_ms()-start<15000u){
        got=conn_read(&c,out+used,cap-1-used);
        if(got<=0)break;
        used+=(unsigned)got;
    }
    conn_close(&c);
    out[used]=0;
    /* Require a complete JSON object, not a prefix of an overlong response. */
    while(used && (out[used-1]==' '||out[used-1]=='\n'||out[used-1]=='\r'||out[used-1]=='\t'))used--;
    if(!used||out[0]!='{'||out[used-1]!='}'||used>=cap-1){
        if(!why[0])why_to(why,"weather response");
        return -1;
    }
    *utc=lw_http_utc;
    return (int)used;
}
""")
