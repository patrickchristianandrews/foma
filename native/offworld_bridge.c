#include <string.h>
/* SPDX-License-Identifier: GPL-3.0-only
 * Isolated proof-of-fit adapter for the original OFFWORLD DSP (MPL-2.0).
 * Not the original OFFWORLD UI; the complete engine is retained for measurement.
 */
#include "../platform/fm1.h"
#include "app.h"
#include "offworld/engine.h"
extern int lw_offworld_alloc(void);
extern void lw_offworld_free(void);
extern int lw_radio_can_power_off(void);
extern void lw_offworld_echo_suspend(void);
extern void lw_offworld_echo_resume(void);
/* 0 normal; 1 transition/failed (silence); 2 OFFWORLD active.
 * Audio IRQ never allocates/frees; mode changes are serialized by plat_lock. */
static volatile int mode;
static int preset,octave,drone,epic,memory,scene,tone,vib;
static uint8_t macros[4];
static uint32_t latched;
static const uint8_t knobs[]={ENC_KNOB1,ENC_KNOB2,ENC_KNOB3,ENC_KNOB4};
static int bound(int x,int low,int high){return x<low?low:x>high?high:x;}
static void set_mode(int v){uint32_t flags=plat_lock();mode=v;plat_unlock(flags);}
static void sound(void){eng_sound(preset,macros,tone/64.0f,vib/127.0f,PRESS_FILTER,0.7f);}
static void select_preset(int p){preset=(p%NPRESETS+NPRESETS)%NPRESETS;for(int i=0;i<4;i++)macros[i]=PRESETS[preset].m[i];sound();}
void lw_offworld_enter(void){
 set_mode(1);
 if(!lw_radio_can_power_off())return;
 lw_offworld_echo_suspend();
 if(!lw_offworld_alloc()){lw_offworld_echo_resume();return;}
 octave=drone=epic=memory=scene=tone=0;vib=38;latched=0;
 select_preset(0);eng_world(0,0.25f);
 set_mode(2);
}
void lw_offworld_leave(void){
 int was=mode;set_mode(1);
 if(was==2){lw_offworld_free();lw_offworld_echo_resume();}
 set_mode(0);
}
int lw_offworld_render(int32_t *lr,uint32_t n,uint32_t gain){
 if(!mode)return 0;
 if(mode==2)eng_render(lr,n,(float)(gain>4096u?4096u:gain)*(8300000.0f/4096.0f));
 return 1;
}
void lw_offworld_input(void){
 if(mode!=2){if(button_pressed(BTN_PLAY))lw_offworld_enter();return;}
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
void lw_offworld_draw(void){
 uint16_t neon=RGB(70,220,255),pink=RGB(255,90,180);
 clear(BLACK);text(8,5,"7 / OFFWORLD",neon,FONT_BIG);
 if(mode!=2){text(8,65,"ENGINE UNAVAILABLE",pink,FONT_SMALL);text(8,92,"MEMORY OR NETWORK BUSY",WHITE,FONT_SMALL);text(8,212,"PLAY RETRY / HOME EXIT",WHITE,FONT_SMALL);return;}
 text(8,36,PRESETS[preset].name,pink,FONT_SMALL);
 for(int i=0;i<4;i++){
  int y=65+i*31;char val[12];format_int(val,macros[i]);
  text(8,y,MACROS[i].name,WHITE,FONT_SMALL);text(186,y,val,neon,FONT_SMALL);
  rect(8,y+15,220,5,RGB(25,32,48));rect(8,y+15,220*macros[i]/127,5,neon);
 }
 text(8,190,drone?"DRONE ON":"DRONE OFF",pink,FONT_SMALL);
 text(122,190,epic?"EPIC ON":"EPIC OFF",pink,FONT_SMALL);
 text(8,212,"HOME EXIT / REC MEMORY",WHITE,FONT_SMALL);
}
#ifdef PLAT_HOST
int lw_offworld_expect(const char *name,int *v){
 if(!strcmp(name,"offworld_active"))*v=mode==2;
 else if(!strcmp(name,"offworld_notes"))*v=mode==2?(int)eng_notes():0;
 else return 0;return 1;
}
#endif
