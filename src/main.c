/* SPDX-License-Identifier: GPL-3.0-only */
/* Liquid Wave experimental native firmware. Not hardware tested.
 * PLAY transport; REC overdub; SELECT encoder channel; PRESETS style;
 * knob1 tempo; SEL mute; ARP generate; SEQ next section; SAVE while stopped.
 * Keys play/record selected channel. Drum keys 0..3: kick/snare/hat/open hat.
 * 8 independent 16-step patterns per section, four-bar harmonic sections.
 * Audio: simple two-operator FM, not a DX7 engine. No external sound banks.
 */
#include "fm1.h"
#include "liquid_song.h"
#include "song_catalog.h"
#include "tidbit_portrait.h"
#if defined(__has_include)
#if __has_include("tidbit_room_generated.h")
#include "tidbit_room_generated.h"
#define TIDBIT_ROOM_HIRES 1
#endif
#endif
#include "weather.h"
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
void lw_wifi_draw(void);
const char *lw_weather_status(void);
int lw_wifi_step(void);
void lw_wifi_leave(void);
void lw_radio_draw(void);
int lw_radio_ui(void);
void lw_radio_leave(void);
static int native_radio_mode;
#endif
#define NC 8
#define NS 5
#define NV 12
#define EMPTY 255

typedef struct { uint32_t magic; uint8_t style,bpm,sel,mute; uint8_t pat[NS][NC][16]; } Project;
static Project p;
enum {HOME, MIX, ENV, MOTION, EDIT, GLOBAL, FX, SEQ, SECTION_SOUND, WEATHER, SONG_DETAILS, RADIO, CATALOG, LOOP_BUILDER};
static int page, cursor, octave, loopSection, repeat, transpose;
static volatile int masterLevel=100;
static uint8_t level[8]={85,85,85,85,85,85,85,85};
static uint8_t shape[8]={0,1,1,1,1,1,1,1};
static int toneDepth[8]={0,16,20,28,8,36,24,12};
static int tails[8]={11,12,13,13,15,11,12,13};
typedef struct {const char *name; uint8_t ratio,depth,tail,level; int8_t shift;} Instrument;
static const Instrument instruments[]={
 {"Round bass",1,18,12,82,-12},{"Rubber bass",2,49,11,83,-12},{"Sub sine",1,0,15,78,-12},
 {"Acid bass",3,69,12,78,-12},{"Octave bass",2,35,13,75,-24},{"Reed bass",4,24,13,76,-12},
 {"Velvet keys",2,13,14,78,0},{"Stage keys",3,34,13,76,0},{"Wurlie",1,49,12,73,0},
 {"Toy piano",6,31,11,72,12},{"Clav",5,62,10,68,0},{"Harpsichord",7,39,11,70,0},
 {"Glass bell",6,30,16,65,12},{"Tiny chime",8,21,14,62,12},{"Gong",5,68,17,62,-12},
 {"Vibraphone",3,17,15,69,0},{"Marimba",4,45,10,73,0},{"Kalimba",7,22,12,67,0},
 {"Air pad",1,7,17,65,0},{"Cloud pad",2,10,16,67,12},{"Halo pad",3,16,17,63,0},
 {"Dark pad",1,26,17,62,-12},{"Soft strings",2,41,16,64,0},{"Space choir",4,12,16,62,0},
 {"Square lead",3,58,13,69,0},{"Bright lead",5,40,14,70,0},{"Flute",1,7,13,74,12},
 {"Reed lead",4,50,13,70,0},{"Hollow lead",2,29,14,72,0},{"Chip pulse",6,56,12,65,12},
 {"Bubble pluck",2,59,10,72,0},{"Laser pluck",7,67,10,67,12},{"Wood pluck",3,33,10,70,0},
 {"Dust spark",8,72,11,60,12},{"Alien call",5,57,16,63,0},{"Metal hit",7,72,12,66,-12}
};
static const Instrument kits[]={
 {"Pocket kit",1,0,11,85,0},{"Deep club",1,5,12,94,-9},{"Tight electro",1,18,9,80,7},
 {"Dusty breaks",1,11,10,83,-4},{"Soft clicks",1,0,7,62,12},{"Big room",1,26,13,88,-2},
 {"Tin machine",1,40,8,78,15},{"Slow thunder",1,8,14,84,-17}
};
#define COUNT(a) ((int)(sizeof(a)/sizeof((a)[0])))
static uint8_t preset[NC]={0,0,6,24,18,30,33,9};
static int motion[8], drive[8];
static uint8_t lfoRate[8]={4,4,4,4,4,4,4,4};
static uint8_t attackMs[8];
static volatile uint8_t fxDelay,fxReverb,fxChorus,fxFilter;
static int fxParameter;
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
extern void *malloc(unsigned long);
static int16_t *echoL,*echoR;
#else
static int16_t echoL[4096],echoR[4096];
#endif
static unsigned echoPos;
static int32_t filterL,filterR;
static int wrap(int value,int count);
static const Instrument *instrument(int ch){return ch?&instruments[preset[ch]]:&kits[preset[ch]];}
static void chooseInstrument(int ch,int id){
 preset[ch]=(uint8_t)wrap(id,ch?COUNT(instruments):COUNT(kits));
 const Instrument *patch=instrument(ch);
 shape[ch]=patch->ratio-1;level[ch]=patch->level;toneDepth[ch]=patch->depth;tails[ch]=patch->tail;
 motion[ch]=0;drive[ch]=ch?0:patch->depth;
}
typedef struct { Project project; uint8_t presets[NC]; } SavedProject;
typedef struct {
 SavedProject session;
 LiquidHarmony harmony;
} SavedSong;

typedef struct { int8_t level, pitch, depth, decay, ratio, pan; } SectionSound;
typedef struct { SavedSong song; SectionSound sound[NS][NC]; } SavedMix;
static SectionSound sectionMix[NS][NC];
static int editSection, editParameter;
static LiquidHarmony songHarmony;
static const uint32_t MIX_MAGIC = 12347u;
static const uint32_t SONG_MAGIC = 12346u;
static const uint32_t LEGACY_MAGIC = 12345u;

static const char *pageName[]={"STAGE","MIX","ENVELOPE","MOTION","SOUND EDIT","GLOBAL","FX","STEPS","SECTION SOUND"};
static const char *knobGuide[]={"TEMPO  LEVEL  FM  TAIL","TEMPO  LEVEL  FM  TAIL","TEMPO  LEVEL  FM  TAIL","TEMPO  LEVEL  MOT  TAIL","TEMPO  LEVEL  FM  TAIL","TEMPO  OUT  KEY  OCT","TEMPO  LEVEL  DRIVE  TAIL","TEMPO  STEP  NOTE  LOOP"};
static int clamp(int value,int low,int high){return value<low?low:value>high?high:value;}
static int wrap(int value,int count){return (value%count+count)%count;}

static const char *names[8]={"BEAR","CAT","BUNNY","CHICK","CLOUD","ALIEN","GHOST","TIDBIT"};
static const char *sections[5]={"INTRO","VERSE","CHORUS","BRIDGE","OUTRO"};

static const uint8_t scale[2][7]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10}};
static const uint16_t colors[8]={RGB(255,100,40),RGB(190,90,255),RGB(255,210,60),RGB(70,220,250),RGB(255,120,210),RGB(90,255,170),RGB(250,150,80),RGB(160,180,255)};
static const uint8_t vb[8]={0,0,1,4,5,8,9,10};
static const uint8_t vn[8]={0,1,3,1,3,1,1,1};
static volatile uint8_t head,tail;
static volatile uint8_t releaseRequested;
/* Weather-only procedural ambience: disabled by default, no cat samples. */
static volatile uint8_t ambientLevel, animalLevel, ambientMode;
static volatile uint8_t animalTrigger;
static uint32_t animalPhase, animalRemaining;
static int32_t ambientLow, thunderEnv;
static uint32_t ambientRng=0x9e3779b9u;
typedef struct {uint8_t voice,sh,drum,ratio;uint32_t inc;int32_t amp,idx;int8_t pan;} Event;
static Event queue[64];
typedef struct {uint32_t ph,mp,inc;int32_t env,idx;uint8_t sh,ratio;int8_t pan;uint32_t age,attack;} Voice;
static Voice v[NV];
static int32_t ke,se,he,prevNoise;static uint8_t drumTail=11; static uint32_t kp,ki,kbase,noise=0xA5A5A5A5u;
static int8_t drumPan;
static int playing,recording,pos,section,step,bar;
static uint32_t due,remMs,active[8],lastFace,lastHud;
static uint32_t bootStarted, lastWave;
#define SURF_Y 145
#define SURF_COLOR RGB(16,67,96)
static int booting = 1;
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
static int native_boot_clock_started;
#define BOOT_WAVE_MS 1400u
#else
#define BOOT_WAVE_MS 780u
#endif
int liquid_boot_complete(void){return !booting;}
static struct {
 LiquidWeather reading;
 LiquidWeatherMusic music;
 LiquidHarmony harmony;
 uint8_t pattern[NS][NC][16];
 uint32_t synced, nextStep, octSince, seed;
 int place, back, available, playing, step, octHeld, octUsed;
} weather;
typedef struct {int x,y,vx,vy,ox,oy;uint8_t expr; } Face;
static Face f[8];
static uint32_t rnd(uint32_t n){return n?random32()%n:0;}
static void enqueue(Event e){uint8_t h=(head+1)&63;if(h==tail)return;queue[head]=e;head=h;}
static int degree(int d,int style){return scale[liquid_song_major((uint8_t)style)?0:1][d%7]+12*(d/7);}
static void sound_at_root(int ch,int d,int root,int part,int style,int gain){
 const SectionSound *fx=&sectionMix[part][ch];
 int volume=clamp(level[ch]+fx->level,0,100)*gain/100;
 if(!volume)return;
 int decay=clamp(tails[ch]+fx->decay,ch?9:5,ch?17:14);
 int shift=instrument(ch)->shift+transpose+octave*12+fx->pitch;
 int depth=clamp(toneDepth[ch]+drive[ch]+fx->depth,0,120);
 int8_t pan=(int8_t)(fx->pan*127/100);
 active[ch]=millis()+180;
 if(ch==0){
  Event e={0,(uint8_t)decay,(uint8_t)d,0,note_inc(clamp(68+shift,0,127)),
    volume*240,clamp(drive[0]+fx->depth,0,90),pan};
  enqueue(e);
  return;
 }
 for(int j=0;j<vn[ch];j++){
  int note=clamp(60+root+shift+degree(d+j*2,style),0,127);
  int animate=motion[ch]?((int)sine(millis()* (uint32_t)(lfoRate[ch]+1)*429497u)*motion[ch])/32768:0;
  Event e={(uint8_t)(vb[ch]+j),(uint8_t)decay,0,(uint8_t)clamp(shape[ch]+1+fx->ratio,1,8),
    note_inc(note),((ch==2||ch==4)?70:160)*volume,depth+animate,pan};
  enqueue(e);
 }
}
static void sound(int ch,int d) {sound_at_root(ch,d,songHarmony.root,section,p.style,100);}
#if FOMA_BLUETOOTH
static void bluetooth_input(void) {
 uint8_t status,note,velocity;
 for(int budget=0;budget<16;budget++){
  int source=midi_read(&status,&note,&velocity);
  if(!source)break;
  if(source==MIDI_BLE && (status&0xf0)==0x90 && velocity && note<=127){
   int ch=(status&15)==9?0:p.sel;
   if(!ch) sound(0,1u<<(note%4));
   else {
    active[ch]=millis()+180;
    Event e={vb[ch],(uint8_t)tails[ch],0,(uint8_t)(shape[ch]+1),note_inc(note),
      160*level[ch]*velocity/127,toneDepth[ch]+drive[ch],0};
    enqueue(e);
   }
  }
 }
}
#endif
/* Increase instrument loudness by 50% at the mixer, without changing
 * oscillator envelopes, FM timbre, track balance or pan. */
static int32_t instrument_gain(int32_t sample){
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 return sample;
#else
 return sample+sample/2;
#endif
}
/* Gentle bounded output knee retains headroom for overlapping notes and FX. */
static int32_t output_limit(int32_t sample){
 int32_t a=sample<0?-sample:sample;
 if(a>16000)a=16000+(a-16000)/4;
 if(a>24000)a=24000;
 return sample<0?-a:a;
}
/* Fourfold output makeup gain (+12 dB below the knee). Compress only after
 * the effects, so internal feedback and per-track balances are unchanged.
 * Master remains a true post-limiter attenuator, including silence at zero. */
static int32_t loud_output(int32_t sample,int volume){
 int32_t a=sample<0?-sample:sample;
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 /* Leave headroom for stacked FM voices, effects and network audio. */
 if(a>22000)a=22000+(a-22000)/4;
 if(a>28000)a=28000;
#else
 a*=4;
 if(a>20000)a=20000+(a-20000)/8;
 if(a>32000)a=32000;
#endif
 a=a*volume/100;
 return sample<0?-a:a;
}
static void mix_sample(int32_t sample,int pan,int32_t *mono,int32_t *left,int32_t *right){
 sample=instrument_gain(sample);
 if(!pan){*mono+=sample;return;}
 /* Constant gain on the near speaker; attenuate the opposite speaker. */
 *left+=pan>0?(sample*(127-pan))/128:sample;
 *right+=pan<0?(sample*(127+pan))/128:sample;
}
void audio(int32_t *out,uint32_t frames){
 while(tail!=head){Event e=queue[tail];tail=(tail+1)&63;
 if(e.drum==128){if(e.voice<NV&&v[e.voice].sh>10)v[e.voice].sh=10;continue;}
 if(e.drum){drumTail=e.sh;drumPan=e.pan;if(e.drum&1){ke=e.amp;ki=e.inc;kbase=e.inc/7;}if(e.drum&2)se=e.amp*3/4+e.idx*30;if(e.drum&12)he=e.amp/2+e.idx*20;}
 else {Voice *a=&v[e.voice];a->inc=e.inc;a->env=e.amp<<14;a->idx=e.idx;a->sh=e.sh;a->ratio=e.ratio;a->pan=e.pan;a->age=0;int ch=1;for(int j=1;j<NC;j++)if(e.voice>=vb[j]&&e.voice<vb[j]+vn[j]){ch=j;break;}a->attack=(uint32_t)attackMs[ch]*44u;}}
 /* Stop releases active voices smoothly on the audio thread. */
 if(releaseRequested){releaseRequested=0;for(int j=0;j<NV;j++)if(v[j].sh>10)v[j].sh=10;if(drumTail>10)drumTail=10;}
 for(uint32_t n=0;n<frames;n++){
 int32_t sum=0,left=0,right=0,drums=0;
 for(int j=0;j<NV;j++){Voice *a=&v[j];int32_t env=a->env>>14;if(env<8)continue;
 a->ph+=a->inc;a->mp+=a->inc*a->ratio;
 int32_t dep=(a->idx*env)>>15;
 uint32_t offset=(uint32_t)((int64_t)sine(a->mp)*dep*1304);
 int32_t signal=(sine(a->ph+offset)*env)>>17;
 if(a->age<a->attack){signal=signal*(int32_t)(a->age/16)/(int32_t)(a->attack/16+1);a->age++;}
 mix_sample(signal,a->pan,&sum,&left,&right);
 a->env-=a->env>>a->sh;}
 noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;int32_t nz=(int32_t)(noise>>17)-16384;
 if(ke>32){kp+=ki;ki-=(ki-kbase)>>9;drums+=(sine(kp)*ke)>>17;ke-= (ke>>drumTail) ? (ke>>drumTail) : 1;}
 if(se>32){drums+=(nz*se)>>17;se-= (se>>(drumTail-1)) ? (se>>(drumTail-1)) : 1;}
 if(he>32){drums+=((nz-prevNoise)*he)>>18;he-= (he>>(drumTail-1)) ? (he>>(drumTail-1)) : 1;}prevNoise=nz;
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 /* Percussion-only +4.9 dB makeup, before the existing output limiter. */
 drums+=drums*3/4;
#endif
 mix_sample(drums,drumPan,&sum,&left,&right);
 /* Separate optional weather ambience and dog/bird calls. */
 if(ambientLevel||animalLevel||animalRemaining){
  ambientRng^=ambientRng<<13;ambientRng^=ambientRng>>17;ambientRng^=ambientRng<<5;
  int32_t white=(int32_t)(ambientRng>>17)-16384;
  ambientLow+=(white-ambientLow)/128;
  if(ambientLevel){
   int32_t texture=ambientMode==2?ambientLow*3:ambientMode==1?(white-ambientLow)/3:(white-ambientLow)/6;
   if(ambientMode==3){if((ambientRng&0x1ffffu)==0)thunderEnv=12000;texture+=ambientLow*(thunderEnv/256)/16;if(thunderEnv>0)thunderEnv--;}
   sum+=texture*ambientLevel/400;
  }
  if(animalTrigger){animalTrigger=0;animalRemaining=12000;animalPhase=0;}
  if(animalRemaining){
   uint32_t elapsed=12000-animalRemaining;
   uint32_t hz=ambientMode==2?1100+elapsed/32:180+animalRemaining/80;
   animalPhase+=hz*97391u;
   int32_t envelope=(int32_t)(animalRemaining<6000?animalRemaining:12000-animalRemaining);
   int32_t call=(sine(animalPhase)/8+(ambientMode==2?0:white/5))*envelope/6000;
   sum+=call*animalLevel/100;
   animalRemaining--;
  }
 }
 int32_t dryL=output_limit(sum+left),dryR=output_limit(sum+right);
 if(fxFilter){int shift=1+fxFilter/16;filterL+=(dryL-filterL)>>shift;filterR+=(dryR-filterR)>>shift;dryL=filterL;dryR=filterR;}
 else {filterL=dryL;filterR=dryR;}
 if(echoL&&echoR&&(fxDelay||fxReverb||fxChorus)){
  unsigned mod=(unsigned)((sine((uint32_t)echoPos*1048576u)+32768)>>10);
  unsigned shortTap=(echoPos+4096u-700u-mod)&4095u;
  unsigned roomTap=(echoPos+4096u-1700u)&4095u;
  int32_t wetL=(echoL[echoPos]*fxDelay+echoR[roomTap]*fxReverb+echoL[shortTap]*fxChorus)/100;
  int32_t wetR=(echoR[echoPos]*fxDelay+echoL[roomTap]*fxReverb+echoR[shortTap]*fxChorus)/100;
  echoL[echoPos]=(int16_t)clamp(dryL+wetL/3,-24000,24000);
  echoR[echoPos]=(int16_t)clamp(dryR+wetR/3,-24000,24000);
  dryL+=wetL;dryR+=wetR;
 }else if(echoL&&echoR){echoL[echoPos]=0;echoR[echoPos]=0;}
 echoPos=(echoPos+1)&4095u;
 int volume=masterLevel;
 out[n*2]+=loud_output(dryL,volume);
 out[n*2+1]+=loud_output(dryR,volume);
 }
}
/* Catalog player uses the existing FM voices, with exact source pitches,
 * delta-time scheduling and bounded per-note releases. It never edits p. */
static int catalogIndex,catalogPlaying,catalogEvent,catalogVolume=75,catalogSpeed=100,catalogPatch=0;
static uint32_t catalogStart,catalogTick,catalogEnd,catalogOff[6];
static const uint8_t catalogVoices[6]={1,2,3,5,6,7};
static const char *catalogPatches[]={"SOFT KEYS","FM BELLS","WARM ORGAN","BRIGHT KEYS"};
static void catalog_stop(void){catalogPlaying=0;releaseRequested=1;for(int i=0;i<6;i++)catalogOff[i]=0;}
static void catalog_start(uint32_t now){
 catalog_stop();playing=0;catalogEvent=0;catalogTick=catalogSongs[catalogIndex].events[0].dt;
 catalogStart=now;catalogEnd=0;catalogPlaying=1;
}
static void catalog_playback(uint32_t now){
 if(!catalogPlaying)return;
 const CatalogSong *song=&catalogSongs[catalogIndex];
 uint32_t tick=(uint32_t)(((uint64_t)(now-catalogStart)*song->bpm*24u*catalogSpeed)/6000000u);
 for(int i=0;i<6;i++)if(catalogOff[i]&&tick>=catalogOff[i]){
  Event e={catalogVoices[i],0,128,0,0,0,0,0};enqueue(e);catalogOff[i]=0;
 }
 for(int budget=0;catalogEvent<song->count&&tick>=catalogTick&&budget<24;budget++){
  const CatalogEvent *note=&song->events[catalogEvent];
  int slot=0;for(int j=1;j<6;j++)if(catalogOff[j]<catalogOff[slot])slot=j;
  catalogOff[slot]=catalogTick+(note->dur?note->dur:1);
  if(catalogOff[slot]>catalogEnd)catalogEnd=catalogOff[slot];
  static const uint8_t ratio[]={1,3,2,2},depth[]={14,32,4,24};
  Event e={catalogVoices[slot],16,0,ratio[catalogPatch],note_inc(note->note&127),catalogVolume*95,depth[catalogPatch],(note->note&128)?-24:24};
  enqueue(e);
  if(++catalogEvent<song->count)catalogTick+=song->events[catalogEvent].dt;
 }
 if(catalogEvent>=song->count&&tick>=catalogEnd)catalog_stop();
}
static void generate(void){
 static uint8_t nextPattern[NS][NC][16];
 LiquidHarmony nextHarmony;
 uint8_t bpm;
 uint32_t seed=random32();
 int different;
 do {
  bpm=liquid_song_generate(p.style,seed++,nextPattern,&nextHarmony);
  different=0;
  for(int s=0;s<NS;s++)for(int ch=0;ch<NC;ch++)for(int i=0;i<16;i++)
   different |= p.pat[s][ch][i]!=nextPattern[s][ch][i];
 } while(!different);
 for(int s=0;s<NS;s++)for(int ch=0;ch<NC;ch++)for(int i=0;i<16;i++){
  p.pat[s][ch][i]=nextPattern[s][ch][i];
 }
 p.bpm=bpm;
 songHarmony=nextHarmony;
}
/* Classic preserves the original web composition. Other modes selectively remix. */
static int songScope=0,songDensity=2,songPart=0;
static const char *songScopes[]={"CLASSIC","CUSTOM","SECTION","TRACK","CHORDS","ORDER","ONE SEQUENCE"};
/* Randomize exactly one 16-step character pattern. Preserve all other
 * patterns, tempo, key, progression, arrangement, instrument and mix. */
static void remix_sequence(int part,int ch){
 static uint8_t candidate[NS][NC][16];
 LiquidHarmony ignored;
 uint8_t next[16];
 int changed=0;
 for(int attempt=0;attempt<4&&!changed;attempt++){
  liquid_song_generate(p.style,random32(),candidate,&ignored);
  changed=0;
  for(int i=0;i<16;i++){
   uint8_t n=candidate[part][ch][i];
   if(songDensity<2&&i&&rnd(4)<(unsigned)(2-songDensity))n=ch?EMPTY:0;
   if(songDensity>2&&rnd(8)<(unsigned)(songDensity-2)){
    if(!ch)n|=4;
    else if(n==EMPTY)n=(uint8_t)(rnd(4)*2);
   }
   next[i]=n;
   changed|=n!=p.pat[part][ch][i];
  }
 }
 if(!changed){
  int i=(int)rnd(16);
  next[i]=ch?(next[i]==EMPTY?0:EMPTY):(next[i]^4);
 }
 for(int i=0;i<16;i++)p.pat[part][ch][i]=next[i];
}
static void scoped_generate(void){
 if(songScope==0){generate();return;}
 if(songScope==6){remix_sequence(songPart,p.sel);return;}
 static uint8_t candidate[NS][NC][16];
 LiquidHarmony h;
 liquid_song_generate(p.style,random32(),candidate,&h);
 if(songScope==4){
  for(int s=0;s<NS;s++)for(int j=0;j<4;j++)songHarmony.progression[s][j]=h.progression[s][j];
  return;
 }
 if(songScope==5){
  for(int i=6;i>1;i--){int j=1+(int)rnd(i);uint8_t t=songHarmony.arrangement[i];songHarmony.arrangement[i]=songHarmony.arrangement[j];songHarmony.arrangement[j]=t;}
  return;
 }
 for(int s=0;s<NS;s++)for(int ch=0;ch<NC;ch++){
  if((songScope==2&&s!=songPart)||(songScope==3&&ch!=p.sel))continue;
  for(int i=0;i<16;i++){
   uint8_t n=candidate[s][ch][i];
   if(songDensity<2 && i && rnd(4)<(unsigned)(2-songDensity))n=ch?EMPTY:0;
   if(songDensity>2 && rnd(8)<(unsigned)(songDensity-2)){
    if(!ch)n|=4;
    else if(n==EMPTY)n=(uint8_t)(rnd(4)*2);
   }
   p.pat[s][ch][i]=n;
  }
 }
 if(songScope==1){
  for(int s=0;s<NS;s++)for(int j=0;j<4;j++)songHarmony.progression[s][j]=h.progression[s][j];
  for(int i=0;i<8;i++)songHarmony.arrangement[i]=h.arrangement[i];
 }
}
static void transport(void){
 section=repeat?loopSection:songHarmony.arrangement[pos/64];bar=(pos/16)%4;step=pos%16;
 int cd=songHarmony.progression[section][bar];
 for(int c=0;c<8;c++){if(p.mute&(1<<c))continue;int d=p.pat[section][c][step];if(c==0){if(d)sound(c,d);}else if(d!=EMPTY)sound(c,d+cd);}
 pos=(pos+1)%512;
}

int liquid_weather_destination(void){return weather.place;}
int liquid_weather_room_active(void){return page==WEATHER;}

int liquid_weather_accept(unsigned destination,const LiquidWeather *reading){
 if(destination>=liquid_place_count||destination!=(unsigned)weather.place||!liquid_weather_valid(reading))return 0;
 weather.reading=*reading;
 weather.synced=millis();
 weather.available=1;
 return 1;
}

static void weather_select(int destination){
 if(weather.place==destination)return;
 weather.back=weather.place;
 weather.place=destination;
 weather.available=0;
 if(weather.playing){weather.step=0;weather.nextStep=millis();}
}

static unsigned offlineScene=0;
static int offlineHour=12,offlineTemp=18,offlineWind=12;
static uint32_t offlineClock;
static const uint8_t offlineCodes[]={0,3,45,51,61,71,95};
static LiquidWeather offline_weather(uint32_t now){
 LiquidWeather v={0};
 v.code=offlineCodes[offlineScene];
 v.temperature10=(int16_t)(offlineTemp*10);
 v.wind10=(uint16_t)(offlineWind*10);
 v.local_minute=(uint16_t)((offlineHour*60+(now-offlineClock)/60000u)%1440u);
 return v;
}
static int weather_fresh(uint32_t now){
 return weather.available && now-weather.synced<1800000u;
}

static void weather_generate(uint32_t now){
 int moon=weather.place==LIQUID_MOON;
 LiquidWeather current=weather_fresh(now)?weather.reading:offline_weather(now);
 if(!moon&&weather_fresh(now))current.local_minute=(uint16_t)((current.local_minute+(now-weather.synced)/60000u)%1440);
 weather.music=liquid_weather_music(&current,moon?0:liquid_places[weather.place].rhythm,moon);
 if(!weather.seed)weather.seed=random32();
 liquid_song_generate(weather.music.style,++weather.seed,weather.pattern,&weather.harmony);
 weather.step=0;
}

static void weather_transport(uint32_t now){
 if(!weather.playing||(int32_t)(now-weather.nextStep)<0)return;
 if(!weather.step){
  weather_generate(now);
 }
 int part=weather.harmony.arrangement[weather.step/64];
 int chord=weather.harmony.progression[part][weather.step/16%4];
 int root=weather.harmony.root+weather.music.transpose;
 int beat=weather.step%16;
 for(int ch=0;ch<NC;ch++){
  if(p.mute&(1<<ch))continue;
  int note=weather.pattern[part][ch][beat];
  if(!ch){
   note&=weather.music.drum_mask;
   if(note)sound_at_root(ch,note,root,part,weather.music.style,100);
  }else if(note!=EMPTY){
   int gain=ch==3?weather.music.lead_level:ch==4?weather.music.pad_level:100;
   sound_at_root(ch,note+chord,root,part,weather.music.style,gain);
  }
 }
 weather.step=(weather.step+1)%512;
 weather.nextStep=now+15000u/weather.music.bpm;
}

/* Preserve the original portrait; animate only the muzzle on note attacks.
 * Three mouth shapes follow the same note activity used by the stage. */
static void tidbit_mouth(int x,int y,int scale,int hit,uint32_t now){
 if(!hit)return;
 int phase=(int)((now/55u)%3u);
 int height=phase==1?3:2;
 uint16_t dark=RGB(42,19,30), tongue=RGB(233,116,148);
 rect(x+11*scale,y+11*scale,4*scale,height*scale,dark);
 if(height==3)rect(x+12*scale,y+13*scale,2*scale,scale,tongue);
}
static void battery_hud(void){
 int n=battery_bars();
 rect(207,0,33,18,BLACK);
 frame(208,3,28,12,WHITE);rect(236,6,3,6,WHITE);
 if(n<0){text(219,1,"?",WHITE,FONT_SMALL);return;}
 for(int i=0;i<n;i++)rect(211+i*8,6,6,6,n==1?RGB(255,155,65):RGB(100,220,150));
 if(n==0)rect(211,6,2,6,RGB(255,70,70));
}
/* Celsius tenths remain the internal weather/music unit. Round display to °F. */
static int weather_fahrenheit(int c10){
 int f50=c10*9+1600;
 return (f50+(f50>=0?25:-25))/50;
}
static void weather_hud(uint32_t now){
 const uint16_t peach=RGB(255,174,117), purple=RGB(183,137,235), dim=RGB(103,66,76);
 const uint16_t sky=RGB(18,17,37);
 char value[12];
 int moon=weather.place==LIQUID_MOON;
 int live=!moon&&weather_fresh(now);
 LiquidWeather demo=offline_weather(now);
 unsigned code=live?weather.reading.code:demo.code;
 unsigned minute=live?(weather.reading.local_minute+(now-weather.synced)/60000u)%1440:demo.local_minute;
 int night=minute<360||minute>=1080;
 int snow=code>=71&&code<=77;
 int rain=(code>=51&&code<=67)||(code>=80&&code<=82)||code>=95;
 rect(0,0,240,240,RGB(49,30,49));
 text(8,5,"TIDBIT WEATHER ROOM",peach,FONT_SMALL);
 text(8,22,moon?"THE MOON":liquid_places[weather.place].name,WHITE,FONT_SMALL);
 rect(61,43,166,105,night?sky:RGB(64,104,132));
 if(!moon&&code!=0){
  for(int i=0;i<3;i++){
   int cx=65+(int)((now/160u+i*53u)%128u);
   rect(cx,58+i%2*12,28,6,RGB(140,151,174));
   rect(cx+5,54+i%2*12,18,6,RGB(164,174,194));
  }
 }
 if(!moon&&code==45)for(int i=0;i<5;i++)rect(64,80+i*11,160,3,RGB(152,158,172));
 if(!moon&&code>=95&&now%2600u<100u){
  line(180,49,168,77,WHITE);line(168,77,181,75,WHITE);line(181,75,163,100,WHITE);
 }
 for(int i=0;i<8;i++){
  int x=65+i*20,h=12+(i*17)%35;
  rect(x,144-h,15,h,dim);
  for(int y=147-h;y<142;y+=8)rect(x+4,y,3,3,peach);
 }
 if(night||moon){rect(192,51,13,13,peach);rect(197,48,10,12,sky);}
 else rect(192,51,15,15,RGB(255,220,100));
 if(!moon&&(rain||snow))for(int i=0;i<29;i++){
  int x=65+(i*37+(snow?now/180:now/90))%155;
  int y=47+(i*19+now/(snow?180:35))%88;
  if(snow)rect(x,y,2,2,WHITE);
  else line(x,y,x-3,y+6,RGB(120,142,185));
 }
 frame(59,41,170,109,peach);
 line(144,42,144,148,peach);line(60,94,227,94,peach);
 line(31,39,31,65,peach);
 line(20,65,42,65,peach);line(20,65,10,84,peach);line(42,65,52,84,peach);
 line(10,84,52,84,peach);line(15,89,47,89,dim);
 rect(8,150,224,3,peach);
 rect(8,153,224,47,RGB(82,49,62));
 for(int i=0;i<6;i++)line(8,156+i*8,231,156+i*8,dim);
 rect(88,140,92,12,RGB(125,78,119));
 /* Actual website mascot, rendered at native room resolution rather than
  * enlarging the tiny sequencer portrait. Gentle note-driven bob. */
#ifdef TIDBIT_ROOM_HIRES
 int bob=(int32_t)(active[7]-now)>0 && !(p.mute&(1<<7))?2:0;
 for(int y=0;y<80;y++)for(int x=0;x<80;x++){
  uint16_t c=tidbitRoomPortrait[y*80+x];
  if(c)pixel(96+x,70+y-bob,c);
 }
#else
 for(int y=0;y<18;y++)for(int x=0;x<24;x++){
  uint16_t c=tidbitPortrait[y*24+x];
  if(c)rect(98+x*3,96+y*3,3,3,c);
 }
 tidbit_mouth(98,96,3,(int32_t)(active[7]-now)>0 && !(p.mute&(1<<7)),now);
#endif
 /* Plant, record player, mug and books. */
 rect(199,136,17,14,purple);
 line(207,136,207,116,purple);line(207,128,198,119,purple);
 line(207,124,216,113,purple);
 /* Compact h1-inspired turntable: thin peach line art, shallow perspective
  * and a dark platter. Keep it on the left shelf and out of Tidbit's window.
  * Reference: user's Rainy Night Tidbit Cozy Room.png. */
 int recordOn=weather.playing||playing;
 uint16_t deck=RGB(37,24,36),edge=RGB(229,152,111),groove=RGB(125,78,78);
 for(int y=170;y<192;y++){
  int inset=(191-y)/4;
  rect(13+inset,y,64-2*inset,1,deck);
 }
 line(18,170,72,170,edge);
 line(18,170,13,192,edge);line(72,170,77,192,edge);
 line(13,192,77,192,edge);line(13,192,13,198,edge);
 line(77,192,77,198,edge);line(13,198,77,198,edge);
 rect(14,193,62,5,RGB(45,29,39));
 /* Draw elliptical grooves with integer geometry, as on the reference deck. */
 for(int yy=-9;yy<=9;yy++)for(int xx=-22;xx<=22;xx++){
  int q=xx*xx*81+yy*yy*484;
  if(q>39204)continue;
  uint16_t c=q<=2600?RGB(192,121,96):RGB(25,20,31);
  if(q>=36300||(q>=25500&&q<=27300)||(q>=17000&&q<=18400)||(q>=9000&&q<=10100))c=groove;
  pixel(40+xx,181+yy,c);
 }
 /* Tiny moving mark rather than bright spinning highlights or floating notes. */
 uint32_t spin=recordOn?now*2386093u:0;
 int dx=(sine(spin+1073741824u)*16)>>15;
 int dy=(sine(spin)*6)>>15;
 pixel(40+dx,181+dy,edge);
 pixel(40,181,RGB(42,28,35));
 rect(65,171,4,3,edge);
 int elbowX=recordOn?58:69,elbowY=recordOn?177:181;
 int armX=recordOn?51:69,armY=recordOn?183:187;
 line(67,173,elbowX,elbowY,edge);line(elbowX,elbowY,armX,armY,edge);
 pixel(armX-1,armY,edge);
 rect(69,189,3,1,recordOn?edge:groove);

 frame(104,177,23,21,peach);frame(127,180,9,12,peach);
 for(int i=0;i<3;i++){int x=108+i*6;line(x,173,x+2,166-(int)((now/250+i)%3),dim);}
 for(int i=0;i<5;i++)frame(166+i*10,172-i%3*5,8,26+i%3*5,i%2?purple:peach);
 line(8,201,232,201,peach);
 if(live){
  text(8,205,liquid_weather_condition(code),WHITE,FONT_SMALL);
  text(8,38,"LIVE",RGB(90,220,150),FONT_SMALL);
  format_int(value,weather_fahrenheit(weather.reading.temperature10));text(170,205,value,WHITE,FONT_SMALL);text(215,205,"F",peach,FONT_SMALL);
  }else {
  text(8,205,moon?"LUNAR MODE":liquid_weather_condition(code),WHITE,FONT_SMALL);
  if(!moon){format_int(value,weather_fahrenheit(offlineTemp*10));text(115,205,value,WHITE,FONT_SMALL);text(150,205,"F",peach,FONT_SMALL); 
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  text(170,205,lw_weather_status(),purple,FONT_SMALL);
#else
  text(170,205,"DEMO",purple,FONT_SMALL);
#endif
}
 }
 format_int(value,live?weather.reading.wind10/10:offlineWind);
 if(!moon){text(8,105,"WIND",peach,FONT_SMALL);text(8,122,value,WHITE,FONT_SMALL);text(8,139,"KM/H",peach,FONT_SMALL);}
 if(live){
  char clock[6]={(char)('0'+minute/600),(char)('0'+minute/60%10),':',(char)('0'+minute%60/10),(char)('0'+minute%10),0};
  text(8,222,"LOCAL TIME",peach,FONT_SMALL);text(110,222,clock,WHITE,FONT_SMALL);
 }else text(8,222,"K1 SKY K2 HR K3 F K4 WIND",peach,FONT_SMALL);
 if(ambientLevel)text(8,35,"AMB",purple,FONT_SMALL);
 if(animalLevel)text(34,35,"CALL",purple,FONT_SMALL);
 if(weather.playing)rect(196,8,7,7,purple);
 battery_hud();
}

static const uint16_t waveColors[] = {RGB(28,72,105),RGB(42,124,157),RGB(103,212,250)};
/* A little banner plane crosses the sky above the foreground swimmers.
 * Independent random state: sky messages never change the song generator. */
static const char *const skyMessages[]={
 "KEEP GOING","YOU CAN DO IT","GOOD THINGS AHEAD","ENJOY THE MOMENT",
 "MAKE SOME JOY","BE KIND","STAY CURIOUS","YOU MATTER",
 "ONE STEP AT A TIME","LET YOURSELF SHINE","KEEP CREATING","FIND YOUR RHYTHM",
 "TODAY IS YOURS","SMALL WINS COUNT","GOOD VIBES","DREAM BIG"
};
static uint32_t skyStart,skySeed=1831565813u;
static unsigned skyMessage;
static int skyReady;
static void stage_plane(uint32_t now){
 if(!skyReady||now-skyStart>=16000u){
  skySeed^=skySeed<<13;skySeed^=skySeed>>17;skySeed^=skySeed<<5;
  unsigned next=skySeed%COUNT(skyMessages);
  if(skyReady&&next==skyMessage)next=(next+1)%COUNT(skyMessages);
  skyMessage=next;skyStart=now;skyReady=1;
 }
 const char *msg=skyMessages[skyMessage];
 int w=text_width(msg,FONT_SMALL)+12;
 int x=268-(int)((now-skyStart)/40u);
 int left=x+40;
 rect(0,48,240,42,BLACK);
 /* Exhaust puffs drift behind the left-facing plane, expand and fade. */
 for(int i=0;i<9;i++){
  int age=(int)((now/85u+i*7u)%63u);
  int px=x+24+age/2,py=65+((i*3+age/4)%9)-4;
  int rad=1+age/23;
  uint16_t shade=age<20?RGB(191,216,230):age<42?RGB(109,140,163):RGB(54,75,97);
  for(int dy=-rad;dy<=rad;dy++)for(int dx=-rad;dx<=rad;dx++)
   if(dx*dx+dy*dy<=rad*rad&&px+dx>=0&&px+dx<240)pixel(px+dx,py+dy,shade);
 }
 /* clipped banner and tow rope, with lettering only where fully visible */
 int lo=clamp(left,0,240),hi=clamp(left+w,0,240);
 if(hi>lo){
  rect(lo,57,hi-lo,22,RGB(255,233,173));
  line(lo,57,hi-1,57,RGB(240,143,228));
  line(lo,78,hi-1,78,RGB(240,143,228));
 }
 for(int i=0;msg[i];i++){
  int tx=left+6+i*8;char letter[2]={msg[i],0};
  if(tx>=0&&tx<=232)text(tx,60,letter,RGB(56,33,79),FONT_SMALL);
 }
 lo=clamp(x+26,0,239);hi=clamp(left,0,239);
 if(left>0&&x+26<240&&hi>lo)line(lo,65,hi,68,RGB(200,215,235));
 /* side profile, wings, tail, cockpit and spinning propeller */
 for(int k=0;k<27;k++){
  int px=x+26-k;if(px<0||px>=240)continue;
  if(k<23)rect(px,63,1,5,RGB(210,235,255));
  if(k<6)rect(px,58+k/2,1,5,RGB(180,140,240));
  if(k>=8&&k<=17)rect(px,68,1,5-(k-8)/3,RGB(180,140,240));
  if(k>=14&&k<=20)rect(px,60,1,3,RGB(103,212,250));
  if(k>=21&&k<=24)rect(px,64,1,3,RGB(255,196,80));
  if(k==26)rect(px,61+(int)((now/80u)%2u),1,8,WHITE);
 }
}
static void boot_wave(uint32_t elapsed) {
 static int previous=-1;
 if (previous<0) {
  text(32,72,"LIQUID WAVE",RGB(190,120,255),FONT_BIG);
  text(64,108,"ADVANCED BETA 2",WHITE,FONT_SMALL);
 }
 int front=(int)(elapsed*300u/BOOT_WAVE_MS);
 if(front==previous)return;
 for(int x=0;x<SCREEN_W;x+=3){
  if(previous>=0 && x<=previous && x>previous-55)rect(x,122,3,48,BLACK);
  int distance=front-x;
  if(distance>=0 && distance<55){
   int crest=141+((sine((uint32_t)distance*0x06400000u)*12)>>15);
   rect(x,crest,3,5,waveColors[0]);
   rect(x,crest+5,3,5,RGB(103,212,250));
   rect(x,crest+10,3,4,RGB(240,143,228));
  }
 }
 previous=front;
}
/* LCD adaptations of RandomCandyLand Liquid Wave's character shapes and palette.
 * Bass Cat belongs on the stage; Tidbit's weather room has no cat. */
static void mascot_ellipse(int x,int y,int rx,int ry,uint16_t c){
 for(int dy=-ry;dy<=ry;dy++)for(int dx=-rx;dx<=rx;dx++)
  if(dx*dx*ry*ry+dy*dy*rx*rx<=rx*rx*ry*ry)pixel(x+dx,y+dy,c);
}
static void portrait_at(int i,int x,int y){
 static const uint16_t palette[]={RGB(201,139,90),RGB(180,140,240),RGB(255,226,122),RGB(127,208,255),RGB(255,179,217),RGB(143,240,164),RGB(255,210,166)};
 static const uint16_t light[]={RGB(243,210,176),RGB(236,220,255),RGB(255,246,201),RGB(223,244,255),RGB(255,227,241),RGB(215,255,224),RGB(255,240,224)};
 int muted=(p.mute&(1<<i))!=0;
 uint32_t now=millis();
 int hit=(int32_t)(active[i]-now)>0&&!muted;
 if(i==7){
  for(int row=0;row<18;row++)for(int tx=0;tx<24;tx++){
   uint16_t color=tidbitPortrait[row*24+tx];
   if(color)pixel(x+tx,y+row,muted?(uint16_t)((color&0xf7deu)>>1):color);
  }
  tidbit_mouth(x,y,1,hit,now);
 }else{
  uint16_t c=muted?RGB(169,163,173):palette[i], l=muted?RGB(214,209,218):light[i];
  uint16_t out=RGB(74,42,58), pink=RGB(255,194,216);
  int cx=x+12,cy=y+10;
  if(i==0){for(int k=-1;k<=1;k+=2){mascot_ellipse(cx+k*7,y+4,4,4,out);mascot_ellipse(cx+k*7,y+4,3,3,c);mascot_ellipse(cx+k*7,y+4,1,1,pink);}}
  if(i==1){for(int k=-1;k<=1;k+=2){for(int j=0;j<7;j++){int w=j/2;line(cx+k*6-w,y+1+j,cx+k*6+w,y+1+j,out);if(w>1)line(cx+k*6-w+1,y+1+j,cx+k*6+w-1,y+1+j,pink);}}}
  if(i==2){for(int k=-1;k<=1;k+=2){mascot_ellipse(cx+k*4,y+4,3,4,out);mascot_ellipse(cx+k*4,y+4,2,3,c);mascot_ellipse(cx+k*4,y+4,1,2,pink);}}
  if(i==3){mascot_ellipse(cx-9,cy,2,3,c);mascot_ellipse(cx+9,cy,2,3,c);for(int k=-1;k<=1;k++)mascot_ellipse(cx+k*2,y+3,1,3,c);}
  if(i==5){line(cx-4,y+5,cx-7,y+1,out);line(cx+4,y+5,cx+7,y+1,out);mascot_ellipse(cx-7,y+1,1,1,pink);mascot_ellipse(cx+7,y+1,1,1,pink);}
  if(i==4){mascot_ellipse(cx,y+1,4,1,RGB(255,196,0));for(int k=-1;k<=1;k++){mascot_ellipse(cx+k*6,cy,5,5,out);mascot_ellipse(cx+k*6,cy,4,4,c);}mascot_ellipse(cx,cy-3,6,5,c);}
  else if(i==6){mascot_ellipse(cx,cy,8,7,out);rect(cx-8,cy,17,6,out);rect(cx-7,cy,15,5,c);mascot_ellipse(cx,cy,7,6,c);for(int k=-1;k<=1;k++)mascot_ellipse(cx+k*5,y+16,2,1,c);line(cx-4,y+4,cx,y,out);line(cx,y,cx+4,y+4,out);rect(cx-2,y+2,5,2,pink);pixel(cx,y,WHITE);}
  else {mascot_ellipse(cx,cy,9,7,out);mascot_ellipse(cx,cy,8,6,c);}
  if(i==0||i==1||i==3)mascot_ellipse(cx,cy+3,4,3,l);
  int blink=muted||((now/130u+i*3u)%37u==0);
  for(int k=-1;k<=1;k+=2){
   int ex=cx+k*3;
   if(blink)line(ex-1,cy-1,ex+1,cy-1,out);
   else {mascot_ellipse(ex,cy-1,1,2,out);pixel(ex,cy-2,WHITE);}
   pixel(cx+k*6,cy+2,pink);
  }
  if(i==0||i==1)pixel(cx,cy+1,i==1?pink:out);
  if(i==1){for(int k=-1;k<=1;k+=2){line(cx+k*6,cy+2,cx+k*11,cy+1,out);line(cx+k*6,cy+3,cx+k*11,cy+4,out);}}
  if(hit){mascot_ellipse(cx,cy+3,2,2,out);pixel(cx,cy+4,pink);}
  else if(i==3){line(cx-2,cy+2,cx+2,cy+2,RGB(255,181,74));pixel(cx,cy+3,RGB(255,181,74));}
  else if(i==6)mascot_ellipse(cx,cy+3,1,1,out);
  else {line(cx-2,cy+2,cx-1,cy+3,out);line(cx-1,cy+3,cx+1,cy+3,out);line(cx+1,cy+3,cx+2,cy+2,out);}
  if(i==2){pixel(cx,cy+4,WHITE);pixel(cx-2,y+17,pink);pixel(cx+2,y+17,pink);}
 }
 if(i==p.sel)frame(x-1,y-1,26,20,WHITE);
}
/* Surface is drawn over the lower part of each swimmer; foreground foam is
 * deliberately separate from the distant whitecapped waves. */
enum { SWIM_FLOAT, SWIM_PADDLE, SWIM_SURF, SWIM_DIVE, SWIM_LEAP, SWIM_SPLASH };
static uint8_t swimMode[NC];
static uint32_t swimSince[NC], swimDuration[NC];
static int surface_at(int x,uint32_t now){
 return SURF_Y+((sine((uint32_t)x*(1u<<25)-now*410000u)*5)>>15);
}
static void surface_patch(int x,int w){
 uint32_t now=millis();
 for(int k=x;k<x+w;k+=4){
  int n=clamp(x+w-k,1,4),y=surface_at(k,now);
  rect(k,137,n,y-137,BLACK);
  rect(k,y,n,192-y,SURF_COLOR);
  line(k,y,k+n-1,y,RGB(124,211,233));
  if(y<SURF_Y-2)line(k,y-1,k+n-1,y-1,RGB(219,248,255));
  if(((k/4+(int)(now/240u))%13)==0)line(k,176,k+n-1,176,RGB(42,124,157));
 }
}
static void faceDraw(int i){
 Face *a=&f[i];
 int water=surface_at(a->x+12,millis());
 if(a->y<water){
  portrait_at(i,a->x,a->y);
  int lower=a->y+19-water;
  if(lower>0){rect(a->x-1,water,26,lower,SURF_COLOR);line(a->x-1,water,a->x+24,water,RGB(124,211,233));}
 }
 a->ox=a->x;a->oy=a->y;
}
static int stage_singing(int i,uint32_t now){
 return !(p.mute&(1<<i))&&(playing||(int32_t)(active[i]-now)>0);
}
static void swimmer_step(int i,uint32_t now){
 Face *a=&f[i];
 if(stage_singing(i,now)){
  int bob=sine(now*260000u+(uint32_t)i*536870912u)>>14;
  /* Singing performers stay entirely above the highest local wave. */
  a->y=surface_at(a->x+12,now)-25+bob;
  if(now/170u%2u==0)a->x+=a->vx;
  if(a->x<8){a->x=8;a->vx=1;}
  if(a->x>208){a->x=208;a->vx=-1;}
  swimDuration[i]=0;
  return;
 }
 if(!swimDuration[i]||now-swimSince[i]>=swimDuration[i]){
  unsigned next=(swimMode[i]+1+rnd(5))%6;
  swimMode[i]=(uint8_t)next;swimSince[i]=now;
  static const uint16_t durations[]={4200,3600,3200,2600,2400,2100};
  swimDuration[i]=durations[next]+rnd(1700);
  if(rnd(3)==0)a->vx=-a->vx;
 }
 uint32_t age=now-swimSince[i],duration=swimDuration[i];
 unsigned progress=(unsigned)(age*256u/duration);
 int arc=clamp(sine(progress<<23),0,32767);
 int water=surface_at(a->x+12,now);
 int bob=sine(now*260000u+(uint32_t)i*536870912u)>>13;
 int target=water-16+bob,dx=0;
 switch(swimMode[i]){
  case SWIM_FLOAT: target=water-16+bob;dx=(now/200u+i)%2==0?1:0;break;
  case SWIM_PADDLE: target=water-12+bob;dx=1;break;
  case SWIM_SURF: target=water-22-(arc*13>>15);dx=2+(arc>>14);break;
  case SWIM_DIVE: target=water-14+(arc*30>>15);dx=1;break;
  case SWIM_LEAP: target=water-15-(arc*29>>15);dx=2;break;
  case SWIM_SPLASH: target=water-11+bob;dx=1;break;
 }
 a->x+=a->vx*dx;
 if(a->x<8){a->x=8;a->vx=1;}
 if(a->x>208){a->x=208;a->vx=-1;}
 a->y+=clamp(target-a->y,-3,3);
}
static void stage_splash(int i,uint32_t now){
 if(stage_singing(i,now))return;
 Face *a=&f[i];int cx=a->x+12,water=surface_at(cx,now);
 uint32_t age=now-swimSince[i];unsigned t=(age/80u)%10u;
 int mode=swimMode[i],cross=a->y+9-water;
 if(mode==SWIM_SURF){
  int tail=clamp(cx-a->vx*15,0,239);
  line(tail,water+2,cx,water-1,RGB(222,249,255));
  line(clamp(tail-a->vx*6,0,239),water+5,cx,water+3,RGB(124,211,233));
 }
 if(mode==SWIM_DIVE&&a->y>=water){
  for(int k=0;k<3;k++){
   int x=clamp(cx+(k-1)*5,1,238),y=water+4+(int)((age/100u+k*4u)%15u);
   frame(x,y,3,3,RGB(115,211,239));
  }
 }
 if(mode==SWIM_FLOAT||((cross< -7||cross>7)&&mode!=SWIM_SPLASH&&mode!=SWIM_SURF))return;
 uint16_t foam=RGB(222,249,255);
 for(int k=-1;k<=1;k+=2){
  int x=clamp(cx+k*(5+(int)t),1,238);
  int y=water-3-(int)(t*(10u-t)/4u);
  pixel(x,y,foam);pixel(clamp(x+k*2,1,238),y+3,RGB(115,211,239));
  line(clamp(cx+k*3,1,238),water+2,x,water+2,foam);
 }
}
static void stage_surface(void){surface_patch(0,240);}
enum { PART_LEVEL, PART_PITCH, PART_DEPTH, PART_DECAY, PART_RATIO, PART_PAN, PART_PARAMS };
enum { MOT_ATTACK=PART_PARAMS,MOT_RATE,MOT_DEPTH,MOT_PARAMS };
static const char *partParams[]={"LEVEL","PITCH","FM / DRIVE","DECAY","FM RATIO","PAN","ATTACK MS","LFO RATE","LFO DEPTH"};
static int motion_value(int ch,int param);
static void motion_adjust(int ch,int param,int d);
static int8_t *part_value(SectionSound *part,int param){
 switch(param){
  case PART_LEVEL:return &part->level;
  case PART_PITCH:return &part->pitch;
  case PART_DEPTH:return &part->depth;
  case PART_DECAY:return &part->decay;
  case PART_RATIO:return &part->ratio;
  default:return &part->pan;
 }
}
static void tune_part(SectionSound *part,int param,int delta){
 static const int limit[]={100,24,72,8,7,100};
 int8_t *value=part_value(part,param);
 *value=(int8_t)clamp(*value+delta,-limit[param],limit[param]);
}
static int motion_value(int ch,int param){
 if(param==MOT_ATTACK)return attackMs[ch];
 if(param==MOT_RATE)return lfoRate[ch];
 if(param==MOT_DEPTH)return motion[ch];
 return *part_value(&sectionMix[editSection][ch],param);
}
static void motion_adjust(int ch,int param,int d){
 if(param==MOT_ATTACK){if(ch)attackMs[ch]=clamp(attackMs[ch]+d,0,100);}
 else if(param==MOT_RATE){if(ch)lfoRate[ch]=clamp(lfoRate[ch]+d,0,20);}
 else if(param==MOT_DEPTH){if(ch)motion[ch]=clamp(motion[ch]+d,0,32);}
 else if(param!=PART_RATIO||ch)tune_part(&sectionMix[editSection][ch],param,d);
}
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
/* Distinct native pages share the renderer, not the parameter set. */
static const uint8_t env_params[]={MOT_ATTACK,PART_DECAY,PART_LEVEL};
static const uint8_t lfo_params[]={MOT_RATE,MOT_DEPTH};
static const uint8_t edit_params[]={PART_LEVEL,PART_PITCH,PART_DEPTH,PART_DECAY,PART_RATIO,PART_PAN};
static const uint8_t *native_edit_params(int *count){
 if(page==ENV){*count=COUNT(env_params);return env_params;}
 if(page==MOTION){*count=COUNT(lfo_params);return lfo_params;}
 *count=COUNT(edit_params);return edit_params;
}
static int native_edit_page(void){return page==ENV||page==MOTION||page==SECTION_SOUND;}
static void native_edit_scroll(int d){
 int count;const uint8_t *params=native_edit_params(&count);
 int i=0;while(i<count-1&&params[i]!=editParameter)i++;
 editParameter=params[wrap(i+d,count)];
}
#endif
static void motion_hud(void){
 uint16_t violet=RGB(190,120,255),ink=RGB(103,212,250);
 char value[12];
 rect(0,0,240,240,BLACK);
 text(8,3,"LIQUID WAVE",violet,FONT_SMALL);battery_hud();
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 text(8,22,page==ENV?"ENVELOPE":page==MOTION?"LFO / MOTION":"SECTION SOUND EDIT",WHITE,FONT_SMALL);
#else
 text(8,22,"MOTION / ENVELOPE",WHITE,FONT_SMALL);
#endif
 text(8,42,names[p.sel],colors[p.sel],FONT_SMALL);
 text(80,42,instrument(p.sel)->name,WHITE,FONT_SMALL);
 text(8,60,"K1",violet,FONT_SMALL);
 text(35,60,sections[editSection],ink,FONT_SMALL);
 text(120,60,editParameter>=PART_PARAMS?"ALL SECTIONS":"SECTION",ink,FONT_SMALL);
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 int count;const uint8_t *params=native_edit_params(&count);
 for(int row=0;row<count;row++){
  int i=params[row],y=80+row*18;
  uint16_t color=i==editParameter?RGB(255,190,237):WHITE;
  if(i==editParameter)rect(3,y,234,17,RGB(50,30,70));
  text(8,y,i==editParameter?">":" ",color,FONT_SMALL);
  text(24,y,partParams[i],color,FONT_SMALL);
  if(!p.sel&&(i==PART_RATIO||i>=PART_PARAMS))text(176,y,"N/A",color,FONT_SMALL);
  else {format_int(value,motion_value(p.sel,i));text(176,y,value,color,FONT_SMALL);}
 }
 text(8,190,"K2 PARAM / K3 VALUE",violet,FONT_SMALL);
 text(8,207,"K1 PART / SELECT TRACK",WHITE,FONT_SMALL);
 text(8,224,page==ENV?"K4 DECAY / HOME":page==MOTION?"K4 DEPTH / HOME":"K4 PAN / HOME",ink,FONT_SMALL);
#else
 int first=clamp(editParameter-2,0,MOT_PARAMS-6);
 for(int i=first;i<first+6;i++){
  int y=80+(i-first)*18;
  uint16_t color=i==editParameter?RGB(255,190,237):WHITE;
  if(i==editParameter)rect(3,y,234,17,RGB(50,30,70));
  text(8,y,i==editParameter?">":" ",color,FONT_SMALL);
  text(24,y,partParams[i],color,FONT_SMALL);
  if(!p.sel&&(i==PART_RATIO||i>=PART_PARAMS))text(176,y,"N/A",color,FONT_SMALL);
  else {format_int(value,motion_value(p.sel,i));text(176,y,value,color,FONT_SMALL);}
 }
 text(8,190,"K2 PARAM / K3 VALUE",violet,FONT_SMALL);
 text(8,207,"K4 PAN / SELECT CHARACTER",WHITE,FONT_SMALL);
 text(8,224,"SEQ INSTRUMENT / HOME",ink,FONT_SMALL);
#endif
}
static void song_details_hud(void){
 char value[12];
 static const char *keys[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
 static const char *degrees[]={"I","II","III","IV","V","VI","VII"};
 static const char *shortSection[]={"I","V","C","B","O"};
 static const char *densityNames[]={"SPARSE","LIGHT","ORIGINAL","RICH","DENSE"};
 uint16_t accent=RGB(183,137,235),ink=RGB(103,212,250);
 rect(0,0,240,240,BLACK);
 text(8,3,"3 / SONG LAB",accent,FONT_SMALL);battery_hud();
 text(8,22,liquid_song_style_name(p.style),WHITE,FONT_SMALL);
 format_int(value,p.bpm);text(112,22,value,WHITE,FONT_SMALL);text(144,22,"BPM",WHITE,FONT_SMALL);
 text(184,22,keys[wrap(songHarmony.root,12)],ink,FONT_SMALL);
 text(8,40,"K1 TEMPO  K2 KEY",accent,FONT_SMALL);
 text(8,58,"K3",accent,FONT_SMALL);text(34,58,densityNames[songDensity],WHITE,FONT_SMALL);
 text(8,76,"K4",accent,FONT_SMALL);text(34,76,songScopes[songScope],WHITE,FONT_SMALL);
 text(8,94,"OCT",accent,FONT_SMALL);text(65,94,sections[songPart],WHITE,FONT_SMALL);
 for(int j=0;j<4;j++)text(127+j*27,94,degrees[songHarmony.progression[songPart][j]%7],ink,FONT_SMALL);
 text(8,112,"SELECT",accent,FONT_SMALL);text(65,112,names[p.sel],colors[p.sel],FONT_SMALL);
 text(8,130,"ALGO",accent,FONT_SMALL);text(65,130,instrument(p.sel)->name,WHITE,FONT_SMALL);
 for(int i=0;i<8;i++){
  int x=9+i*29;
  frame(x,155,25,18,playing&&pos/64==i?WHITE:colors[songHarmony.arrangement[i]]);
  text(x+7,157,shortSection[songHarmony.arrangement[i]],WHITE,FONT_SMALL);
 }
 text(8,177,songScope==6?"ARP: ONLY THIS SEQUENCE":"PRESET STYLE / SEL MUTE",accent,FONT_SMALL);
 text(8,195,playing?"PLAY STOP / SAVE WHEN STOP":"PLAY START / SAVE SONG",WHITE,FONT_SMALL);
 text(8,222,"ARP REMIX / HOME RADIO",ink,FONT_SMALL);
}
static void radio_hud(void){
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 if(native_radio_mode)lw_radio_draw();else lw_wifi_draw();battery_hud();return;
#endif
 rect(0,0,240,240,BLACK);
 text(8,5,"4 / ONLINE RADIO",RGB(103,212,250),FONT_SMALL);
 text(8,36,"INTERNET RADIO",WHITE,FONT_BIG);
 text(8,80,"NETWORK NOT AVAILABLE",WHITE,FONT_SMALL);
 text(8,101,"Native Wi-Fi and stream",WHITE,FONT_SMALL);
 text(8,118,"decoder not connected.",WHITE,FONT_SMALL);
 text(8,159,"No station is playing.",WHITE,FONT_SMALL);
 text(8,222,"HOME / RETURN TO STAGE",WHITE,FONT_SMALL);battery_hud();
}
static void catalog_hud(void){
 uint16_t purple=RGB(190,120,255),blue=RGB(103,212,250);
 char val[12];
 rect(0,0,240,240,BLACK);
 text(8,3,"5 / SONG LIBRARY",purple,FONT_SMALL);battery_hud();
 int first=clamp(catalogIndex-2,0,COUNT(catalogSongs)-6);
 for(int i=0;i<6;i++){
  int n=first+i,y=27+i*20;
  if(n==catalogIndex)rect(3,y,234,18,RGB(58,35,78));
  text(8,y,n==catalogIndex?">":" ",purple,FONT_SMALL);
  text(24,y,catalogSongs[n].title,WHITE,FONT_SMALL);
 }
 text(8,151,catalogPlaying?"NOW PLAYING":"READY / SELECT SONG",blue,FONT_SMALL);
 format_int(val,catalogIndex+1);text(184,151,val,WHITE,FONT_SMALL);text(208,151,"/12",WHITE,FONT_SMALL);
 rect(8,171,224,6,RGB(45,37,57));
 if(catalogPlaying)rect(8,171,catalogEvent*224/catalogSongs[catalogIndex].count,6,purple);
 text(8,181,catalogPatches[catalogPatch],WHITE,FONT_SMALL);
 format_int(val,catalogVolume);text(176,181,val,blue,FONT_SMALL);
 text(8,201,"ALGO SOUND / K1 LEVEL",purple,FONT_SMALL);
 text(8,222,"PLAY / ARP RANDOM / HOME",WHITE,FONT_SMALL);
}
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
enum { SF_DRIVE,SF_DELAY,SF_ROOM,SF_CHORUS,SF_FILTER,SF_LEVEL,SF_DEPTH,SF_DECAY,SF_MUTE,SF_COUNT };
static int sound_menu_value(int ch){
 switch(fxParameter){
 case SF_DRIVE:return drive[ch];
 case SF_DELAY:return fxDelay;
 case SF_ROOM:return fxReverb;
 case SF_CHORUS:return fxChorus;
 case SF_FILTER:return fxFilter;
 case SF_LEVEL:return level[ch];
 case SF_DEPTH:return toneDepth[ch];
 case SF_DECAY:return tails[ch];
 default:return (p.mute>>ch)&1;
 }
}
static void sound_menu_adjust(int ch,int d){
 switch(fxParameter){
 case SF_DRIVE:drive[ch]=clamp(drive[ch]+d,0,40);break;
 case SF_DELAY:fxDelay=clamp(fxDelay+d,0,50);break;
 case SF_ROOM:fxReverb=clamp(fxReverb+d,0,50);break;
 case SF_CHORUS:fxChorus=clamp(fxChorus+d,0,50);break;
 case SF_FILTER:fxFilter=clamp(fxFilter+d,0,100);break;
 case SF_LEVEL:level[ch]=clamp(level[ch]+d,0,100);break;
 case SF_DEPTH:toneDepth[ch]=clamp(toneDepth[ch]+d,0,72);break;
 case SF_DECAY:tails[ch]=clamp(tails[ch]+d,ch?9:5,ch?17:14);break;
 default:if(d>0)p.mute|=1u<<ch;else p.mute&=~(1u<<ch);break;
 }
}
static void sound_menu_hud(void){
 static const char *const label[]={"DRIVE","DELAY","REVERB","CHORUS","FILTER","LEVEL","FM DEPTH","DECAY","MUTE"};
 uint16_t accent=RGB(190,120,255),cyan=RGB(103,212,250);
 char b[12];
 rect(0,0,240,240,BLACK);
 text(8,4,"SOUND / FX",accent,FONT_SMALL);battery_hud();
 text(8,24,names[p.sel],colors[p.sel],FONT_SMALL);
 text(80,24,instrument(p.sel)->name,WHITE,FONT_SMALL);
 text(8,43,fxParameter>=SF_DELAY&&fxParameter<=SF_FILTER?"GLOBAL EFFECT":"INSTRUMENT",cyan,FONT_SMALL);
 int first=clamp(fxParameter-2,0,SF_COUNT-6);
 for(int i=first;i<first+6;i++){
  int y=64+(i-first)*19;
  if(i==fxParameter)rect(3,y,234,18,RGB(50,30,70));
  text(8,y,i==fxParameter?">":" ",WHITE,FONT_SMALL);
  text(25,y,label[i],WHITE,FONT_SMALL);
  int old=fxParameter;fxParameter=i;int value=sound_menu_value(p.sel);fxParameter=old;
  format_int(b,value);text(184,y,b,cyan,FONT_SMALL);
 }
 text(8,182,"SELECT / K2: SCROLL",accent,FONT_SMALL);
 text(8,200,"K1 TRACK / K3 VALUE",WHITE,FONT_SMALL);
 text(8,220,"SEL BACK / HOME STAGE",cyan,FONT_SMALL);
}
#endif
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
/* An independent, single repeating pattern. Never writes p.pat or songHarmony. */
static uint8_t lb_notes[NC][16],lb_undo[16];
static int lb_ready,lb_track,lb_cursor,lb_note,lb_length=16,lb_bpm=110;
static int lb_playing,lb_step,lb_last=-1,lb_undo_track=-1,lb_variation;
static uint32_t lb_due,lb_rem;
static const char *const lb_tracks[]={"DRUMS","BASS","KEYS","LEAD","PAD","PLUCK","FX","TIDBIT"};
static int lb_on(int ch,int i){return ch?lb_notes[ch][i]!=EMPTY:lb_notes[ch][i]!=0;}
static void lb_snapshot(void){
 lb_undo_track=lb_track;
 for(int i=0;i<16;i++)lb_undo[i]=lb_notes[lb_track][i];
}
static void lb_fill(void){
 lb_snapshot();
 int variation=lb_variation++%3;
 for(int i=0;i<16;i++){
  if(!lb_track){
   int kick=variation==0?i%4==0:variation==1?(i==0||i==6||i==8||i==14):(i==0||i==3||i==8||i==11);
   lb_notes[0][i]=(kick?1:0)|(i%8==4?2:0)|(i%2==0?4:0)|(i==15&&variation?8:0);
  }else {
   int interval=lb_track==1?4:lb_track==4?8:variation==1?3:4;
   static const uint8_t degrees[]={0,2,4,2,5,4,2,0};
   lb_notes[lb_track][i]=i%interval==0?degrees[(i/interval+variation)%8]:EMPTY;
  }
 }
}
static void lb_init(void){
 if(lb_ready)return;
 lb_ready=1;
 for(int c=0;c<NC;c++)for(int i=0;i<16;i++)lb_notes[c][i]=c?EMPTY:0;
 lb_fill();lb_undo_track=-1;
}
static void lb_stop(void){lb_playing=0;lb_last=-1;releaseRequested=1;}
static void lb_preview(int ch,int note){sound_at_root(ch,note,0,0,0,100);}
static void lb_toggle(int i){
 if(i<0||i>=lb_length)return;
 lb_snapshot();lb_cursor=i;
 if(lb_on(lb_track,i))lb_notes[lb_track][i]=lb_track?EMPTY:0;
 else {int note=lb_track?lb_note:1<<lb_note;lb_notes[lb_track][i]=note; if(!lb_playing)lb_preview(lb_track,note);}
}
static void lb_service(uint32_t now){
 if(!lb_playing)return;
 for(int budget=0;budget<4&&(int32_t)(now-lb_due)>=0;budget++){
  for(int c=0;c<NC;c++)if(lb_on(c,lb_step))lb_preview(c,lb_notes[c][lb_step]);
  lb_last=lb_step;lb_step=(lb_step+1)%lb_length;
  lb_due+=15000u/(unsigned)lb_bpm;lb_rem+=15000u%(unsigned)lb_bpm;
  if(lb_rem>=(unsigned)lb_bpm){lb_due++;lb_rem-=lb_bpm;}
 }
}
static void lb_input(uint32_t now){
 int d=encoder(ENC_SELECT);
 if(d){lb_track=wrap(lb_track+d,NC);lb_note=0;}
 d=encoder(ENC_KNOB1);if(d){lb_bpm=clamp(lb_bpm+d,60,180);lb_rem=0;}
 d=encoder(ENC_KNOB2);if(d)lb_cursor=wrap(lb_cursor+d,lb_length);
 d=encoder(ENC_KNOB3);if(d){
  lb_note=wrap(lb_note+d,lb_track?13:4);
  if(lb_on(lb_track,lb_cursor)){lb_snapshot();lb_notes[lb_track][lb_cursor]=lb_track?lb_note:1<<lb_note;}
  if(!lb_playing)lb_preview(lb_track,lb_track?lb_note:1<<lb_note);
 }
 d=encoder(ENC_KNOB4);if(d){
  int n=lb_length==4?0:lb_length==8?1:2;
  lb_length=4<<wrap(n+d,3);lb_step%=lb_length;lb_cursor%=lb_length;
 }
 if(button_pressed(BTN_PLAY)){if(lb_playing)lb_stop();else {lb_playing=1;lb_step=0;lb_due=now;lb_rem=0;}}
 if(button_pressed(BTN_ARP))lb_fill();
 if(button_pressed(BTN_SEL))lb_toggle(lb_cursor);
 if(button_pressed(BTN_EDIT)){lb_snapshot();for(int i=0;i<16;i++)lb_notes[lb_track][i]=lb_track?EMPTY:0;}
 if(button_pressed(BTN_REC)&&lb_undo_track>=0){
  lb_track=lb_undo_track;lb_note=0;
  for(int i=0;i<16;i++)lb_notes[lb_track][i]=lb_undo[i];
  lb_undo_track=-1;
 }
 for(int k=0;k<16;k++)if(key_pressed(k))lb_toggle(k);
 lb_service(now);
}
static void lb_hud(void){
 uint16_t purple=RGB(190,120,255),cyan=RGB(103,212,250);
 char b[12];rect(0,0,240,240,BLACK);
 text(8,3,"6 / LOOP BUILDER",purple,FONT_SMALL);battery_hud();
 text(8,22,lb_tracks[lb_track],colors[lb_track],FONT_SMALL);
 text(88,22,lb_playing?"PLAYING":"READY",cyan,FONT_SMALL);
 format_int(b,lb_bpm);text(178,22,b,WHITE,FONT_SMALL);
 text(8,40,"SELECT TRACK / K1 BPM",WHITE,FONT_SMALL);
 static const char *const drum[]={"KICK","SNARE","HAT","OPEN HAT"};
 text(8,57,"K3 NOTE",purple,FONT_SMALL);
 if(!lb_track)text(80,57,drum[lb_note],WHITE,FONT_SMALL);
 else {format_int(b,lb_note+1);text(80,57,b,WHITE,FONT_SMALL);}
 text(140,57,"K4 LEN",purple,FONT_SMALL);format_int(b,lb_length);text(211,57,b,WHITE,FONT_SMALL);
 for(int i=0;i<16;i++){
  int x=8+(i%4)*57,y=75+(i/4)*23;
  uint16_t c=i>=lb_length?RGB(22,22,25):lb_on(lb_track,i)?colors[lb_track]:RGB(52,43,65);
  rect(x,y,51,19,c);
  if(i==lb_cursor)frame(x,y,51,19,WHITE);
  if(lb_playing&&i==lb_last)frame(x+2,y+2,47,15,cyan);
  format_int(b,i+1);text(x+4,y+1,b,lb_on(lb_track,i)&&i<lb_length?BLACK:WHITE,FONT_SMALL);
 }
 text(8,169,"KEYS 1-16 / SEL TOGGLE",WHITE,FONT_SMALL);
 text(8,187,"K2 STEP / ARP AUTO FILL",purple,FONT_SMALL);
 text(8,205,"EDIT CLEAR / REC UNDO",WHITE,FONT_SMALL);
 text(8,222,"PLAY PREVIEW / HOME EXIT",cyan,FONT_SMALL);
}
#ifdef PLAT_HOST
#include <string.h>
int lw_loop_expect(const char *name,int *v){
 if(!strcmp(name,"loop_page"))*v=page==LOOP_BUILDER;
 else if(!strcmp(name,"loop_playing"))*v=lb_playing;
 else if(!strcmp(name,"loop_track"))*v=lb_track;
 else if(!strcmp(name,"loop_length"))*v=lb_length;
 else if(!strcmp(name,"loop_steps")){*v=0;for(int i=0;i<lb_length;i++)*v+=lb_on(lb_track,i);}
 else return 0;
 return 1;
}
#endif
#endif
static void hud(void){
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 if(page==LOOP_BUILDER){lb_hud();return;}
 if(page==FX){sound_menu_hud();return;}
#endif
 if(page==SONG_DETAILS){song_details_hud();return;}
 if(page==RADIO){radio_hud();return;}
 if(page==CATALOG){catalog_hud();return;}
 if(page==WEATHER){weather_hud(millis());return;}
 if(page==SECTION_SOUND||page==MOTION||page==ENV||page==EDIT){motion_hud();return;}
 if(page!=HOME){
  rect(0,48,240,145,BLACK);
  text(8,60,names[p.sel],colors[p.sel],FONT_SMALL);
  text(72,60,instrument(p.sel)->name,WHITE,FONT_SMALL);
  const char *labels[]={"LEVEL","FM DEPTH","DECAY","RATIO","MOTION","DRIVE"};
  int values[]={level[p.sel],toneDepth[p.sel],tails[p.sel],shape[p.sel]+1,motion[p.sel],drive[p.sel]};
  if(page==ENV){labels[0]="ATTACK MS";values[0]=attackMs[p.sel];}
  if(page==MOTION){labels[0]="LFO RATE";values[0]=lfoRate[p.sel];}
  if(page==FX){labels[0]="DRIVE";values[0]=drive[p.sel];labels[1]="DELAY";values[1]=fxDelay;labels[2]="ROOM";values[2]=fxReverb;labels[3]="CHORUS";values[3]=fxChorus;labels[4]="FILTER";values[4]=fxFilter;labels[5]="PARAM";values[5]=fxParameter+1;}
  if(page==GLOBAL){labels[0]="MASTER";values[0]=masterLevel;labels[1]="KEY";values[1]=transpose;labels[2]="OCTAVE";values[2]=octave;}
  if(page==SEQ){labels[0]="STEP";values[0]=cursor+1;labels[1]="LOOP";values[1]=repeat;labels[2]="SECTION";values[2]=section+1;}
  for(int i=0;i<(page==GLOBAL||page==SEQ?3:6);i++){char value[12];format_int(value,values[i]);text(8,80+i*17,labels[i],WHITE,FONT_SMALL);text(135,80+i*17,value,WHITE,FONT_SMALL);}
  if(page==SEQ){for(int i=0;i<NC;i++)portrait_at(i,3+30*i,151);text(8,175,"ARP NEW SEQ / K4 PART",RGB(190,120,255),FONT_SMALL);}
 }

 rect(0,0,240,48,BLACK);text(0,0,"LIQUID WAVE",RGB(190,120,255),FONT_SMALL);text(125,0,pageName[page],WHITE,FONT_SMALL);battery_hud();
 text(0,16,liquid_song_style_name(p.style),WHITE,FONT_SMALL);char b[12];format_int(b,p.bpm);text(112,16,b,WHITE,FONT_SMALL);if(page==HOME)text(168,16,"BETA 2",RGB(190,120,255),FONT_SMALL);
 const char *guide=page==SECTION_SOUND?"K1 SECTION / K2 PARAM":
  page==SEQ?"SEL TRACK / ALGO SOUND":page==FX?"K2 PARAM / K3 VALUE":page==ENV?"K2 ATTACK / K4 TAIL":page==MOTION?"K2 RATE / K3 DEPTH":(page==EDIT||page==HOME||page==MIX)?"SELECT / ALGO SOUND":!p.sel&&page!=GLOBAL&&page!=SEQ?"TEMPO  LEVEL DRIVE TAIL":knobGuide[page];
 text(0,32,guide,WHITE,FONT_SMALL);
 rect(0,194,240,46,BLACK);text(0,194,sections[section],WHITE,FONT_SMALL);
 text(96,194,names[p.sel],colors[p.sel],FONT_SMALL);text(192,194,recording?"REC":"",WHITE,FONT_SMALL);
 for(int i=0;i<16;i++){int d=p.pat[section][p.sel][i];uint16_t col=(p.sel?d!=EMPTY:d!=0)?colors[p.sel]:RGB(35,35,35);rect(i*15,218,12,12,col);if((playing&&i==step)||(page==SEQ&&i==cursor))frame(i*15,216,14,16,WHITE);}
}
static int load_song_settings(const SavedSong *saved,uint32_t magic){
 const Project *project=&saved->session.project;
 if(project->magic!=magic||project->style>=LW_STYLE_COUNT||project->sel>=NC||project->bpm<60||project->bpm>180)return 0;
 if(saved->harmony.root< -6||saved->harmony.root>5)return 0;
 for(int s=0;s<NS;s++)for(int i=0;i<4;i++)if(saved->harmony.progression[s][i]>=7)return 0;
 for(int i=0;i<LW_ARRANGEMENT;i++)if(saved->harmony.arrangement[i]>=NS)return 0;
 p=*project;
 songHarmony=saved->harmony;
 for(int ch=0;ch<NC;ch++)chooseInstrument(ch,saved->session.presets[ch]);
 return 1;
}
/* Dim locator lighting stays on even for unavailable controls.
 * Context-available buttons pulse at a calm 1 Hz; active modes/held
 * controls are steady bright. No dependence on display refresh rate. */
#define LIT(b) (1u << (b))
static void panel_lights(uint32_t now){
 uint32_t available=0,active=LIT(BTN_HOME);
 if(page==WEATHER){
  available=LIT(BTN_OCT_DOWN)|LIT(BTN_OCT_UP)|LIT(BTN_FX)|LIT(BTN_SEL)|LIT(BTN_ENV)|LIT(BTN_LFO)|LIT(BTN_PLAY);
  if(weather.playing||playing)active|=LIT(BTN_PLAY);
  if(ambientLevel)active|=LIT(BTN_ENV);
  if(animalLevel)active|=LIT(BTN_LFO);
 }else if(page==RADIO){
  /* Advertise only the controls implemented by this build. */
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  available=LIT(BTN_SEL)|LIT(BTN_SAVE)|LIT(BTN_GLO)|LIT(BTN_EDIT)|LIT(BTN_PLAY)|LIT(BTN_ARP)|LIT(BTN_SEQ);
#endif
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 }else if(page==LOOP_BUILDER){
  available=LIT(BTN_PLAY)|LIT(BTN_ARP)|LIT(BTN_SEL)|LIT(BTN_EDIT)|LIT(BTN_REC);
  if(lb_playing)active|=LIT(BTN_PLAY);
#endif
 }else if(page==CATALOG){
  available=LIT(BTN_PLAY)|LIT(BTN_ARP);
  if(catalogPlaying)active|=LIT(BTN_PLAY);
 }else{
  available=((1u<<BTN_COUNT)-1u)&~LIT(BTN_HOME);
  if(playing){active|=LIT(BTN_PLAY);available&=~LIT(BTN_SAVE);}
  if(recording)active|=LIT(BTN_REC);
  if(page==SEQ)active|=LIT(BTN_SEQ);
  if(page==FX)active|=LIT(BTN_FX);
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  if(page==ENV)active|=LIT(BTN_ENV);
  if(page==MOTION)active|=LIT(BTN_LFO);
  if(page==SECTION_SOUND)active|=LIT(BTN_EDIT);
#else
  if(page==SECTION_SOUND)active|=LIT(BTN_ENV)|LIT(BTN_LFO)|LIT(BTN_EDIT);
#endif
  if(page==GLOBAL)active|=LIT(BTN_GLO);
  if(p.mute&(1u<<p.sel))active|=LIT(BTN_SEL);
  if(page!=SONG_DETAILS){
   if(octave<=-2)available&=~LIT(BTN_OCT_DOWN);
   if(octave>=2)available&=~LIT(BTN_OCT_UP);
  }
 }
 uint32_t mask=active|((now%1000u<600u)?available:0u)|(buttons()&available);
 led_backlight(1);
 for(int b=0;b<BTN_COUNT;b++)led_button(b,(mask>>b)&1u);
 uint32_t held=keys();
 for(int k=0;k<KEY_COUNT;k++)led_key(k,(held>>k)&1u);
}
#undef LIT
void setup(void){
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 echoL=malloc(4096u*sizeof *echoL);echoR=malloc(4096u*sizeof *echoR);
 if(echoL&&echoR)for(unsigned i=0;i<4096u;i++){echoL[i]=0;echoR[i]=0;}
#endif
 clear(BLACK);p.magic=LEGACY_MAGIC;p.style=0;p.sel=0;p.mute=0;generate();
 SavedMix mixed;
 SavedSong full;
 SavedProject saved;
 if(settings_load(&mixed,sizeof(mixed))==0&&load_song_settings(&mixed.song,MIX_MAGIC)){
  for(int s=0;s<NS;s++)for(int ch=0;ch<NC;ch++){
   sectionMix[s][ch]=mixed.sound[s][ch];
   for(int param=0;param<PART_PARAMS;param++)tune_part(&sectionMix[s][ch],param,0);
  }
 }else if(settings_load(&full,sizeof(full))==0&&load_song_settings(&full,SONG_MAGIC)){
  /* Earlier builds did not have per-section sound offsets. */
 }else if(settings_load(&saved,sizeof(saved))==0&&saved.project.magic==LEGACY_MAGIC&&saved.project.style<LW_STYLE_COUNT&&saved.project.sel<8&&saved.project.bpm>=60&&saved.project.bpm<=180){
  p=saved.project;for(int ch=0;ch<NC;ch++)chooseInstrument(ch,saved.presets[ch]);
  liquid_song_legacy_harmony(p.style,&songHarmony);
 }else{
  Project legacy;if(settings_load(&legacy,sizeof(legacy))==0&&legacy.magic==LEGACY_MAGIC&&legacy.style<LW_STYLE_COUNT&&legacy.sel<8&&legacy.bpm>=60&&legacy.bpm<=180){
   p=legacy;
   liquid_song_legacy_harmony(p.style,&songHarmony);
  }
  for(int ch=0;ch<NC;ch++)chooseInstrument(ch,preset[ch]);
 }
 for(int i=0;i<8;i++){f[i].x=8+(i%4)*55;f[i].y=128+(i%4)*5;f[i].vx=rnd(2)?1:-1;f[i].vy=rnd(2)?1:-1;f[i].ox=f[i].x;f[i].oy=f[i].y;}
 led_backlight(1);
 bootStarted=millis();
 boot_wave(0);
#if FOMA_BLUETOOTH
 bt_enable(BT_MIDI);
#endif
}
void loop(void){
 uint32_t now=millis();
 int skippedBoot=0;
 if(booting){
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  /* The board can spend time initializing after setup(); count visible frames
   * from the first interactive loop rather than from early setup. */
  if(!native_boot_clock_started){native_boot_clock_started=1;bootStarted=now;}
#endif
  if(now-bootStarted<BOOT_WAVE_MS && !button_pressed(BTN_HOME) && !button_pressed(BTN_PLAY) && !keys()){
   boot_wave(now-bootStarted);
   return;
  }
  booting=0;
  skippedBoot=1;
  clear(BLACK);
  stage_plane(now);
  stage_surface();
  for(int i=0;i<NC;i++)faceDraw(i);
  lastFace=lastWave=lastHud=now;
  hud();
 }
#if FOMA_BLUETOOTH
 bluetooth_input();
#endif
 if(page==WEATHER){
  if(!button_pressed(BTN_HOME)){
   if(button_pressed(BTN_OCT_UP)){weather.octHeld=1;weather.octUsed=0;weather.octSince=now;}
   if(weather.octHeld && button_held(BTN_OCT_UP) && now-weather.octSince>=3000u && !weather.octUsed){
    weather_select(LIQUID_MOON);
    weather.octUsed=1;
    lastHud=now-1000u;
   }
   if(weather.octHeld && button_released(BTN_OCT_UP)){
    if(!weather.octUsed){
     int start=weather.place;
     int target=start<0?0:wrap(start+1+(int)rnd(liquid_place_count-1),liquid_place_count);
     weather_select(target);
     lastHud=now-1000u;
    }
    weather.octHeld=0;
   }
   if(button_pressed(BTN_OCT_DOWN)){weather_select(weather.back);lastHud=now-1000u;}
   if(button_pressed(BTN_PLAY)){
    if(weather.playing){weather.playing=0;releaseRequested=1;}
    else if(playing){playing=0;releaseRequested=1;}
    else {
     weather.playing=1;
     weather.nextStep=now;
     weather.step=0;
    }
    lastHud=now-1000u;
   }
   int wd=encoder(ENC_SELECT);if(wd)weather_select(wrap((weather.place<0?0:weather.place)+wd,liquid_place_count));
   wd=encoder(ENC_KNOB1);if(wd&&!weather_fresh(now)){offlineScene=wrap((int)offlineScene+wd,COUNT(offlineCodes));weather.available=0;weather.step=0;}
   wd=encoder(ENC_KNOB2);if(wd&&!weather_fresh(now)){offlineHour=wrap(offlineHour+wd,24);offlineClock=now;weather.available=0;weather.step=0;}
   wd=encoder(ENC_KNOB3);if(wd&&!weather_fresh(now)){offlineTemp=clamp(offlineTemp+wd,-30,45);weather.available=0;weather.step=0;}
   wd=encoder(ENC_KNOB4);if(wd&&!weather_fresh(now)){offlineWind=clamp(offlineWind+wd,0,100);weather.available=0;weather.step=0;}
   /* Dedicated weather controls: FX cycles texture, ENV toggles ambience,
      LFO toggles dog/wildlife level, SEL auditions the selected call. */
   if(button_pressed(BTN_FX))ambientMode=(ambientMode+1)%4;
   if(button_pressed(BTN_ENV))ambientLevel=ambientLevel?0:35;
   if(button_pressed(BTN_LFO))animalLevel=animalLevel?0:45;
   if(button_pressed(BTN_SEL)){if(!animalLevel)animalLevel=45;animalTrigger=1;}
   if(weather.playing){
    static const int8_t whiteKey[]={0,-1,1,-1,2,-1,3,4,-1,5,-1,6};
    int root=weather.harmony.root+weather.music.transpose;
    for(int k=0;k<KEY_COUNT;k++)if(key_pressed(k)&&whiteKey[k%12]>=0)
     sound_at_root(3,whiteKey[k%12]+7*(k/12),root,0,weather.music.style,100);
   }
   weather_transport(now);
   int catchup=0;
   while(playing && (int32_t)(now-due)>=0 && catchup++<4){
    transport();due+=15000/p.bpm;remMs+=15000%p.bpm;
    if(remMs>=p.bpm){remMs-=p.bpm;due++;}
   }
   if(now-lastHud>=70){lastHud=now;weather_hud(now);}
   panel_lights(now);
   return;
  }
  if(weather.playing)releaseRequested=1;
  ambientLevel=animalLevel=animalTrigger=0;
  weather.playing=0;
  weather.octHeld=0;
  page=SONG_DETAILS;
  song_details_hud();lastHud=now;panel_lights(now);return;
 }
 if(button_pressed(BTN_HOME)){
  if(page==SONG_DETAILS){page=RADIO;radio_hud();lastHud=now;panel_lights(now);return;}
  if(page==RADIO){
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 lw_wifi_leave();lw_radio_leave();
#endif
 if(playing){playing=0;releaseRequested=1;}page=CATALOG;catalog_hud();lastHud=now;panel_lights(now);return;}
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  if(page==CATALOG){catalog_stop();if(playing){playing=0;releaseRequested=1;}lb_init();page=LOOP_BUILDER;lb_hud();lastHud=now;panel_lights(now);return;}
  if(page==LOOP_BUILDER)lb_stop();
#endif
  if(page==CATALOG)catalog_stop();
  if(page==HOME&&!skippedBoot){
   page=WEATHER;
   weather_hud(now);
   lastHud=now;
   panel_lights(now);
   return;
  }
  page=HOME;
  clear(BLACK);
  stage_plane(now);
  stage_surface();
  for(int i=0;i<NC;i++)faceDraw(i);
 }
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 if(page==LOOP_BUILDER){
  lb_input(now);
  if(now-lastHud>=70){lastHud=now;lb_hud();}
  panel_lights(now);return;
 }
#endif
 if(page==CATALOG){
  int d=encoder(ENC_SELECT);d+=encoder(ENC_PRESETS);
  if(d){catalog_stop();catalogIndex=wrap(catalogIndex+d,COUNT(catalogSongs));}
  d=encoder(ENC_ALGORITHM);if(d)catalogPatch=wrap(catalogPatch+d,COUNT(catalogPatches));
  d=encoder(ENC_KNOB1);if(d)catalogVolume=clamp(catalogVolume+d,0,100);
  if(button_pressed(BTN_ARP)){catalog_stop();catalogIndex=wrap(catalogIndex+1+(int)rnd(COUNT(catalogSongs)-1),COUNT(catalogSongs));}
  if(button_pressed(BTN_PLAY)){if(catalogPlaying)catalog_stop();else catalog_start(now);}
  catalog_playback(now);
  if(now-lastHud>=90){lastHud=now;catalog_hud();}
  panel_lights(now);return;
 }
 if(page==RADIO){
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  if(button_pressed(BTN_ARP)){native_radio_mode=!native_radio_mode;lw_wifi_leave();lw_radio_leave();lastHud=now-1000u;}
  else if(native_radio_mode){if(lw_radio_ui())lastHud=now-1000u;}
  else if(lw_wifi_step())lastHud=now-1000u;
#endif
  if(now-lastHud>=1000){lastHud=now;radio_hud();}
  int catchup=0;while(playing&&(int32_t)(now-due)>=0&&catchup++<4){transport();due+=15000/p.bpm;remMs+=15000%p.bpm;if(remMs>=p.bpm){remMs-=p.bpm;due++;}}
  panel_lights(now);return;
 }
 if(button_pressed(BTN_SEL)){
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  if(page==HOME||page==MIX){page=FX;lastHud=now-1000u;}
  else if(page==FX){page=MIX;clear(BLACK);lastHud=now-1000u;}
  else p.mute^=1<<p.sel;
#else
  p.mute^=1<<p.sel;
#endif
 }
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
 if(button_pressed(BTN_ENV)){page=ENV;editSection=section;editParameter=MOT_ATTACK;lastHud=now-1000u;}
 if(button_pressed(BTN_LFO)){page=MOTION;editSection=section;editParameter=MOT_RATE;lastHud=now-1000u;}
 if(button_pressed(BTN_EDIT)){page=SECTION_SOUND;editSection=section;editParameter=PART_LEVEL;lastHud=now-1000u;}
#else
 if(button_pressed(BTN_ENV)||button_pressed(BTN_LFO)||button_pressed(BTN_EDIT)){
  if(page!=SECTION_SOUND){editSection=section;editParameter=PART_LEVEL;}
  page=SECTION_SOUND;
 }
#endif
 if(button_pressed(BTN_GLO))page=GLOBAL;
 if(button_pressed(BTN_FX))page=FX;
 if(button_pressed(BTN_SEQ)){if(page==SEQ)repeat=!repeat;page=SEQ;loopSection=section;}
 if(button_pressed(BTN_PLAY)){playing=!playing;if(playing){pos=0;due=now;remMs=0;}else releaseRequested=1;}
 if(button_pressed(BTN_REC))recording=!recording;
 if(button_pressed(BTN_OCT_DOWN)){if(page==SONG_DETAILS)songPart=wrap(songPart-1,NS);else octave=clamp(octave-1,-2,2);}
 if(button_pressed(BTN_OCT_UP)){if(page==SONG_DETAILS)songPart=wrap(songPart+1,NS);else octave=clamp(octave+1,-2,2);}
 if(button_pressed(BTN_ARP)){
  if(page==SEQ)remix_sequence(section,p.sel);
  else if(page==SONG_DETAILS){scoped_generate();if(songScope<=1){pos=0;due=now;remMs=0;}}
  else {generate();pos=0;due=now;remMs=0;}
 }
 if(button_pressed(BTN_SAVE)&&!playing){
  SavedMix saved={0};
  saved.song.session.project=p;
  saved.song.session.project.magic=MIX_MAGIC;
  for(int ch=0;ch<NC;ch++)saved.song.session.presets[ch]=preset[ch];
  saved.song.harmony=songHarmony;
  for(int s=0;s<NS;s++)for(int ch=0;ch<NC;ch++)saved.sound[s][ch]=sectionMix[s][ch];
  settings_save(&saved,sizeof(saved));
 }
 int d=encoder(ENC_SELECT);if(d){
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  if(page==FX)fxParameter=wrap(fxParameter+d,SF_COUNT);
  else
#endif
  {p.sel=(uint8_t)wrap(p.sel+d,8);if(page==HOME)page=MIX;}
 }
 d=encoder(ENC_PRESETS);if(d){p.style=(uint8_t)wrap(p.style+d,LW_STYLE_COUNT);if(page==SONG_DETAILS)scoped_generate();else generate();pos=0;due=now;remMs=0;}
 int ch=p.sel;
 d=encoder(ENC_ALGORITHM);if(d){
  if(page==EDIT||page==HOME||page==MIX||page==SONG_DETAILS||page==SEQ){chooseInstrument(ch,preset[ch]+d);if(page==HOME)page=MIX;if(!playing)sound(ch,ch?0:7);}
  else if(page==SECTION_SOUND){if(ch)tune_part(&sectionMix[editSection][ch],PART_RATIO,d);}
  else if(ch)shape[ch]=(uint8_t)wrap(shape[ch]+d,8);
  else drive[0]=clamp(drive[0]+d,0,40);
 }
 d=encoder(ENC_KNOB1);if(d){
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  if(page==FX)p.sel=(uint8_t)wrap(p.sel+d,NC);
  else
#endif
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  if(native_edit_page())editSection=wrap(editSection+d,NS);
#else
  if(page==SECTION_SOUND)editSection=wrap(editSection+d,NS);
#endif
  else {p.bpm=(uint8_t)clamp(p.bpm+d,60,180);if(page==SONG_DETAILS&&songScope==0)songScope=1;}
 }
 d=encoder(ENC_KNOB2);if(d){
  if(page==SONG_DETAILS){songHarmony.root=(int8_t)(wrap(songHarmony.root+d+6,12)-6);if(songScope==0)songScope=1;}
  else if(page==GLOBAL)masterLevel=clamp(masterLevel+d,0,100);
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  else if(native_edit_page())native_edit_scroll(d);
#else
  else if(page==SECTION_SOUND)editParameter=wrap(editParameter+d,MOT_PARAMS);
#endif
  else if(page==SEQ)cursor=wrap(cursor+d,16);
  else if(page==FX)fxParameter=wrap(fxParameter+d,
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  SF_COUNT
#else
  5
#endif
  );
  else if(page==ENV)attackMs[ch]=clamp(attackMs[ch]+d,0,100);
  else if(page==MOTION)lfoRate[ch]=clamp(lfoRate[ch]+d,0,20);
  else level[ch]=(uint8_t)clamp(level[ch]+d,0,100);
 }
 d=encoder(ENC_KNOB3);if(d){
  if(page==SONG_DETAILS){songDensity=clamp(songDensity+d,0,4);if(songScope==0)songScope=1;}
  else if(page==GLOBAL)transpose=clamp(transpose+d,-12,12);
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  else if(native_edit_page())motion_adjust(ch,editParameter,d);
#else
  else if(page==SECTION_SOUND)motion_adjust(ch,editParameter,d);
#endif
  else if(page==MOTION&&ch)motion[ch]=clamp(motion[ch]+d,0,32);
  else if(page==SEQ){
   uint8_t *note=&p.pat[section][ch][cursor];
   *note=(uint8_t)clamp((*note==EMPTY?0:*note)+d,ch?0:1,ch?12:15);
  }else if(page==FX){
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
   sound_menu_adjust(ch,d);
#else
   switch(fxParameter){case 0:drive[ch]=clamp(drive[ch]+d,0,40);break;case 1:fxDelay=clamp(fxDelay+d,0,50);break;case 2:fxReverb=clamp(fxReverb+d,0,50);break;case 3:fxChorus=clamp(fxChorus+d,0,50);break;default:fxFilter=clamp(fxFilter+d,0,100);break;}
#endif
  }
  else if(!ch)drive[ch]=clamp(drive[ch]+d,0,40);
  else toneDepth[ch]=clamp(toneDepth[ch]+d,0,72);
 }
 d=encoder(ENC_KNOB4);if(d){
  if(page==SONG_DETAILS)songScope=wrap(songScope+d,COUNT(songScopes));
  else if(page==GLOBAL)octave=clamp(octave+d,-2,2);
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
  else if(page==ENV)motion_adjust(ch,PART_DECAY,d);
  else if(page==MOTION)motion_adjust(ch,MOT_DEPTH,d);
#endif
  else if(page==SECTION_SOUND)tune_part(&sectionMix[editSection][ch],PART_PAN,d);
  else if(page==SEQ){loopSection=wrap(loopSection+d,NS);section=loopSection;repeat=1;}
  else tails[ch]=clamp(tails[ch]+d,ch?9:5,ch?17:14);
 }
 for(int k=0;k<KEY_COUNT;k++)if(key_pressed(k)){
  int kd=ch==0?(1<<(k%4)):k%13;
  if(page==SEQ&&!recording){
   cursor=k%16;uint8_t *note=&p.pat[section][ch][cursor];
   *note=ch?(*note==EMPTY?(uint8_t)kd:EMPTY):(*note?0:(uint8_t)kd);
   if(ch?*note!=EMPTY:*note)sound(ch,*note);
  }else{
#if defined(FOMA_NATIVE_WIFI) && FOMA_NATIVE_WIFI
   if(native_edit_page()&&!recording)sound_at_root(ch,kd,songHarmony.root,editSection,p.style,100);
#else
   if(page==SECTION_SOUND&&!recording)sound_at_root(ch,kd,songHarmony.root,editSection,p.style,100);
#endif
   else sound(ch,kd);
   if(recording)p.pat[section][ch][playing?step:k%16]=(uint8_t)kd;
  }
 }
 int catchup=0;while(playing&&(int32_t)(now-due)>=0&&catchup++<4){transport();due+=15000/p.bpm;remMs+=15000%p.bpm;if(remMs>=p.bpm){remMs-=p.bpm;due++;}}
 if(now-lastFace>=85&&page==HOME){lastFace=now;
 /* Clear old silhouettes and wakes before compositing the next ocean frame. */
 for(int j=0;j<NC;j++){
  Face *a=&f[j];int x=clamp(a->ox-11,0,239),w=clamp(46,1,240-x);
  int top=clamp(a->oy-3,91,137);
  if(top<137)rect(x,top,w,137-top,BLACK);
  rect(x,125,w,12,BLACK);
 }
 stage_surface();
 for(int j=0;j<NC;j++)swimmer_step(j,now);
 if(now-lastWave>=85){lastWave=now;stage_plane(now);}
 for(int j=0;j<NC;j++)faceDraw(j);
 for(int j=0;j<NC;j++)stage_splash(j,now);
 }
 if(now-lastHud>=140){lastHud=now;hud();}
 panel_lights(now);
}
