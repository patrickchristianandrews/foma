/* SPDX-License-Identifier: GPL-3.0-only */
/* Pocket Wave experimental native prototype. Not hardware tested.
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
static const uint8_t order[8]={0,1,2,1,2,3,2,4};
static const char *names[8]={"DRUM","BASS","CHORD","LEAD","PAD","ARP","EXTRA","FREE"};
static const char *sections[5]={"INTRO","VERSE","CHORUS","BRIDGE","OUTRO"};
static const char *styles[4]={"SYNTH","FUNK","BALLAD","HOUSE"};
static const uint8_t scale[2][7]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10}};
static const uint8_t progress[4][4]={{0,5,2,6},{0,3,0,4},{0,4,5,3},{0,0,5,6}};
static const uint16_t colors[8]={RGB(255,100,40),RGB(190,90,255),RGB(255,210,60),RGB(70,220,250),RGB(255,120,210),RGB(90,255,170),RGB(250,150,80),RGB(160,180,255)};
static const uint8_t vb[8]={0,0,1,4,5,8,9,10};
static const uint8_t vn[8]={0,1,3,1,3,1,1,1};
static const int8_t oct[8]={0,-2,-1,0,-1,1,0,0};
static const uint8_t decay[8]={0,12,13,13,15,11,12,13};
static const uint8_t depth[8]={0,16,20,28,8,36,24,12};
static volatile uint8_t head,tail;
typedef struct {uint8_t voice,sh,drum;uint32_t inc;int32_t amp,idx;} Event;
static Event queue[64];
typedef struct {uint32_t ph,mp,inc;int32_t env,idx;uint8_t sh;} Voice;
static Voice v[NV];
static int32_t ke,se,he,prevNoise; static uint32_t kp,ki,kbase,noise=1234567;
static int playing,recording,pos,section,step,bar;
static uint32_t due,remainder,active[8],lastFace,lastHud;
typedef struct {int x,y,vx,vy,ox,oy;uint8_t expr; } Face;
static Face f[8]; static uint8_t faceIndex;
static uint32_t rnd(uint32_t n){return n?random32()%n:0;}
static void enqueue(Event e){uint8_t h=(head+1)&63;if(h==tail)return;queue[head]=e;head=h;}
static int degree(int d){return scale[p.style==2?0:1][d%7]+12*(d/7);}
static void sound(int ch,int d){
 active[ch]=millis()+180;
 if(ch==0){Event e={0,0,(uint8_t)d,0,0,0};enqueue(e);return;}
 for(int j=0;j<vn[ch];j++){
 int note=60+12*oct[ch]+degree(d+j*2);if(note>127)note=127;if(note<0)note=0;
 Event e={(uint8_t)(vb[ch]+j),decay[ch],0,note_inc(note),(ch==2||ch==4)?7000:16000,depth[ch]};enqueue(e);
 }
}
void audio(int32_t *out,uint32_t frames){
 while(tail!=head){Event e=queue[tail];tail=(tail+1)&63;
 if(e.drum){if(e.drum&1){ke=24000;ki=note_inc(68);kbase=note_inc(34);}if(e.drum&2)se=18000;if(e.drum&12)he=10000;}
 else {Voice *a=&v[e.voice];a->inc=e.inc;a->env=e.amp<<14;a->idx=e.idx;a->sh=e.sh;}}
 for(uint32_t n=0;n<frames;n++){
 int32_t sum=0;
 for(int j=0;j<NV;j++){Voice *a=&v[j];int32_t env=a->env>>14;if(env<8)continue;
 a->ph+=a->inc;a->mp+=a->inc*2u;
 int32_t dep=(a->idx*env)>>15;
 uint32_t offset=(uint32_t)((int64_t)sine(a->mp)*dep*1304);
 sum+=(sine(a->ph+offset)*env)>>17;a->env-=a->env>>a->sh;}
 noise^=noise<<13;noise^=noise>>17;noise^=noise<<5;int32_t nz=(int32_t)(noise>>17)-16384;
 if(ke>32){kp+=ki;ki-=(ki-kbase)>>9;sum+=(sine(kp)*ke)>>17;ke-=ke>>11;}
 if(se>32){sum+=(nz*se)>>17;se-=se>>10;}
 if(he>32){sum+=((nz-prevNoise)*he)>>18;he-=he>>10;}prevNoise=nz;
 if(sum>24000)sum=24000;if(sum< -24000)sum=-24000;
 out[n*2]+=sum;out[n*2+1]+=sum;
 }
}
static void generate(void){
 p.bpm=(uint8_t)(p.style==0?108:p.style==1?112:p.style==2?76:124);
 for(int s=0;s<NS;s++)for(int c=0;c<NC;c++)for(int i=0;i<16;i++)p.pat[s][c][i]=c?EMPTY:0;
 for(int s=0;s<NS;s++)for(int i=0;i<16;i++){
 if(s<4){uint8_t d=(i%4==0)?1:0;if(s==1||s==2){if(i==4||i==12)d|=2;if(!(i&1))d|=4;}p.pat[s][0][i]=d;}
 if(s==1||s==2){if(!(i&1))p.pat[s][1][i]=rnd(6)?0:4;if(i%4==2)p.pat[s][2][i]=0;}
 if(s==2||s==3){if(rnd(10)<(s==2?5:2))p.pat[s][3][i]=(uint8_t)rnd(10);}
 if(s!=1&&i==0)p.pat[s][4][i]=0;
 if(s==0||s==2||s==4){static const uint8_t a[6]={0,2,4,7,4,2};p.pat[s][5][i]=a[i%6];}
 if(s==2&&i%4==2)p.pat[s][6][i]=rnd(2)?4:7;
 }
}
static void transport(void){
 section=order[(pos/64)%8];bar=(pos/16)%4;step=pos%16;
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
 rect(0,0,240,32,BLACK);text(0,0,"POCKET WAVE",WHITE,FONT_SMALL);
 text(0,16,styles[p.style],WHITE,FONT_SMALL);char b[12];format_int(b,p.bpm);text(96,16,b,WHITE,FONT_SMALL);
 rect(0,194,240,46,BLACK);text(0,194,playing?sections[section]:"STOP",WHITE,FONT_SMALL);
 text(96,194,names[p.sel],colors[p.sel],FONT_SMALL);text(192,194,recording?"REC":"",WHITE,FONT_SMALL);
 for(int i=0;i<16;i++){int d=p.pat[playing?section:0][p.sel][i];uint16_t col=(p.sel?d!=EMPTY:d!=0)?colors[p.sel]:RGB(35,35,35);rect(i*15,218,12,12,col);if(playing&&i==step)frame(i*15,216,14,16,WHITE);}
}
void setup(void){
 clear(BLACK);p.magic=0x50573031;p.style=0;p.sel=0;p.mute=0;generate();
 Project saved;if(settings_load(&saved,sizeof(saved))==0&&saved.magic==p.magic&&saved.style<4&&saved.sel<8&&saved.bpm>=60&&saved.bpm<=180)p=saved;
 for(int i=0;i<8;i++){f[i].x=8+(i%4)*55;f[i].y=45+(i/4)*65;f[i].vx=rnd(2)?1:-1;f[i].vy=rnd(2)?1:-1;f[i].ox=f[i].x;f[i].oy=f[i].y;faceDraw(i,0);}
 hud();
}
void loop(void){
 uint32_t now=millis();
 if(button_pressed(BTN_PLAY)){playing=!playing;if(playing){pos=0;due=now;remainder=0;}}
 if(button_pressed(BTN_REC))recording=!recording;
 int d=encoder(ENC_SELECT);if(d)p.sel=(uint8_t)((p.sel+d%8+8)%8);
 d=encoder(ENC_PRESETS);if(d){p.style=(uint8_t)((p.style+d%4+4)%4);generate();}
 d=encoder(ENC_KNOB1);if(d){int b=p.bpm+d;if(b<60)b=60;if(b>180)b=180;p.bpm=(uint8_t)b;}
 if(button_pressed(BTN_SEL))p.mute^=(1<<p.sel);
 if(button_pressed(BTN_ARP))generate();
 if(button_pressed(BTN_SEQ)){section=(section+1)%5;for(int i=0;i<8;i++)if(order[i]==section){pos=i*64;break;}}
 if(button_pressed(BTN_SAVE)&&!playing)settings_save(&p,sizeof(p));
 for(int k=0;k<KEY_COUNT;k++)if(key_pressed(k)){
 int d=p.sel==0?(1<<(k%4)):k%10;sound(p.sel,d);
 if(recording)p.pat[playing?section:0][p.sel][playing?step:k%16]=(uint8_t)d;
 }
 int catchup=0;while(playing&&(int32_t)(now-due)>=0&&catchup++<4){transport();due+=15000/p.bpm;remainder+=15000%p.bpm;if(remainder>=p.bpm){remainder-=p.bpm;due++;}}
 if(now-lastFace>=35){lastFace=now;int i=faceIndex;Face *a=&f[i];
 rect(a->ox-1,a->oy-1,26,20,BLACK);
 a->x+=a->vx;a->y+=a->vy;if(a->x<2){a->x=2;a->vx=1;}if(a->x>214){a->x=214;a->vx=-1;}if(a->y<35){a->y=35;a->vy=1;}if(a->y>170){a->y=170;a->vy=-1;}
 for(int j=0;j<8;j++)faceDraw(j,0);faceIndex=(faceIndex+1)%8;
 }
 if(now-lastHud>=140){lastHud=now;hud();}
 led_button(BTN_PLAY,playing);led_button(BTN_REC,recording);
}
