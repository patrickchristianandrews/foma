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
/* Procedural city film: no bitmap assets, no additional framebuffer. */
static unsigned city_hash(unsigned n){n^=n>>16;n*=0x7feb352du;n^=n>>15;return n;}
static void city_draw(uint32_t now){
 static const uint16_t sky[]={RGB(20,12,48),RGB(42,13,40),RGB(9,26,49),RGB(11,38,45),RGB(46,20,15),RGB(25,15,40),RGB(34,10,46),RGB(10,28,45)};
 static const uint16_t glow[]={RGB(80,220,255),RGB(255,140,60),RGB(120,160,255),RGB(70,255,185),RGB(255,115,45),RGB(200,120,255),RGB(255,75,180),RGB(130,210,255)};
 uint16_t ink=glow[scene];
 int pulse=(int)(eng_level()*40.0f);
 uint32_t shift=now/220u;
 rect(4,46,232,102,sky[scene]);frame(3,45,234,104,ink);
 for(int i=0;i<13;i++){
  unsigned h=city_hash((unsigned)(i+scene*29+7));
  int x=5+i*18-(int)(shift%18u),height=17+(int)(h%54u);
  int y=126-height;rect(x,y,16,height,RGB(8,12,25));
  line(x,y,x+15,y,RGB(49,53,75));
  for(int j=0;j<height/8;j++)for(int k=0;k<2;k++)
   if(((h>>(j%17))+j+k+(latched?pulse/2:0))%4u!=0)
    rect(x+3+k*7,y+4+j*8,3,3,(latched&&((j+k)%3==0))?ink:RGB(123,96,79));
 }
 rect(4,126,232,20,RGB(13,17,35));
 for(int i=0;i<12;i++){int x=6+i*20;line(x,128,x+(i%2?4:-4),144,RGB(44,51,75));}
 int car=8+(int)((now/19u)%205u);int cy=65+scene%3*15;
 rect(car,cy,19,4,RGB(185,192,217));rect(car+5,cy-2,9,2,ink);
 line(car-8,cy+3,car+1,cy+3,ink);
 for(int i=0;i<macros[3]/4;i++){
  unsigned a=city_hash((unsigned)i+scene*91u);
  int x=7+(int)((a+(now/38u)*3u)%220u);
  int y=49+(int)(((a>>8)+now/11u)%90u);
  line(x,y,x-2,y+5,RGB(86,120,155));
 }
 if(macros[3]>95&&now%6300u<90u)line(180,48,165,76,WHITE);
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
 static const char *const names[]={"MIDNIGHT BRASS","NEON TOWER","PYRAMID HALL","LAST SUNRISE","SKYLINE","DEEP SECTOR","ACID SKY"};
 text(8,30,names[preset],pink,FONT_SMALL);
 city_draw(millis());
 for(int i=0;i<4;i++){
  int x=5+i*59;char val[12];format_int(val,macros[i]);
  text(x,152,MACROS[i].name,WHITE,FONT_SMALL);
  rect(x,170,52,5,RGB(25,32,48));rect(x,170,52*macros[i]/127,5,neon);
  text(x,178,val,neon,FONT_SMALL);
 }
 text(5,197,drone?"DRN ON":"DRN OFF",pink,FONT_SMALL);
 text(80,197,epic?"EPIC ON":"EPIC OFF",pink,FONT_SMALL);
 text(160,197,memory?"MEM ON":"MEM OFF",pink,FONT_SMALL);
 text(5,218,"FX CITY / HOME EXIT",WHITE,FONT_SMALL);
}
#ifdef PLAT_HOST
int lw_offworld_expect(const char *name,int *v){
 if(!strcmp(name,"offworld_active"))*v=mode==2;
 else if(!strcmp(name,"offworld_notes"))*v=mode==2?(int)eng_notes():0;
 else if(!strcmp(name,"offworld_error"))*v=init_error;
 else return 0;return 1;
}
#endif
