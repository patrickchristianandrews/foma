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
#include "tidbit_portrait.h"
#include "weather.h"
#define NC 8
#define NS 5
#define NV 12
#define EMPTY 255

typedef struct { uint32_t magic; uint8_t style,bpm,sel,mute; uint8_t pat[NS][NC][16]; } Project;
static Project p;
enum {HOME, MIX, ENV, MOTION, EDIT, GLOBAL, FX, SEQ, SECTION_SOUND, WEATHER};
static int page, cursor, octave, loopSection, repeat, transpose;
static volatile int masterLevel=85;
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

static const char *names[8]={"BEAR","DOG","BUNNY","CHICK","CLOUD","ALIEN","GHOST","TIDBIT"};
static const char *sections[5]={"INTRO","VERSE","CHORUS","BRIDGE","OUTRO"};
static const char *styles[4]={"SYNTH","FUNK","BALLAD","HOUSE"};
static const uint8_t scale[2][7]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10}};
static const uint16_t colors[8]={RGB(255,100,40),RGB(190,90,255),RGB(255,210,60),RGB(70,220,250),RGB(255,120,210),RGB(90,255,170),RGB(250,150,80),RGB(160,180,255)};
static const uint8_t vb[8]={0,0,1,4,5,8,9,10};
static const uint8_t vn[8]={0,1,3,1,3,1,1,1};
static volatile uint8_t head,tail;
static volatile uint8_t releaseRequested;
typedef struct {uint8_t voice,sh,drum,ratio;uint32_t inc;int32_t amp,idx;int8_t pan;} Event;
static Event queue[64];
typedef struct {uint32_t ph,mp,inc;int32_t env,idx;uint8_t sh,ratio;int8_t pan;} Voice;
static Voice v[NV];
static int32_t ke,se,he,prevNoise;static uint8_t drumTail=11; static uint32_t kp,ki,kbase,noise=0xA5A5A5A5u;
static int8_t drumPan;
static int playing,recording,pos,section,step,bar;
static uint32_t due,remMs,active[8],lastFace,lastHud;
static uint32_t bootStarted, lastWave;
static int booting = 1;
static struct {
 LiquidWeather reading;
 LiquidWeatherMusic music;
 LiquidHarmony harmony;
 uint8_t pattern[NS][NC][16];
 uint32_t synced, nextStep, octSince, seed;
 int place, back, available, playing, step, octHeld, octUsed;
} weather;
typedef struct {int x,y,vx,vy,ox,oy;uint8_t expr; } Face;
static Face f[8]; static uint8_t faceIndex;
static uint32_t rnd(uint32_t n){return n?random32()%n:0;}
static void enqueue(Event e){uint8_t h=(head+1)&63;if(h==tail)return;queue[head]=e;head=h;}
static int degree(int d,int style){return scale[style==2?0:1][d%7]+12*(d/7);}
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
  int animate=motion[ch]?(int)((millis()/40)%12)*motion[ch]/8:0;
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
static void mix_sample(int32_t sample,int pan,int32_t *mono,int32_t *left,int32_t *right){
 if(!pan){*mono+=sample;return;}
 /* Constant gain on the near speaker; attenuate the opposite speaker. */
 *left+=pan>0?(sample*(127-pan))/128:sample;
 *right+=pan<0?(sample*(127+pan))/128:sample;
}
void audio(int32_t *out,uint32_t frames){
 while(tail!=head){Event e=queue[tail];tail=(tail+1)&63;
 if(e.drum){drumTail=e.sh;drumPan=e.pan;if(e.drum&1){ke=e.amp;ki=e.inc;kbase=e.inc/7;}if(e.drum&2)se=e.amp*3/4+e.idx*30;if(e.drum&12)he=e.amp/2+e.idx*20;}
 else {Voice *a=&v[e.voice];a->inc=e.inc;a->env=e.amp<<14;a->idx=e.idx;a->sh=e.sh;a->ratio=e.ratio;a->pan=e.pan;}}
 /* Stop releases active voices smoothly on the audio thread. */
 if(releaseRequested){releaseRequested=0;for(int j=0;j<NV;j++)if(v[j].sh>10)v[j].sh=10;if(drumTail>10)drumTail=10;}
 for(uint32_t n=0;n<frames;n++){
 int32_t sum=0,left=0,right=0,drums=0;
 for(int j=0;j<NV;j++){Voice *a=&v[j];int32_t env=a->env>>14;if(env<8)continue;
 a->ph+=a->inc;a->mp+=a->inc*a->ratio;
 int32_t dep=(a->idx*env)>>15;
 uint32_t offset=(uint32_t)((int64_t)sine(a->mp)*dep*1304);
 mix_sample((sine(a->ph+offset)*env)>>17,a->pan,&sum,&left,&right);
 a->env-=a->env>>a->sh;}
 noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;int32_t nz=(int32_t)(noise>>17)-16384;
 if(ke>32){kp+=ki;ki-=(ki-kbase)>>9;drums+=(sine(kp)*ke)>>17;ke-= (ke>>drumTail) ? (ke>>drumTail) : 1;}
 if(se>32){drums+=(nz*se)>>17;se-= (se>>(drumTail-1)) ? (se>>(drumTail-1)) : 1;}
 if(he>32){drums+=((nz-prevNoise)*he)>>18;he-= (he>>(drumTail-1)) ? (he>>(drumTail-1)) : 1;}prevNoise=nz;
 mix_sample(drums,drumPan,&sum,&left,&right);
 int volume=masterLevel;
 out[n*2]+=clamp((sum+left)*volume/100,-24000,24000);
 out[n*2+1]+=clamp((sum+right)*volume/100,-24000,24000);
 }
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
static void transport(void){
 section=repeat?loopSection:songHarmony.arrangement[pos/64];bar=(pos/16)%4;step=pos%16;
 int cd=songHarmony.progression[section][bar];
 for(int c=0;c<8;c++){if(p.mute&(1<<c))continue;int d=p.pat[section][c][step];if(c==0){if(d)sound(c,d);}else if(d!=EMPTY)sound(c,d+cd);}
 pos=(pos+1)%512;
}

int liquid_weather_destination(void){return weather.place;}

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
 weather.playing=0;
}

static int weather_fresh(uint32_t now){
 return weather.available && now-weather.synced<1800000u;
}

static void weather_generate(uint32_t now){
 int moon=weather.place==LIQUID_MOON;
 LiquidWeather current=weather.reading;
 if(!moon)current.local_minute=(uint16_t)((current.local_minute+(now-weather.synced)/60000u)%1440);
 weather.music=liquid_weather_music(&current,moon?0:liquid_places[weather.place].rhythm,moon);
 if(!weather.seed)weather.seed=random32();
 liquid_song_generate(weather.music.style,++weather.seed,weather.pattern,&weather.harmony);
 weather.step=0;
}

static void weather_transport(uint32_t now){
 if(!weather.playing||(int32_t)(now-weather.nextStep)<0)return;
 if(!weather.step){
  if(weather.place!=LIQUID_MOON&&!weather_fresh(now)){weather.playing=0;return;}
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

static void weather_hud(uint32_t now){
 const uint16_t peach=RGB(255,174,117), purple=RGB(183,137,235), dim=RGB(103,66,76);
 const uint16_t sky=RGB(18,17,37);
 char value[12];
 int moon=weather.place==LIQUID_MOON;
 int live=!moon&&weather_fresh(now);
 unsigned code=live?weather.reading.code:61;
 unsigned minute=live?(weather.reading.local_minute+(now-weather.synced)/60000u)%1440:1260;
 int night=minute<360||minute>=1080;
 int snow=code>=71&&code<=77;
 int rain=(code>=51&&code<=67)||(code>=80&&code<=82)||code>=95;
 rect(0,0,240,240,BLACK);
 text(8,5,"TIDBIT WEATHER ROOM",peach,FONT_SMALL);
 text(8,22,moon?"THE MOON":liquid_places[weather.place].name,WHITE,FONT_SMALL);
 rect(61,43,166,105,night?sky:RGB(64,104,132));
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
 /* Tidbit: reuse the existing mascot sprite, enlarged for the room. */
 for(int y=0;y<18;y++)for(int x=0;x<24;x++){
  uint16_t c=tidbitPortrait[y*24+x];
  if(c)rect(98+x*3,96+y*3,3,3,c);
 }
 /* Plant, record player, mug and books. */
 rect(199,136,17,14,purple);
 line(207,136,207,116,purple);line(207,128,198,119,purple);
 line(207,124,216,113,purple);
 frame(12,168,68,31,peach);
 frame(19,173,35,20,dim);frame(27,177,19,12,peach);
 line(70,174,59,189,peach);
 frame(104,177,23,21,peach);frame(127,180,9,12,peach);
 for(int i=0;i<3;i++){int x=108+i*6;line(x,173,x+2,166-(int)((now/250+i)%3),dim);}
 for(int i=0;i<5;i++)frame(166+i*10,172-i%3*5,8,26+i%3*5,i%2?purple:peach);
 line(8,201,232,201,peach);
 if(live){
  text(8,205,liquid_weather_condition(code),WHITE,FONT_SMALL);
  format_int(value,weather.reading.temperature10/10);text(185,205,value,WHITE,FONT_SMALL);text(210,205,"C",peach,FONT_SMALL);
 }else text(8,205,moon?"LUNAR MODE":weather.available?"STALE / DEMO":"OFFLINE / DEMO",WHITE,FONT_SMALL);
 text(8,222,"HOME STAGE / PLAY JAM",peach,FONT_SMALL);
 if(weather.playing)rect(224,8,7,7,purple);
}

static const uint16_t waveColors[] = {RGB(53,44,100),RGB(28,72,105),RGB(85,39,92)};
static const int waveRows[] = {76,117,158};

static int wave_height(int x, int layer, uint32_t phase) {
 uint32_t angle = (uint32_t)x * (0x01900000u + (uint32_t)layer * 0x00800000u);
 angle -= phase * (2u + (uint32_t)layer);
 return (sine(angle) * (5 + layer)) >> 15;
}

static void stage_waves(uint32_t now) {
 uint32_t phase = now * 1800000u;
 static uint32_t previousPhase;
 static int painted;
 if (painted) {
  for (int layer=0;layer<3;layer++)
   for (int x=0;x<SCREEN_W-1;x+=4) {
    int end=x+4<SCREEN_W?x+4:SCREEN_W-1, row=waveRows[layer];
    line(x,row+wave_height(x,layer,previousPhase),end,row+wave_height(end,layer,previousPhase),BLACK);
   }
 }
 for (int layer=0;layer<3;layer++)
  for (int x=0;x<SCREEN_W-1;x+=4) {
   int end=x+4<SCREEN_W?x+4:SCREEN_W-1, row=waveRows[layer];
   line(x,row+wave_height(x,layer,phase),end,row+wave_height(end,layer,phase),waveColors[layer]);
  }
 painted=1;
 previousPhase=phase;
}

static void boot_wave(uint32_t elapsed) {
 static int previous=-1;
 if (previous<0) text(32,72,"LIQUID WAVE",WHITE,FONT_BIG);
 int front=(int)(elapsed*300u/780u);
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
static void portrait_at(int i,int x,int y){
 uint16_t col=p.mute&(1<<i)?RGB(90,90,90):colors[i];
 int hit=millis()<active[i];
 if(i==7){
  int muted=(p.mute&(1<<i))!=0;
  for(int row=0;row<18;row++)for(int tx=0;tx<24;tx++){
   uint16_t color=tidbitPortrait[row*24+tx];
   if(color)pixel(x+tx,y+row,muted?(uint16_t)((color&0xf7deu)>>1):color);
  }
  if(hit&&!muted)line(x+4,y+18,x+19,y+18,RGB(255,170,225));
  if(i==p.sel)frame(x-1,y-1,26,20,WHITE);
  return;
 }
 rect(x,y,24,18,col);
 if(p.mute&(1<<i)){line(x+4,y+6,x+8,y+6,BLACK);line(x+15,y+6,x+19,y+6,BLACK);}
 else {rect(x+5,y+4,3,4,BLACK);rect(x+16,y+4,3,4,BLACK);}
 if(hit&&!(p.mute&(1<<i)))rect(x+8,y+10,8,5,BLACK);else line(x+8,y+12,x+15,y+12,BLACK);
 if(i==p.sel)frame(x-1,y-1,26,20,WHITE);
}
static void faceDraw(int i){
 Face *a=&f[i];
 portrait_at(i,a->x,a->y);
 a->ox=a->x;a->oy=a->y;
}
enum { PART_LEVEL, PART_PITCH, PART_DEPTH, PART_DECAY, PART_RATIO, PART_PAN, PART_PARAMS };
static const char *partParams[]={"LEVEL","PITCH","FM / DRIVE","DECAY","FM RATIO","PAN"};
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
static void hud(void){
 if(page==WEATHER){weather_hud(millis());return;}
 if(page==SECTION_SOUND){
  SectionSound *part=&sectionMix[editSection][p.sel];
  rect(0,48,240,145,BLACK);
  text(8,49,sections[editSection],WHITE,FONT_SMALL);
  text(117,49,names[p.sel],colors[p.sel],FONT_SMALL);
  for(int i=0;i<PART_PARAMS;i++){
   char value[12];
   int y=68+i*16;
   uint16_t ink=i==editParameter?RGB(255,190,237):WHITE;
   text(8,y,i==editParameter?">":" ",ink,FONT_SMALL);
   text(24,y,partParams[i],ink,FONT_SMALL);
   if(i==PART_RATIO&&!p.sel)text(160,y,"-",ink,FONT_SMALL);
   else {format_int(value,*part_value(part,i));text(160,y,value,ink,FONT_SMALL);}
  }
  text(8,170,"K3 ADJUST / K4 PAN",WHITE,FONT_SMALL);
 }else if(page!=HOME){
  rect(0,48,240,145,BLACK);
  text(8,60,names[p.sel],colors[p.sel],FONT_SMALL);
  text(72,60,instrument(p.sel)->name,WHITE,FONT_SMALL);
  const char *labels[]={"LEVEL","FM DEPTH","DECAY","RATIO","MOTION","DRIVE"};
  int values[]={level[p.sel],toneDepth[p.sel],tails[p.sel],shape[p.sel]+1,motion[p.sel],drive[p.sel]};
  if(page==GLOBAL){labels[0]="MASTER";values[0]=masterLevel;labels[1]="KEY";values[1]=transpose;labels[2]="OCTAVE";values[2]=octave;}
  if(page==SEQ){labels[0]="STEP";values[0]=cursor+1;labels[1]="LOOP";values[1]=repeat;labels[2]="SECTION";values[2]=section+1;}
  for(int i=0;i<(page==GLOBAL||page==SEQ?3:6);i++){char value[12];format_int(value,values[i]);text(8,80+i*17,labels[i],WHITE,FONT_SMALL);text(135,80+i*17,value,WHITE,FONT_SMALL);}
  if(page==SEQ)for(int i=0;i<NC;i++)portrait_at(i,3+30*i,151);
 }

 rect(0,0,240,48,BLACK);text(0,0,"LIQUID WAVE",WHITE,FONT_SMALL);text(125,0,pageName[page],WHITE,FONT_SMALL);
 text(0,16,styles[p.style],WHITE,FONT_SMALL);char b[12];format_int(b,p.bpm);text(96,16,b,WHITE,FONT_SMALL);
 const char *guide=page==SECTION_SOUND?"K1 SECTION / K2 PARAM":
  page==EDIT?"ALGORITHM: INSTRUMENT":!p.sel&&page!=GLOBAL&&page!=SEQ?"TEMPO  LEVEL DRIVE TAIL":knobGuide[page];
 text(0,32,guide,WHITE,FONT_SMALL);
 rect(0,194,240,46,BLACK);text(0,194,sections[section],WHITE,FONT_SMALL);
 text(96,194,names[p.sel],colors[p.sel],FONT_SMALL);text(192,194,recording?"REC":"",WHITE,FONT_SMALL);
 for(int i=0;i<16;i++){int d=p.pat[section][p.sel][i];uint16_t col=(p.sel?d!=EMPTY:d!=0)?colors[p.sel]:RGB(35,35,35);rect(i*15,218,12,12,col);if((playing&&i==step)||(page==SEQ&&i==cursor))frame(i*15,216,14,16,WHITE);}
}
static int load_song_settings(const SavedSong *saved,uint32_t magic){
 const Project *project=&saved->session.project;
 if(project->magic!=magic||project->style>=4||project->sel>=NC||project->bpm<60||project->bpm>180)return 0;
 if(saved->harmony.root< -4||saved->harmony.root>5)return 0;
 for(int s=0;s<NS;s++)for(int i=0;i<4;i++)if(saved->harmony.progression[s][i]>=7)return 0;
 for(int i=0;i<LW_ARRANGEMENT;i++)if(saved->harmony.arrangement[i]>=NS)return 0;
 p=*project;
 songHarmony=saved->harmony;
 for(int ch=0;ch<NC;ch++)chooseInstrument(ch,saved->session.presets[ch]);
 return 1;
}
void setup(void){
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
 }else if(settings_load(&saved,sizeof(saved))==0&&saved.project.magic==LEGACY_MAGIC&&saved.project.style<4&&saved.project.sel<8&&saved.project.bpm>=60&&saved.project.bpm<=180){
  p=saved.project;for(int ch=0;ch<NC;ch++)chooseInstrument(ch,saved.presets[ch]);
  liquid_song_legacy_harmony(p.style,&songHarmony);
 }else{
  Project legacy;if(settings_load(&legacy,sizeof(legacy))==0&&legacy.magic==LEGACY_MAGIC&&legacy.style<4&&legacy.sel<8&&legacy.bpm>=60&&legacy.bpm<=180){
   p=legacy;
   liquid_song_legacy_harmony(p.style,&songHarmony);
  }
  for(int ch=0;ch<NC;ch++)chooseInstrument(ch,preset[ch]);
 }
 for(int i=0;i<8;i++){f[i].x=8+(i%4)*55;f[i].y=49+(i/4)*65;f[i].vx=rnd(2)?1:-1;f[i].vy=rnd(2)?1:-1;f[i].ox=f[i].x;f[i].oy=f[i].y;}
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
  if(now-bootStarted<780 && !button_pressed(BTN_HOME) && !button_pressed(BTN_PLAY)){
   boot_wave(now-bootStarted);
   return;
  }
  booting=0;
  skippedBoot=1;
  clear(BLACK);
  stage_waves(now);
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
    else if(weather.place==LIQUID_MOON||weather_fresh(now)){
     weather.playing=1;
     weather.nextStep=now;
     weather.step=0;
    }
    lastHud=now-1000u;
   }
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
   if(now-lastHud>=1000){lastHud=now;weather_hud(now);}
   led_button(BTN_HOME,1);
   led_button(BTN_PLAY,weather.playing||playing);
   return;
  }
  if(weather.playing)releaseRequested=1;
  weather.playing=0;
  weather.octHeld=0;
  page=MIX; /* The existing HOME handler below returns to Stage. */
 }
 if(button_pressed(BTN_HOME)){
  if(page==HOME&&!skippedBoot){
   page=WEATHER;
   weather_hud(now);
   lastHud=now;
   return;
  }
  page=HOME;
  clear(BLACK);
  stage_waves(now);
  for(int i=0;i<NC;i++)faceDraw(i);
 }
 if(button_pressed(BTN_SEL))p.mute^=1<<p.sel;
 if(button_pressed(BTN_ENV))page=ENV;
 if(button_pressed(BTN_LFO))page=MOTION;
 if(button_pressed(BTN_EDIT)){
  if(page==EDIT){page=SECTION_SOUND;editSection=section;editParameter=PART_LEVEL;}
  else page=EDIT;
 }
 if(button_pressed(BTN_GLO))page=GLOBAL;
 if(button_pressed(BTN_FX))page=FX;
 if(button_pressed(BTN_SEQ)){if(page==SEQ)repeat=!repeat;page=SEQ;loopSection=section;}
 if(button_pressed(BTN_PLAY)){playing=!playing;if(playing){pos=0;due=now;remMs=0;}else releaseRequested=1;}
 if(button_pressed(BTN_REC))recording=!recording;
 if(button_pressed(BTN_OCT_DOWN))octave=clamp(octave-1,-2,2);
 if(button_pressed(BTN_OCT_UP))octave=clamp(octave+1,-2,2);
 if(button_pressed(BTN_ARP)){generate();pos=0;due=now;}
 if(button_pressed(BTN_SAVE)&&!playing){
  SavedMix saved={0};
  saved.song.session.project=p;
  saved.song.session.project.magic=MIX_MAGIC;
  for(int ch=0;ch<NC;ch++)saved.song.session.presets[ch]=preset[ch];
  saved.song.harmony=songHarmony;
  for(int s=0;s<NS;s++)for(int ch=0;ch<NC;ch++)saved.sound[s][ch]=sectionMix[s][ch];
  settings_save(&saved,sizeof(saved));
 }
 int d=encoder(ENC_SELECT);if(d){p.sel=(uint8_t)wrap(p.sel+d,8);if(page==HOME)page=MIX;}
 d=encoder(ENC_PRESETS);if(d){p.style=(uint8_t)wrap(p.style+d,4);generate();pos=0;due=now;}
 int ch=p.sel;
 d=encoder(ENC_ALGORITHM);if(d){
  if(page==EDIT){chooseInstrument(ch,preset[ch]+d);if(!playing)sound(ch,ch?0:7);}
  else if(page==SECTION_SOUND){if(ch)tune_part(&sectionMix[editSection][ch],PART_RATIO,d);}
  else if(ch)shape[ch]=(uint8_t)wrap(shape[ch]+d,8);
  else drive[0]=clamp(drive[0]+d,0,40);
 }
 d=encoder(ENC_KNOB1);if(d){
  if(page==SECTION_SOUND)editSection=wrap(editSection+d,NS);
  else p.bpm=(uint8_t)clamp(p.bpm+d,60,180);
 }
 d=encoder(ENC_KNOB2);if(d){
  if(page==GLOBAL)masterLevel=clamp(masterLevel+d,0,100);
  else if(page==SECTION_SOUND)editParameter=wrap(editParameter+d,PART_PARAMS);
  else if(page==SEQ)cursor=wrap(cursor+d,16);
  else level[ch]=(uint8_t)clamp(level[ch]+d,0,100);
 }
 d=encoder(ENC_KNOB3);if(d){
  if(page==GLOBAL)transpose=clamp(transpose+d,-12,12);
  else if(page==SECTION_SOUND){
   if(editParameter!=PART_RATIO || ch)tune_part(&sectionMix[editSection][ch],editParameter,d);
  }
  else if(page==MOTION&&ch)motion[ch]=clamp(motion[ch]+d,0,32);
  else if(page==SEQ){
   uint8_t *note=&p.pat[section][ch][cursor];
   *note=(uint8_t)clamp((*note==EMPTY?0:*note)+d,ch?0:1,ch?12:15);
  }else if(page==FX||!ch)drive[ch]=clamp(drive[ch]+d,0,40);
  else toneDepth[ch]=clamp(toneDepth[ch]+d,0,72);
 }
 d=encoder(ENC_KNOB4);if(d){
  if(page==GLOBAL)octave=clamp(octave+d,-2,2);
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
   if(page==SECTION_SOUND&&!recording)sound_at_root(ch,kd,songHarmony.root,editSection,p.style,100);
   else sound(ch,kd);
   if(recording)p.pat[section][ch][playing?step:k%16]=(uint8_t)kd;
  }
 }
 int catchup=0;while(playing&&(int32_t)(now-due)>=0&&catchup++<4){transport();due+=15000/p.bpm;remMs+=15000%p.bpm;if(remMs>=p.bpm){remMs-=p.bpm;due++;}}
 if(now-lastFace>=35&&page==HOME){lastFace=now;int i=faceIndex;Face *a=&f[i];
 rect(a->ox-1,a->oy-1,26,20,BLACK);
 a->x+=a->vx*3;a->y+=a->vy*3;if(a->x<2){a->x=2;a->vx=1;}if(a->x>214){a->x=214;a->vx=-1;}if(a->y<49){a->y=49;a->vy=1;}if(a->y>170){a->y=170;a->vy=-1;}
 if(now-lastWave>=90){lastWave=now;stage_waves(now);}
 for(int j=0;j<NC;j++)faceDraw(j);
 faceIndex=(faceIndex+1)%NC;
 }
 if(now-lastHud>=140){lastHud=now;hud();}
 led_button(BTN_PLAY,playing);led_button(BTN_REC,recording);
 led_button(BTN_HOME,page==HOME);led_button(BTN_SEQ,page==SEQ);led_button(BTN_FX,page==FX);
 led_button(BTN_ENV,page==ENV);led_button(BTN_LFO,page==MOTION);led_button(BTN_EDIT,page==EDIT||page==SECTION_SOUND);
 led_button(BTN_GLO,page==GLOBAL);led_button(BTN_SEL,(p.mute&(1<<p.sel))!=0);
}
