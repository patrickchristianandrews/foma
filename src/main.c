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
#define NC 8
#define NS 5
#define NV 12
#define EMPTY 255

typedef struct { uint32_t magic; uint8_t style,bpm,sel,mute; uint8_t pat[NS][NC][16]; } Project;
static Project p;
enum {HOME, MIX, ENV, MOTION, EDIT, GLOBAL, FX, SEQ};
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

static LiquidHarmony songHarmony;
static const uint32_t SONG_MAGIC = 12346u;
static const uint32_t LEGACY_MAGIC = 12345u;

static const char *pageName[]={"STAGE","MIX","ENVELOPE","MOTION","SOUND EDIT","GLOBAL","FX","STEPS"};
static const char *knobGuide[]={"TEMPO  LEVEL  FM  TAIL","TEMPO  LEVEL  FM  TAIL","TEMPO  LEVEL  FM  TAIL","TEMPO  LEVEL  MOT  TAIL","TEMPO  LEVEL  FM  TAIL","TEMPO  OUT  KEY  OCT","TEMPO  LEVEL  DRIVE  TAIL","TEMPO  STEP  NOTE  LOOP"};
static int clamp(int value,int low,int high){return value<low?low:value>high?high:value;}
static int wrap(int value,int count){return (value%count+count)%count;}

static const char *names[8]={"BEAR","CAT","BUNNY","CHICK","CLOUD","ALIEN","GHOST","TIDBIT"};
static const char *sections[5]={"INTRO","VERSE","CHORUS","BRIDGE","OUTRO"};
static const char *styles[4]={"SYNTH","FUNK","BALLAD","HOUSE"};
static const uint8_t scale[2][7]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10}};
static const uint16_t colors[8]={RGB(255,100,40),RGB(190,90,255),RGB(255,210,60),RGB(70,220,250),RGB(255,120,210),RGB(90,255,170),RGB(250,150,80),RGB(160,180,255)};
static const uint8_t vb[8]={0,0,1,4,5,8,9,10};
static const uint8_t vn[8]={0,1,3,1,3,1,1,1};
static volatile uint8_t head,tail;
typedef struct {uint8_t voice,sh,drum,ratio;uint32_t inc;int32_t amp,idx;} Event;
static Event queue[64];
typedef struct {uint32_t ph,mp,inc;int32_t env,idx;uint8_t sh,ratio;} Voice;
static Voice v[NV];
static int32_t ke,se,he,prevNoise;static uint8_t drumTail=11; static uint32_t kp,ki,kbase,noise=0xA5A5A5A5u;
static int playing,recording,pos,section,step,bar;
static uint32_t due,remMs,active[8],lastFace,lastHud;
static uint32_t bootStarted, lastWave;
static int booting = 1;
typedef struct {int x,y,vx,vy,ox,oy;uint8_t expr; } Face;
static Face f[8]; static uint8_t faceIndex;
static uint32_t rnd(uint32_t n){return n?random32()%n:0;}
static void enqueue(Event e){uint8_t h=(head+1)&63;if(h==tail)return;queue[head]=e;head=h;}
static int degree(int d){return scale[p.style==2?0:1][d%7]+12*(d/7);}
static void sound_at_root(int ch,int d,int root){
 active[ch]=millis()+180;
 if(ch==0){Event e={0,(uint8_t)tails[0],(uint8_t)d,0,note_inc(68+instrument(0)->shift+transpose+octave*12),level[0]*240,drive[0]};enqueue(e);return;}
 for(int j=0;j<vn[ch];j++){
 int note=60+root+12*octave+instrument(ch)->shift+transpose+degree(d+j*2);if(note>127)note=127;if(note<0)note=0;
 Event e={(uint8_t)(vb[ch]+j),(uint8_t)tails[ch],0,(uint8_t)(shape[ch]+1),note_inc(note),((ch==2||ch==4)?70:160)*level[ch],toneDepth[ch]+drive[ch]+(motion[ch]?(int)((millis()/40)%12)*motion[ch]/8:0)};enqueue(e);
 }
}
static void sound(int ch, int d) { sound_at_root(ch, d, songHarmony.root); }
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
      160*level[ch]*velocity/127,toneDepth[ch]+drive[ch]};
    enqueue(e);
   }
  }
 }
}
#endif
void audio(int32_t *out,uint32_t frames){
 while(tail!=head){Event e=queue[tail];tail=(tail+1)&63;
 if(e.drum){drumTail=e.sh;if(e.drum&1){ke=e.amp;ki=e.inc;kbase=e.inc/7;}if(e.drum&2)se=e.amp*3/4+e.idx*30;if(e.drum&12)he=e.amp/2+e.idx*20;}
 else {Voice *a=&v[e.voice];a->inc=e.inc;a->env=e.amp<<14;a->idx=e.idx;a->sh=e.sh;a->ratio=e.ratio;}}
 for(uint32_t n=0;n<frames;n++){
 int32_t sum=0;
 for(int j=0;j<NV;j++){Voice *a=&v[j];int32_t env=a->env>>14;if(env<8)continue;
 a->ph+=a->inc;a->mp+=a->inc*a->ratio;
 int32_t dep=(a->idx*env)>>15;
 uint32_t offset=(uint32_t)((int64_t)sine(a->mp)*dep*1304);
 sum+=(sine(a->ph+offset)*env)>>17;a->env-=a->env>>a->sh;}
 noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;int32_t nz=(int32_t)(noise>>17)-16384;
 if(ke>32){kp+=ki;ki-=(ki-kbase)>>9;sum+=(sine(kp)*ke)>>17;ke-=ke>>drumTail;}
 if(se>32){sum+=(nz*se)>>17;se-=se>>(drumTail-1);}
 if(he>32){sum+=((nz-prevNoise)*he)>>18;he-=he>>(drumTail-1);}prevNoise=nz;
 sum=sum*masterLevel/100;
 if(sum>24000){sum=24000;}if(sum< -24000){sum=-24000;}
 out[n*2]+=sum;out[n*2+1]+=sum;
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
static void hud(void){
 if(page!=HOME){
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
 text(0,16,styles[p.style],WHITE,FONT_SMALL);char b[12];format_int(b,p.bpm);text(96,16,b,WHITE,FONT_SMALL);text(0,32,page==EDIT?"ALGORITHM: INSTRUMENT":!p.sel&&page!=GLOBAL&&page!=SEQ?"TEMPO  LEVEL DRIVE TAIL":knobGuide[page],WHITE,FONT_SMALL);
 rect(0,194,240,46,BLACK);text(0,194,sections[section],WHITE,FONT_SMALL);
 text(96,194,names[p.sel],colors[p.sel],FONT_SMALL);text(192,194,recording?"REC":"",WHITE,FONT_SMALL);
 for(int i=0;i<16;i++){int d=p.pat[section][p.sel][i];uint16_t col=(p.sel?d!=EMPTY:d!=0)?colors[p.sel]:RGB(35,35,35);rect(i*15,218,12,12,col);if((playing&&i==step)||(page==SEQ&&i==cursor))frame(i*15,216,14,16,WHITE);}
}
void setup(void){
 clear(BLACK);p.magic=LEGACY_MAGIC;p.style=0;p.sel=0;p.mute=0;generate();
 SavedSong full;
 SavedProject saved;
 if(settings_load(&full,sizeof(full))==0&&full.session.project.magic==SONG_MAGIC&&
    full.session.project.style<4&&full.session.project.sel<8&&
    full.session.project.bpm>=60&&full.session.project.bpm<=180){
  int valid=full.harmony.root>=-4&&full.harmony.root<=5;
  for(int s=0;s<NS;s++)for(int i=0;i<4;i++)valid &= full.harmony.progression[s][i]<7;
  for(int i=0;i<LW_ARRANGEMENT;i++)valid &= full.harmony.arrangement[i]<NS;
  if(valid){
   p=full.session.project;songHarmony=full.harmony;
   for(int ch=0;ch<NC;ch++)chooseInstrument(ch,full.session.presets[ch]);
  }
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
 if(booting){
  if(now-bootStarted<780 && !button_pressed(BTN_HOME) && !button_pressed(BTN_PLAY)){
   boot_wave(now-bootStarted);
   return;
  }
  booting=0;
  clear(BLACK);
  stage_waves(now);
  for(int i=0;i<NC;i++)faceDraw(i);
  lastFace=lastWave=lastHud=now;
  hud();
 }
#if FOMA_BLUETOOTH
 bluetooth_input();
#endif
 if(button_pressed(BTN_HOME)){
  page=HOME;
  clear(BLACK);
  stage_waves(now);
  for(int i=0;i<NC;i++)faceDraw(i);
 }
 if(button_pressed(BTN_SEL))p.mute^=1<<p.sel;
 if(button_pressed(BTN_ENV))page=ENV;
 if(button_pressed(BTN_LFO))page=MOTION;
 if(button_pressed(BTN_EDIT))page=EDIT;
 if(button_pressed(BTN_GLO))page=GLOBAL;
 if(button_pressed(BTN_FX))page=FX;
 if(button_pressed(BTN_SEQ)){if(page==SEQ)repeat=!repeat;page=SEQ;loopSection=section;}
 if(button_pressed(BTN_PLAY)){playing=!playing;if(playing){pos=0;due=now;remMs=0;}}
 if(button_pressed(BTN_REC))recording=!recording;
 if(button_pressed(BTN_OCT_DOWN))octave=clamp(octave-1,-2,2);
 if(button_pressed(BTN_OCT_UP))octave=clamp(octave+1,-2,2);
 if(button_pressed(BTN_ARP)){generate();pos=0;due=now;}
 if(button_pressed(BTN_SAVE)&&!playing){
  SavedSong saved={0};
  saved.session.project=p;
  saved.session.project.magic=SONG_MAGIC;
  for(int ch=0;ch<NC;ch++)saved.session.presets[ch]=preset[ch];
  saved.harmony=songHarmony;
  settings_save(&saved,sizeof(saved));
 }
 int d=encoder(ENC_SELECT);if(d){p.sel=(uint8_t)wrap(p.sel+d,8);if(page==HOME)page=MIX;}
 d=encoder(ENC_PRESETS);if(d){p.style=(uint8_t)wrap(p.style+d,4);generate();pos=0;due=now;}
 int ch=p.sel;
 d=encoder(ENC_ALGORITHM);if(d){
  if(page==EDIT){chooseInstrument(ch,preset[ch]+d);if(!playing)sound(ch,ch?0:7);}
  else if(ch)shape[ch]=(uint8_t)wrap(shape[ch]+d,8);
  else drive[0]=clamp(drive[0]+d,0,40);
 }
 d=encoder(ENC_KNOB1);if(d)p.bpm=(uint8_t)clamp(p.bpm+d,60,180);
 d=encoder(ENC_KNOB2);if(d){
  if(page==GLOBAL)masterLevel=clamp(masterLevel+d,0,100);
  else if(page==SEQ)cursor=wrap(cursor+d,16);
  else level[ch]=(uint8_t)clamp(level[ch]+d,0,100);
 }
 d=encoder(ENC_KNOB3);if(d){
  if(page==GLOBAL)transpose=clamp(transpose+d,-12,12);
  else if(page==MOTION&&ch)motion[ch]=clamp(motion[ch]+d,0,32);
  else if(page==SEQ){
   uint8_t *note=&p.pat[section][ch][cursor];
   *note=(uint8_t)clamp((*note==EMPTY?0:*note)+d,ch?0:1,ch?12:15);
  }else if(page==FX||!ch)drive[ch]=clamp(drive[ch]+d,0,40);
  else toneDepth[ch]=clamp(toneDepth[ch]+d,0,72);
 }
 d=encoder(ENC_KNOB4);if(d){
  if(page==GLOBAL)octave=clamp(octave+d,-2,2);
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
   sound(ch,kd);
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
 led_button(BTN_ENV,page==ENV);led_button(BTN_LFO,page==MOTION);led_button(BTN_EDIT,page==EDIT);
 led_button(BTN_GLO,page==GLOBAL);led_button(BTN_SEL,(p.mute&(1<<p.sel))!=0);
}
