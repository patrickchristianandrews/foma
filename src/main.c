/* SPDX-License-Identifier: GPL-3.0-only */
/* Liquid Wave experimental native firmware. Not hardware tested.
 * PLAY transport; REC overdub; SELECT encoder channel; PRESETS style;
 * knob1 tempo; SEL mute; ARP generate; SEQ next section; SAVE while stopped.
 * Keys play/record selected channel. Drum keys 0..3: kick/snare/hat/open hat.
 * 8 independent 16-step patterns per section, four-bar harmonic sections.
 * Audio: simple two-operator FM, not a DX7 engine. No external sound banks.
 */
#include "fm1.h"
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
static int motion[8], drive[8];
static const char *pageName[]={"STAGE","MIX","ENVELOPE","MOTION","SOUND EDIT","GLOBAL","FX","STEPS"};
static const char *knobGuide[]={"TEMPO  LEVEL  FM  TAIL","TEMPO  LEVEL  FM  TAIL","TEMPO  LEVEL  FM  TAIL","TEMPO  LEVEL  MOT  TAIL","TEMPO  LEVEL  FM  TAIL","TEMPO  OUT  KEY  OCT","TEMPO  LEVEL  DRIVE  TAIL","TEMPO  STEP  NOTE  LOOP"};
static int clamp(int value,int low,int high){return value<low?low:value>high?high:value;}
static int wrap(int value,int count){return (value%count+count)%count;}

static const uint8_t order[8]={0,1,2,1,2,3,2,4};
static const char *names[8]={"BEAR","CAT","BUNNY","CHICK","CLOUD","ALIEN","GHOST","TIDBIT"};
static const char *sections[5]={"INTRO","VERSE","CHORUS","BRIDGE","OUTRO"};
static const char *styles[4]={"SYNTH","FUNK","BALLAD","HOUSE"};
static const uint8_t scale[2][7]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10}};
static const uint8_t progress[4][4]={{0,5,2,6},{0,3,0,4},{0,4,5,3},{0,0,5,6}};
static const uint16_t colors[8]={RGB(255,100,40),RGB(190,90,255),RGB(255,210,60),RGB(70,220,250),RGB(255,120,210),RGB(90,255,170),RGB(250,150,80),RGB(160,180,255)};
static const uint8_t vb[8]={0,0,1,4,5,8,9,10};
static const uint8_t vn[8]={0,1,3,1,3,1,1,1};
static const int8_t oct[8]={0,-2,-1,0,-1,1,0,0};
static volatile uint8_t head,tail;
typedef struct {uint8_t voice,sh,drum,ratio;uint32_t inc;int32_t amp,idx;} Event;
static Event queue[64];
typedef struct {uint32_t ph,mp,inc;int32_t env,idx;uint8_t sh,ratio;} Voice;
static Voice v[NV];
static int32_t ke,se,he,prevNoise;static uint8_t drumTail=11; static uint32_t kp,ki,kbase,noise=0xA5A5A5A5u;
static int playing,recording,pos,section,step,bar;
static uint32_t due,remMs,active[8],lastFace,lastHud;
typedef struct {int x,y,vx,vy,ox,oy;uint8_t expr; } Face;
static Face f[8]; static uint8_t faceIndex;
static uint32_t rnd(uint32_t n){return n?random32()%n:0;}
static void enqueue(Event e){uint8_t h=(head+1)&63;if(h==tail)return;queue[head]=e;head=h;}
static int degree(int d){return scale[p.style==2?0:1][d%7]+12*(d/7);}
static void sound(int ch,int d){
 active[ch]=millis()+180;
 if(ch==0){Event e={0,(uint8_t)tails[0],(uint8_t)d,0,note_inc(68+transpose+octave*12),level[0]*240,drive[0]};enqueue(e);return;}
 for(int j=0;j<vn[ch];j++){
 int note=60+12*(oct[ch]+octave)+transpose+degree(d+j*2);if(note>127)note=127;if(note<0)note=0;
 Event e={(uint8_t)(vb[ch]+j),(uint8_t)tails[ch],0,(uint8_t)(shape[ch]+1),note_inc(note),((ch==2||ch==4)?70:160)*level[ch],toneDepth[ch]+drive[ch]+(motion[ch]?(int)((millis()/40)%12)*motion[ch]/8:0)};enqueue(e);
 }
}
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
 p.bpm=(uint8_t)((p.style==0?108:p.style==1?112:p.style==2?76:124)+rnd(13)-6);
 for(int s=0;s<NS;s++)for(int c=0;c<NC;c++)for(int i=0;i<16;i++)p.pat[s][c][i]=c?EMPTY:0;
 for(int s=0;s<NS;s++)for(int i=0;i<16;i++){
 if(s<4){uint8_t d=(i==0||(i%4==0?rnd(10)<7:rnd(12)==0))?1:0;if(s==1||s==2){if(i==4||i==12)d|=2;if(!(i&1))d|=4;}p.pat[s][0][i]=d;}
 if(s==1||s==2){if((!(i&1)&&rnd(5))||rnd(12)==0)p.pat[s][1][i]=rnd(3)?0:(uint8_t)(2+rnd(6));if(i%4==2)p.pat[s][2][i]=0;}
 if(s==2||s==3){if(rnd(10)<(s==2?5:2))p.pat[s][3][i]=(uint8_t)rnd(10);}
 if(s!=1&&i==0)p.pat[s][4][i]=0;
 if(s==0||s==2||s==4){static const uint8_t a[6]={0,2,4,7,4,2};if(rnd(6))p.pat[s][5][i]=a[(i+rnd(3))%6];}
 if(s==2&&i%4==2)p.pat[s][6][i]=rnd(2)?4:7;
 }
}
static void transport(void){
 section=repeat?loopSection:order[(pos/64)%8];bar=(pos/16)%4;step=pos%16;
 int cd=progress[p.style][bar];
 for(int c=0;c<8;c++){if(p.mute&(1<<c))continue;int d=p.pat[section][c][step];if(c==0){if(d)sound(c,d);}else if(d!=EMPTY)sound(c,d+cd);}
 pos++;
}
static void faceDraw(int i,int erase){
 Face *a=&f[i];int x=erase?a->ox:a->x,y=erase?a->oy:a->y;
 uint16_t col=erase?BLACK:(p.mute&(1<<i)?RGB(90,90,90):colors[i]);
 rect(x,y,24,18,col);
 if(erase)return;
 int hit=millis()<active[i];
 if(p.mute&(1<<i)){line(x+4,y+6,x+8,y+6,BLACK);line(x+15,y+6,x+19,y+6,BLACK);}
 else {rect(x+5,y+4,3,4,BLACK);rect(x+16,y+4,3,4,BLACK);}
 if(hit&&!(p.mute&(1<<i)))rect(x+8,y+10,8,5,BLACK);else line(x+8,y+12,x+15,y+12,BLACK);
 if(i==p.sel)frame(x-1,y-1,26,20,WHITE);
 a->ox=x;a->oy=y;
}
static void hud(void){
 if(page!=HOME){
  rect(0,48,240,145,BLACK);
  text(8,60,names[p.sel],colors[p.sel],FONT_SMALL);
  const char *labels[]={"LEVEL","FM DEPTH","DECAY","RATIO","MOTION","DRIVE"};
  int values[]={level[p.sel],toneDepth[p.sel],tails[p.sel],shape[p.sel]+1,motion[p.sel],drive[p.sel]};
  if(page==GLOBAL){labels[0]="MASTER";values[0]=masterLevel;labels[1]="KEY";values[1]=transpose;labels[2]="OCTAVE";values[2]=octave;}
  if(page==SEQ){labels[0]="STEP";values[0]=cursor+1;labels[1]="LOOP";values[1]=repeat;labels[2]="SECTION";values[2]=section+1;}
  for(int i=0;i<(page==GLOBAL||page==SEQ?3:6);i++){char value[12];format_int(value,values[i]);text(8,80+i*17,labels[i],WHITE,FONT_SMALL);text(135,80+i*17,value,WHITE,FONT_SMALL);}
 }

 rect(0,0,240,48,BLACK);text(0,0,"LIQUID WAVE",WHITE,FONT_SMALL);text(125,0,pageName[page],WHITE,FONT_SMALL);
 text(0,16,styles[p.style],WHITE,FONT_SMALL);char b[12];format_int(b,p.bpm);text(96,16,b,WHITE,FONT_SMALL);text(0,32,!p.sel&&page!=GLOBAL&&page!=SEQ?"TEMPO  LEVEL DRIVE TAIL":knobGuide[page],WHITE,FONT_SMALL);
 rect(0,194,240,46,BLACK);text(0,194,sections[section],WHITE,FONT_SMALL);
 text(96,194,names[p.sel],colors[p.sel],FONT_SMALL);text(192,194,recording?"REC":"",WHITE,FONT_SMALL);
 for(int i=0;i<16;i++){int d=p.pat[section][p.sel][i];uint16_t col=(p.sel?d!=EMPTY:d!=0)?colors[p.sel]:RGB(35,35,35);rect(i*15,218,12,12,col);if((playing&&i==step)||(page==SEQ&&i==cursor))frame(i*15,216,14,16,WHITE);}
}
void setup(void){
 clear(BLACK);p.magic=12345u;p.style=0;p.sel=0;p.mute=0;generate();
 Project saved;if(settings_load(&saved,sizeof(saved))==0&&saved.magic==p.magic&&saved.style<4&&saved.sel<8&&saved.bpm>=60&&saved.bpm<=180)p=saved;
 for(int i=0;i<8;i++){f[i].x=8+(i%4)*55;f[i].y=45+(i/4)*65;f[i].vx=rnd(2)?1:-1;f[i].vy=rnd(2)?1:-1;f[i].ox=f[i].x;f[i].oy=f[i].y;faceDraw(i,0);}
 hud();
}
void loop(void){
 uint32_t now=millis();
 if(button_pressed(BTN_HOME)){page=HOME;clear(BLACK);for(int i=0;i<8;i++)faceDraw(i,0);}
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
 if(button_pressed(BTN_SAVE)&&!playing)settings_save(&p,sizeof(p));
 int d=encoder(ENC_SELECT);if(d){p.sel=(uint8_t)wrap(p.sel+d,8);if(page==HOME)page=MIX;}
 d=encoder(ENC_PRESETS);if(d){p.style=(uint8_t)wrap(p.style+d,4);generate();pos=0;due=now;}
 int ch=p.sel;
 d=encoder(ENC_ALGORITHM);if(d){
  if(ch)shape[ch]=(uint8_t)wrap(shape[ch]+d,6);
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
 for(int j=0;j<8;j++){faceDraw(j,0);}faceIndex=(faceIndex+1)%8;
 }
 if(now-lastHud>=140){lastHud=now;hud();}
 led_button(BTN_PLAY,playing);led_button(BTN_REC,recording);
 led_button(BTN_HOME,page==HOME);led_button(BTN_SEQ,page==SEQ);led_button(BTN_FX,page==FX);
 led_button(BTN_ENV,page==ENV);led_button(BTN_LFO,page==MOTION);led_button(BTN_EDIT,page==EDIT);
 led_button(BTN_GLO,page==GLOBAL);led_button(BTN_SEL,(p.mute&(1<<p.sel))!=0);
}
