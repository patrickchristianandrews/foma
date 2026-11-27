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
/* Direct canvas rendering: no additional image buffers or audio allocations. */
static void skyline(int base,uint32_t now,uint16_t ink,int pulse){
 uint16_t dark=RGB(12,18,30),far=RGB(40,42,66);
 for(int i=0;i<14;i++){
  unsigned h=world_hash((unsigned)(i+23));int x=5+i*16,ht=13+(int)(h%25u);
  rect(x,base-ht,15,ht,far);rect(x+4,base-ht-3,7,3,far);
 }
 for(int i=0;i<9;i++){
  unsigned h=world_hash((unsigned)(i+13));int x=8+i*25,ht=23+(int)(h%33u),w=17+(int)((h>>8)%6u),y=base-ht;
  rect(x,y,w,ht,dark);rect(x+1,y,1,ht,far);rect(x,y,w,1,ink);
  if(i%3==0){rect(x+4,y-3,w-8,3,dark);line(x+w/2,y-8,x+w/2,y-3,ink);pixel(x+w/2,y-9,WHITE);}
  else if(i%3==1){rect(x+3,y-4,w-6,4,far);rect(x+6,y-7,w-12,3,dark);}
  for(int j=0;j<(ht-5)/7;j++)for(int k=0;k<2;k++){
   unsigned q=h+(unsigned)(j*13+k*37);
   if(q%5u)rect(x+4+k*7,y+4+j*7,3,3,((q+now/160u)%11u<2u||(pulse>3&&(j+k)%4==0))?ink:((q+now/420u)%7u==0?dark:RGB(150,130,75)));
  }
 }
}
static void saucer(int x,int y,uint32_t now,uint16_t ink,int pulse,int floor){
 uint16_t dark=RGB(12,18,30),far=RGB(45,55,80);
 if(pulse>0){
  int spread=9+pulse/2;
  line(x-6,y+10,x-spread,floor,RGB(80,150,160));line(x+6,y+10,x+spread,floor,RGB(80,150,160));
  for(int i=-4;i<=4;i+=2)line(x+i,y+10,x+i*spread/4,floor,far);
  line(x-spread,floor,x+spread,floor,ink);
 }
 orb(x,y-3,7,ink);orb(x-2,y-5,3,RGB(210,245,240));
 rect(x-18,y,37,3,far);rect(x-23,y+3,47,4,ink);
 rect(x-19,y+7,39,3,far);rect(x-12,y+10,25,2,ink);
 for(int i=0;i<6;i++)rect(x-17+i*7,y+4,3,2,((now/110u+i)%6u==0)?WHITE:dark);
}
static void stars(uint32_t now,int top,int span,int seed,uint16_t ink){
 for(int i=0;i<26;i++){unsigned h=world_hash((unsigned)(i+seed*31+7));
  pixel(8+(int)(h%222u),top+(int)((h>>12)%(unsigned)span),((now/300u+i)%5u==0)?ink:RGB(150,160,190));}
}
static void alien_draw(uint32_t now){
 static const uint16_t sky[NWORLD]={RGB(12,14,48),RGB(12,42,25),RGB(30,9,48),RGB(6,32,55),RGB(52,14,10),RGB(15,22,43),RGB(34,11,36),RGB(10,28,50)};
 static const uint16_t glow[NWORLD]={RGB(80,220,255),RGB(100,255,120),RGB(230,120,255),RGB(70,255,210),RGB(255,140,60),RGB(210,235,100),RGB(255,100,200),RGB(150,220,255)};
 uint16_t ink=glow[scene],dark=RGB(12,18,30),far=RGB(40,42,66);
 int pulse=bound((int)(eng_level()*30.0f),0,30);
 rect(4,46,232,91,sky[scene]);stars(now,50,38,scene,ink);rect(4,130,232,7,dark);
 switch(scene){
 case 0:
  orb(191,68,15,ink);orb(195,64,11,sky[scene]);line(167,76,215,60,ink);line(169,79,213,63,WHITE);
  skyline(130,now,ink,pulse);break;
 case 1:
  for(int i=0;i<7;i++){int x=20+i*33,h=25+(int)(world_hash(i+19u)%20u),y=130-h;
   rect(x-3,y+4,7,h-4,far);orb(x,y,12,ink);rect(x-12,y,25,5,dark);
   for(int k=-8;k<=8;k+=4)pixel(x+k,y-4,WHITE);
   pixel(x+7,62+(int)((now/100u+i*17u)%64u),ink);}
  break;
 case 2:
  for(int i=0;i<5;i++){int x=40+i*37,y=91+(int)((i%2)*10);
   orb(x,y,13,far);orb(x,y,8,sky[scene]);line(x-17,y+7,x+17,y-7,ink);}
  rect(26,105,188,3,ink);rect(99,92,42,26,dark);frame(99,92,42,26,ink);
  for(int i=0;i<5;i++)rect(103+i*7,100,4,4,((now/170u+i)%3u)?far:WHITE);
  break;
 case 3:
  rect(4,110,232,20,RGB(15,70,105));
  for(int i=0;i<6;i++){int x=20+i*39,h=20+(int)(world_hash(i+43u)%23u);
   line(x-12,111,x,111-h,ink);line(x,111-h,x+12,111,ink);line(x,111-h,x,111,WHITE);
   line(x-12,111,x+12,111,ink);line(x-9,114,x,124,far);line(x,124,x+9,114,far);}
  for(int i=0;i<4;i++)rect(9+(int)((now/45u+i*53u)%196u),114+i*4,25,1,ink);
  break;
 case 4:
  for(int i=0;i<3;i++){int x=42+i*77,y=85+(i%2)*8;
   for(int k=0;k<37;k++)rect(x-k,y+k,2*k+1,1,far);
   line(x,y+1,x-13,122,ink);line(x+3,y+4,x+16,121,RGB(255,220,100));
   pixel(x+(int)(now/95u%9u)-4,y-5,ink);}
  for(int i=0;i<8;i++)rect(6+i*28,125-(int)((now/160u+i)%3u),25,3,ink);
  break;
 case 5:
  for(int i=0;i<3;i++){int x=38+i*81,y=102-i%2*12;
   rect(x-3,y,7,130-y,far);line(x-20,y-12,x,y+4,ink);line(x,y+4,x+20,y-12,ink);
   line(x-20,y-12,x+20,y-12,far);line(x,y+4,x,y-19,WHITE);
   line(x,y-19,x+(int)((now/100u+i*9u)%21u)-10,52,ink);}
  rect(8,123,224,7,far);break;
 case 6:
  for(int i=0;i<3;i++){int x=44+i*76,h=34+(i%2)*8;
   for(int j=0;j<5;j++){int w=12+j*11;rect(x-w/2,130-h+j*h/5,w,h/5+1,far);line(x-w/2,130-h+j*h/5,x+w/2,130-h+j*h/5,ink);}
   rect(x-5,117,11,13,dark);frame(x-5,117,11,13,ink);}
  orb(120,78,10,ink);orb(120,78,7,sky[scene]);pixel(120+(int)(now/130u%11u)-5,78,WHITE);break;
 case 7:
  for(int i=0;i<9;i++){int x=15+i*26,h=15+(int)(world_hash(i+67u)%34u);
   line(x-10,130,x,130-h,ink);line(x,130-h,x+11,130,WHITE);line(x,130-h,x,130,far);}
  for(int x=8;x<229;x+=5){int y=63+((sine((uint32_t)x*0x03000000u+now*140000u)*7)>>15);rect(x,y,5,2,ink);}
  for(int i=0;i<12;i++){unsigned h=world_hash(i+7u);pixel(8+(int)(h%220u),53+(int)(((h>>12)+now/110u)%74u),WHITE);}break;
 }
 int travel=(int)((now/38u)%320u),ship=35+(travel>160?320-travel:travel);
 int bob=(int)((now/160u)%8u),sy=59+(bob>4?8-bob:bob);
 saucer(ship,sy,now,ink,(latched||pulse>3)?pulse+1:0,119);
 for(int i=0;i<macros[3]/8;i++){unsigned q=world_hash((unsigned)i+scene*91u);
  int x=7+(int)((q+(now/35u)*2u)%222u),y=50+(int)(((q>>8)+now/13u)%38u);line(x,y,x-2,y+4,RGB(86,120,155));}
 frame(3,45,234,93,ink);
}
/* Boot renderer is independent of audio-engine allocation. */
void lw_neon_boot_draw(uint32_t elapsed,uint32_t duration){
 uint16_t ink=RGB(95,210,255),night=RGB(12,14,48);
 clear(night);stars(elapsed,5,90,0,ink);orb(190,42,18,ink);orb(195,37,14,night);
 skyline(170,elapsed,ink,0);rect(0,170,240,70,RGB(10,42,65));
 int front=(int)(elapsed*290u/duration)-25;
 for(int x=0;x<240;x+=3){int y=183+((sine((uint32_t)x*0x05000000u-elapsed*2100000u)*8)>>15);
  rect(x,y,3,4,RGB(60,140,210));
  if(x<=front&&x>front-65){rect(x,y-3,3,3,ink);rect(x,y+4,3,2,RGB(220,125,235));}}
 saucer(front,72,elapsed,ink,0,0);
 text(32,10,"LIQUID WAVE",WHITE,FONT_BIG);text(78,211,"NEON TIDES",ink,FONT_SMALL);
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
