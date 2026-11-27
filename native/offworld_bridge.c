#include <string.h>
/* SPDX-License-Identifier: GPL-3.0-only
 * Isolated proof-of-fit adapter for the original OFFWORLD DSP (MPL-2.0).
 * Not the original OFFWORLD UI; the complete engine is retained for measurement.
 */
#include "../platform/fm1.h"
#include "app.h"
#include "offworld/engine.h"
extern int lw_offworld_alloc(void);
extern unsigned lw_offworld_failed_bytes(void);
extern void lw_offworld_free(void);
extern int lw_radio_can_power_off(void);
extern void lw_offworld_echo_suspend(void);
extern void lw_offworld_echo_resume(void);
/* 0 normal; 1 transition/failed (silence); 2 OFFWORLD active.
 * Audio IRQ never allocates/frees; mode changes are serialized by plat_lock. */
static volatile int mode;
/* Diagnostic reason is main-loop owned: 1 network draining, 2 heap allocation. */
static int init_error;
static uint32_t retry_at;
static int preset,octave,drone,epic,memory,scene,tone,vib;
static uint8_t macros[4];
static uint32_t latched;
static const uint8_t knobs[]={ENC_KNOB1,ENC_KNOB2,ENC_KNOB3,ENC_KNOB4};
static int bound(int x,int low,int high){return x<low?low:x>high?high:x;}
static void set_mode(int v){uint32_t flags=plat_lock();mode=v;plat_unlock(flags);}
static void sound(void){eng_sound(preset,macros,tone/64.0f,vib/127.0f,PRESS_FILTER,0.7f);}
static void select_preset(int p){preset=(p%NPRESETS+NPRESETS)%NPRESETS;for(int i=0;i<4;i++)macros[i]=PRESETS[preset].m[i];sound();}
void lw_offworld_enter(void){
 if(mode==2)return;
 set_mode(1);init_error=0;
 if(!lw_radio_can_power_off()){init_error=1;retry_at=millis()+500u;return;}
 lw_offworld_echo_suspend();
 if(!lw_offworld_alloc()){init_error=2;lw_offworld_echo_resume();return;}
 octave=drone=epic=memory=scene=tone=0;vib=38;latched=0;
 select_preset(0);eng_world(0,0.25f);
 set_mode(2);
}
void lw_offworld_leave(void){
 int was=mode;set_mode(1);
 if(was==2){lw_offworld_free();lw_offworld_echo_resume();}
 init_error=0;set_mode(0);
}
int lw_offworld_render(int32_t *lr,uint32_t n,uint32_t gain){
 if(!mode)return 0;
 if(mode==2)eng_render(lr,n,(float)(gain>4096u?4096u:gain)*(8300000.0f/4096.0f));
 return 1;
}
void lw_offworld_input(void){
 if(mode!=2){
  if(button_pressed(BTN_PLAY)||(init_error==1&&(int32_t)(millis()-retry_at)>=0))lw_offworld_enter();
  return;
 }
 int d=encoder(ENC_PRESETS);
 if(d)select_preset(preset+d);
 for(int i=0;i<4;i++)if((d=encoder(knobs[i]))){macros[i]=(uint8_t)bound((int)macros[i]+d,0,127);sound();}
 if((d=encoder(ENC_SELECT))){tone=bound(tone+d,-64,64);sound();}
 if((d=encoder(ENC_ALGORITHM))){vib=bound(vib+d,0,127);sound();}
 if(button_pressed(BTN_OCT_DOWN))octave=bound(octave-1,-2,2);
 if(button_pressed(BTN_OCT_UP))octave=bound(octave+1,-2,2);
 if(button_pressed(BTN_PLAY)){drone=!drone;eng_all_off();latched=0;}
 if(button_pressed(BTN_ARP)){epic=!epic;eng_all_off();latched=0;}
 if(button_pressed(BTN_REC)){memory=!memory;eng_memory(memory);}
 if(button_pressed(BTN_FX)){scene=(scene+1)%NWORLD;eng_world(scene,0.25f);}
 for(int k=0;k<KEY_COUNT;k++){
  if(key_pressed(k)){
   if(drone&&!(keys()&~(1u<<k))){eng_all_off();latched=0;}
   int p=KEY_FIRST_NOTE+k+octave*12;
   eng_note_on(k,(float)p,0.8f);
   if(epic){eng_note_on(k+32,(float)(p+7),0.65f);eng_note_on(k+64,(float)(p+12),0.65f);}
   latched|=1u<<k;
  }
  if(key_released(k)&&!drone){eng_note_off(k);eng_note_off(k+32);eng_note_off(k+64);latched&=~(1u<<k);}
  led_key(k,!!(latched&(1u<<k)));
 }
 uint8_t st,a,b;
 while(midi_read(&st,&a,&b)){
  if((st&0xf0)==0x90&&b)eng_note_on(128+a,(float)a,(float)b/127.0f);
  else if((st&0xf0)==0x80||((st&0xf0)==0x90&&!b))eng_note_off(128+a);
 }
}
/* Alien horizons: procedural primitives only, no additional heap or assets. */
static unsigned world_hash(unsigned n){n^=n>>16;n*=0x7feb352du;n^=n>>15;return n;}
static const char *const worlds[NWORLD]={"RING WORLD","SPORE FOREST","VOID NEBULA","CRYSTAL SEA","LAVA MOON","SIGNAL PLANET","ALIEN RUINS","ICE ORBIT"};
static void orb(int x,int y,int r,uint16_t color){
 int d=0;
 for(int j=r;j>=0;j--){
  while(d<r && (d+1)*(d+1)+j*j<=r*r)d++;
  rect(x-d,y-j,2*d+1,1,color);
  if(j)rect(x-d,y+j,2*d+1,1,color);
 }
}
static void alien_draw(uint32_t now){
 static const uint16_t sky[NWORLD]={RGB(12,14,48),RGB(12,42,25),RGB(30,9,48),RGB(6,32,55),RGB(52,14,10),RGB(15,22,43),RGB(34,11,36),RGB(10,28,50)};
 static const uint16_t glow[NWORLD]={RGB(80,220,255),RGB(100,255,120),RGB(230,120,255),RGB(70,255,210),RGB(255,140,60),RGB(210,235,100),RGB(255,100,200),RGB(150,220,255)};
 uint16_t ink=glow[scene],dark=RGB(12,18,30);
 int pulse=bound((int)(eng_level()*30.0f),0,30);
 rect(4,46,232,91,sky[scene]);frame(3,45,234,93,ink);
 for(int i=0;i<24;i++){
  unsigned h=world_hash((unsigned)(i+scene*31+7));
  int x=8+(int)(h%222u),y=50+(int)((h>>12)%45u);
  pixel(x,y,((now/300u+i)%5u==0)?ink:RGB(150,160,190));
 }
 int px=177,py=76;
 orb(px,py,19,ink);orb(px+5,py-5,14,sky[scene]);
 if(scene==0||scene==7){
  line(px-30,py+9,px+30,py-9,ink);
  line(px-27,py+12,px+28,py-5,RGB(190,180,220));
 }
 rect(4,112,232,25,dark);
 for(int i=0;i<9;i++){
  unsigned h=world_hash((unsigned)(i+scene*19+13));
  int x=10+i*26,ht=10+(int)(h%22u),y=117-ht;
  if(scene==1){rect(x, y+6,3,ht,ink);orb(x+1,y+3,6,ink);orb(x+1,y+1,3,sky[scene]);}
  else if(scene==3||scene==6){line(x-5,124,x+4,y,ink);line(x+4,y,x+12,124,ink);line(x+4,y,x+4,124,RGB(100,140,170));}
  else {line(x-7,122,x+3,y,ink);line(x+3,y,x+13,122,ink);line(x-7,122,x+13,122,RGB(60,90,120));}
 }
 /* A drifting alien vessel and a note-reactive tractor beam. */
 int ship=16+(int)((now/55u)%141u),sy=77+(int)((now/600u)%3u);
 rect(ship-9,sy,19,3,ink);rect(ship-5,sy-3,11,3,RGB(185,220,220));
 rect(ship-5,sy+3,11,2,ink);
 if(latched||pulse>3){line(ship-4,sy+5,ship-11,110,ink);line(ship+4,sy+5,ship+11,110,ink);}
 for(int i=0;i<7;i++){
  int x=12+i*32;
  int h=2+(pulse*(1+(int)((i+now/120u)%4u)))/8;
  if(h>15)h=15;
  rect(x,134-h,18,h,ink);
 }
}
void lw_offworld_draw(void){
 uint16_t neon=RGB(70,220,255),pink=RGB(255,90,180);
 clear(BLACK);text(8,3,"NEON TIDES",neon,FONT_BIG);
 if(mode!=2){
  if(init_error==1){text(8,65,"WAITING FOR NETWORK",pink,FONT_SMALL);text(8,92,"AUTO RETRY",WHITE,FONT_SMALL);}
  else if(init_error==2){char bytes[12];format_int(bytes,(int)lw_offworld_failed_bytes());text(8,65,"AUDIO RAM ALLOCATION FAILED",pink,FONT_SMALL);text(8,92,"FAILED BLOCK BYTES",WHITE,FONT_SMALL);text(8,112,bytes,neon,FONT_SMALL);}
  else text(8,65,"INITIALIZING",pink,FONT_SMALL);
  text(8,212,"PLAY RETRY / HOME EXIT",WHITE,FONT_SMALL);return;
 }
 static const char *const names[NPRESETS]={"ALIEN BRASS","STAR CALL","ORBIT PAD","COSMIC DAWN","GRAVITY BASS","VOID DRONE","ACID SIGNAL"};
 static const char *const labels[4]={"K1 SWELL","K2 DRIFT","K3 SPACE","K4 RAIN"};
 text(8,28,names[preset],pink,FONT_SMALL);
 alien_draw(millis());
 text(8,140,worlds[scene],neon,FONT_SMALL);
 text(150,140,"FX: WORLD",WHITE,FONT_SMALL);
 for(int i=0;i<4;i++){
  int x=5+i*59;char val[12];format_int(val,macros[i]);
  text(x,156,labels[i],WHITE,FONT_SMALL);
  rect(x,170,52,5,RGB(40,48,64));rect(x,170,52*macros[i]/127,5,neon);
  text(x,177,val,neon,FONT_SMALL);
 }
 text(5,194,drone?"PLAY HOLD:ON":"PLAY HOLD:OFF",drone?neon:WHITE,FONT_SMALL);
 text(125,194,epic?"ARP CHORD:ON":"ARP CHORD:OFF",epic?neon:WHITE,FONT_SMALL);
 text(5,207,memory?"REC FREEZE:ON":"REC FREEZE:OFF",memory?neon:WHITE,FONT_SMALL);
 text(125,207,"PRESET: SOUND",WHITE,FONT_SMALL);
 text(5,221,"SEL TONE / ALG VIB / HOME NEXT",WHITE,FONT_SMALL);
}
#ifdef PLAT_HOST
int lw_offworld_expect(const char *name,int *v){
 if(!strcmp(name,"offworld_active"))*v=mode==2;
 else if(!strcmp(name,"offworld_notes"))*v=mode==2?(int)eng_notes():0;
 else if(!strcmp(name,"offworld_error"))*v=init_error;
 else if(!strcmp(name,"offworld_scene"))*v=scene;
 else if(!strcmp(name,"offworld_preset"))*v=preset;
 else if(!strcmp(name,"offworld_drone"))*v=drone;
 else return 0;return 1;
}
#endif
