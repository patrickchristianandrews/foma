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
/* Main-loop-only encounter state; no framebuffer or audio allocations. */
static uint32_t event_at,event_next,event_count;
static int event_kind,event_x,event_origin;
static unsigned event_phase_mask;
#define EVENT_MS 5200u
#define APPROACH_MS 1100u
#define LIFT_MS 3900u
#define FINISH_MS 4600u
static int ship_x(uint32_t now){int travel=(int)((now/38u)%320u);return 35+(travel>160?320-travel:travel);}
static unsigned world_hash(unsigned n);
/* Film control is independent of the synth and uses only scalar state. */
static uint32_t cut_at,cut_due,cut_count,film_seed;
static int world_page,world_level,logs_on,auto_cut,idle_events;
#ifdef PLAT_HOST
static unsigned rendered_worlds,rendered_events,completed_events,abducted_worlds;
#endif
static unsigned seen_worlds;
extern void lw_neon_shift_row(int y,int x,int w,int d);
static void event_reset(uint32_t now);
static void film_cut(uint32_t now,int next){
 scene=next;cut_at=now;cut_count++;seen_worlds|=1u<<scene;
 film_seed=world_hash(film_seed+now+cut_count);cut_due=now+18000u+film_seed%17000u;
 event_reset(now);eng_world(scene,world_level/100.0f);
}
static void film_auto(uint32_t now){
 if(!auto_cut||event_kind||(int32_t)(now-cut_due)<0)return;
 /* Random order without repeats until all eight worlds have been visited. */
 unsigned choices=(~seen_worlds)&((1u<<NWORLD)-1u);
 if(!choices){seen_worlds=1u<<scene;choices=(~seen_worlds)&((1u<<NWORLD)-1u);}
 film_seed=world_hash(film_seed+now+1u);
 int next=(int)(film_seed%NWORLD);
 for(int i=0;i<NWORLD;i++,next=(next+1)%NWORLD)if(choices&(1u<<next)){film_cut(now,next);break;}
}
static void event_reset(uint32_t now){event_kind=0;event_at=now;event_phase_mask=0;event_next=now+6000u+world_hash(now+scene)%7000u;}
static void event_start(uint32_t now,int key){
 if(event_kind)return;
 /* First note in each world triggers its signature event. Subsequent notes
  * can trigger another encounter after the current animation completes. */
 event_at=now;event_phase_mask=0;event_kind=1+scene;event_origin=ship_x(now);event_x=45+(int)(world_hash(now+(unsigned)key*37u)%150u);event_count++;
 event_next=now+EVENT_MS+6000u+world_hash(now+event_count)%7000u;
 /* Use the pinned engine's existing, queued environmental effects. */
 if(world_level){
  float pan=(event_x-120)/120.0f;
  if(scene==0||scene==2||scene==5||scene==7)eng_sfx(SFX_FLYBY,pan<0?-1.0f:1.0f,2.2f);
  else if(scene==4)eng_sfx(SFX_FLAME,0.0f,0.0f);
  else if(scene==6||scene==3)eng_sfx(SFX_ZAP,0.0f,0.0f);
  else eng_sfx(SFX_CLINK,0.0f,0.0f);
 }
}
/* Eight short interactive music lessons. Pitch classes follow MIDI C=0.
 * Physical keyboard input is evaluated, never synthetic EPIC harmonies. */
#define PC(n) (1u<<(n))
#define TRI(r,a,b) (PC(r)|PC(a)|PC(b))
static const uint16_t lesson_steps[NWORLD][8]={
 {PC(0),PC(2),PC(4),PC(7)},
 {PC(0),PC(2),PC(4),PC(5),PC(7),PC(9),PC(11),PC(0)},
 {PC(9),PC(11),PC(0),PC(2),PC(4),PC(5),PC(7),PC(9)},
 {TRI(0,4,7),TRI(5,9,0),TRI(7,11,2)},
 {PC(0),PC(0),PC(0),PC(0)},
 {PC(0),PC(7),PC(2),PC(9),PC(4),PC(11)},
 {TRI(9,0,4),TRI(5,9,0),TRI(0,4,7),TRI(7,11,2)},
 {PC(0),PC(0),PC(7),PC(7),PC(9),PC(9),PC(7),PC(4)}
};
static const uint8_t lesson_lengths[NWORLD]={4,8,8,3,4,6,4,8};
static const char *const lesson_names[NWORLD]={"FIND THE NOTES","C MAJOR SCALE","A MINOR SCALE","MAJOR TRIADS","STEADY BEAT","PERFECT FIFTHS","CHORD JOURNEY","MELODY MISSION"};
static const char *const pitch_names[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static int lesson,lesson_step,lesson_wait,lesson_release,lesson_feedback,lesson_streak;
static unsigned lesson_score,lesson_done;
static const char *const backing_names[]={"OFF","ORBIT PULSE","NEBULA DRIFT","ALIEN GROOVE","STAR ARPEGGIO"};
static const uint8_t backing_roots[4][4]={{9,5,0,7},{0,9,5,7},{9,7,5,7},{0,5,9,7}};
static const uint16_t backing_ms[]={600,600,720,480,420};
static int backing_style,backing_beat,backing_root,backing_minor,backing_running;
static uint32_t backing_due,backing_hits,backing_flash,backing_notes;
static unsigned backing_mask(void){return PC(backing_root)|PC((backing_root+(backing_minor?3:4))%12)|PC((backing_root+7)%12);}
static unsigned backing_period(void){return lesson&&scene==4?600u:backing_ms[backing_style];}
static void backing_stop(void){if(backing_running){eng_note_off(300);eng_note_off(301);}backing_running=0;}
static void backing_restart(uint32_t now){backing_stop();backing_beat=0;backing_due=now;}
static void backing_tick(uint32_t now){
 if(!backing_style){backing_stop();return;}
 if((int32_t)(now-backing_due)<0)return;
 int beat=backing_beat++%16,part=beat%4;
 backing_due=now+backing_period();
 int root=backing_roots[backing_style-1][beat/4];
 if(lesson&&scene==2&&!lesson_wait)root=9;
 if(lesson&&scene==4&&!lesson_wait)root=0;
 if(lesson&&(scene==3||scene==6)&&!lesson_wait){
  static const uint8_t chord_roots[2][4]={{0,5,7,7},{9,5,0,7}};
  root=chord_roots[scene==6][lesson_step];
 }
 int minor=root==9;
 backing_root=root;backing_minor=minor;
 int bass=36+root;
 if(part==0||backing_style==3){eng_note_on(300,(float)(bass+(backing_style==3&&part%2?12:0)),0.25f);backing_notes++;}
 int interval=backing_style==4?(part==1?(minor?3:4):part==2?7:part==3?12:0):backing_style==2?(part%2?7:0):backing_style==3?(part%2?7:12):7;
 eng_note_on(301,(float)(48+root+interval),backing_style==2?0.16f:0.21f);backing_notes++;
 backing_running=1;
}
static void backing_play(uint32_t now,int midi){
 if(!backing_style)return;
 if(backing_mask()&PC(midi%12)){backing_hits++;backing_flash=now;}
}
static void backing_cycle(uint32_t now){backing_style=(backing_style+1)%5;backing_restart(now);}

static uint32_t lesson_feedback_at,lesson_beat;
static unsigned lesson_pitch_mask(uint32_t held){
 unsigned mask=0;
 for(int k=0;k<KEY_COUNT;k++)if(held&(1u<<k))mask|=PC((KEY_FIRST_NOTE+k)%12);
 return mask;
}
static unsigned lesson_target(void){return lesson_step<lesson_lengths[scene]?lesson_steps[scene][lesson_step]:0;}
static void lesson_reset_step(void){lesson_step=lesson_wait=lesson_release=lesson_feedback=0;lesson_beat=0;}
static void lesson_toggle(uint32_t now){
 lesson=!lesson;world_page=0;lesson_reset_step();backing_restart(now);
 eng_all_off();latched=0;
 if(lesson){
  drone=epic=0;lesson_score=lesson_done=0;lesson_streak=0;
  film_cut(now,0);
 }else{
  event_reset(now);cut_due=now+18000u+film_seed%17000u;
 }
}
static void lesson_correct(uint32_t now){
 lesson_feedback=1;lesson_feedback_at=now;lesson_streak++;lesson_score+=10u;
 lesson_step++;lesson_release=1;
 if(lesson_step>=lesson_lengths[scene]){
  lesson_wait=1;lesson_done|=1u<<scene;
  event_start(now,scene);
 }
}
static void lesson_wrong(uint32_t now){lesson_feedback=-1;lesson_feedback_at=now;lesson_streak=0;lesson_release=1;}
static void lesson_input(uint32_t now,int pressed){
 if(!lesson)return;
 if(lesson_wait){
  if(!event_kind){
   if(lesson_done==255u){lesson_wait=2;return;}
   film_cut(now,(scene+1)%NWORLD);lesson_reset_step();backing_restart(now);
  }
  return;
 }
 if(lesson_release){if(!keys())lesson_release=0;return;}
 if(!pressed)return;
 unsigned target=lesson_target(),held=lesson_pitch_mask(keys());
 if(held&~target){lesson_wrong(now);return;}
 if(held!=target)return; /* Allow building a chord one key at a time. */
 if(scene==4){
  if(lesson_step){
   int32_t delta=(int32_t)(now-lesson_beat);
   if(delta<380||delta>820){lesson_step=0;lesson_beat=0;lesson_wrong(now);return;}
  }
  lesson_beat=now;
 }
 lesson_correct(now);
}
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
 select_preset(0);event_count=0;world_page=0;world_level=25;logs_on=1;auto_cut=1;idle_events=1;
 lesson=0;lesson_reset_step();lesson_score=lesson_done=0;lesson_streak=0;
 backing_style=1;backing_running=0;backing_notes=backing_hits=0;backing_flash=0;backing_root=9;backing_minor=1;
 uint32_t now=millis();backing_restart(now);cut_count=0;film_seed=world_hash(now+123u);seen_worlds=1u;cut_at=now-400u;cut_due=now+18000u+film_seed%17000u;
#ifdef PLAT_HOST
 rendered_worlds=rendered_events=completed_events=abducted_worlds=0;
#endif
 eng_world(0,world_level/100.0f);event_reset(now);
 set_mode(2);
}
void lw_offworld_leave(void){
 int was=mode;set_mode(1);
 if(was==2){lw_offworld_free();lw_offworld_echo_resume();}
 init_error=0;event_kind=0;set_mode(0);
}
int lw_offworld_render(int32_t *lr,uint32_t n,uint32_t gain){
 if(!mode)return 0;
 if(mode==2)eng_render(lr,n,(float)(gain>4096u?4096u:gain)*(8300000.0f/4096.0f));
 return 1;
}
void lw_offworld_input(void){
 if(mode!=2){
  if(button_pressed(BTN_PLAY)||(init_error==1&&(int32_t)(millis()-retry_at)>=0))lw_offworld_enter();
  for(int k=0;k<KEY_COUNT;k++)led_key(k,0);
  return;
 }
 uint32_t now=millis();
 if(button_pressed(BTN_ENV))lesson_toggle(now);
 if(button_pressed(BTN_LFO))backing_cycle(now);
 if(!lesson&&button_pressed(BTN_FX))world_page=!world_page;
 if(world_page&&button_pressed(BTN_SEL))film_cut(now,(scene+1)%NWORLD);
 if(world_page&&button_pressed(BTN_GLO)){auto_cut=!auto_cut;cut_due=now+18000u+film_seed%17000u;}
 if(event_kind&&now-event_at>=EVENT_MS&&((event_phase_mask&15u)==15u||now-event_at>=EVENT_MS+1800u))event_kind=0;
 if(!lesson)film_auto(now);
 int d=encoder(ENC_PRESETS);
 if(d)select_preset(preset+d);
 for(int i=0;i<4;i++)if((d=encoder(knobs[i]))){
  if(!world_page){macros[i]=(uint8_t)bound((int)macros[i]+d,0,127);sound();}
  else if(i==0){world_level=bound(world_level+d,0,100);eng_world(scene,world_level/100.0f);}
  else if(i==1){idle_events=d>0;event_next=now+12000u+world_hash(now+scene)%16000u;}
  else if(i==2)logs_on=d>0;
  else {int next=(scene+d%NWORLD+NWORLD)%NWORLD;if(next!=scene)film_cut(now,next);}
 }
 if((d=encoder(ENC_SELECT))){tone=bound(tone+d,-64,64);sound();}
 if((d=encoder(ENC_ALGORITHM))){vib=bound(vib+d,0,127);sound();}
 if(button_pressed(BTN_OCT_DOWN))octave=bound(octave-1,-2,2);
 if(button_pressed(BTN_OCT_UP))octave=bound(octave+1,-2,2);
 if(!lesson&&button_pressed(BTN_PLAY)){drone=!drone;eng_all_off();latched=0;backing_restart(now);}
 if(!lesson&&button_pressed(BTN_ARP)){epic=!epic;eng_all_off();latched=0;backing_restart(now);}
 if(button_pressed(BTN_REC)){memory=!memory;eng_memory(memory);}


 if(!lesson&&world_page&&button_pressed(BTN_EDIT))event_start(now,scene);
 if(!lesson&&idle_events&&!event_kind&&(int32_t)(now-event_next)>=0)event_start(now,scene);
 backing_tick(now);
 int pressed=0;
 for(int k=0;k<KEY_COUNT;k++){
  if(key_pressed(k)){
   pressed=1;if(!lesson)event_start(now,k);
   if(drone&&!(keys()&~(1u<<k))){eng_all_off();latched=0;}
   int p=KEY_FIRST_NOTE+k+octave*12;
   backing_play(now,p);
   eng_note_on(k,(float)p,0.8f);
   if(epic){eng_note_on(k+32,(float)(p+7),0.65f);eng_note_on(k+64,(float)(p+12),0.65f);}
   latched|=1u<<k;
  }
  if(key_released(k)&&!drone){eng_note_off(k);eng_note_off(k+32);eng_note_off(k+64);latched&=~(1u<<k);}
  led_key(k,!!(latched&(1u<<k)));
 }
 lesson_input(now,pressed);
 /* Steady hints in the physical C4-B4 octave. Held keys remain lit. */
 if((lesson&&!lesson_wait)||(backing_style&&!world_page)){
  unsigned target=lesson&&!lesson_wait?lesson_target():backing_mask();
  for(int k=0;k<KEY_COUNT;k++){int midi=KEY_FIRST_NOTE+k;
   led_key(k,!!(latched&(1u<<k))||(midi>=60&&midi<=71&&(target&PC(midi%12))));}}
 uint8_t st,a,b;
 while(midi_read(&st,&a,&b)){
  if((st&0xf0)==0x90&&b){if(!lesson)event_start(now,a);backing_play(now,a);eng_note_on(128+a,(float)a,(float)b/127.0f);}
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
 for(int i=0;i<15;i++){
  unsigned h=world_hash((unsigned)(i+23));int x=5+i*16-(int)((now/220u)%16u),ht=13+(int)(h%25u);
  rect(x,base-ht,15,ht,far);rect(x+4,base-ht-3,7,3,far);
 }
 for(int i=0;i<10;i++){
  unsigned h=world_hash((unsigned)(i+13));int x=8+i*25-(int)((now/95u)%25u),ht=23+(int)(h%33u),w=17+(int)((h>>8)%6u),y=base-ht;
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
/* Small moving props remain inside the 240x240 screen. */
static void flying_car(int x,int y,uint16_t ink){
 rect(x-7,y,15,3,ink);rect(x-3,y-2,7,2,WHITE);
 pixel(x-6,y+3,WHITE);pixel(x+6,y+3,WHITE);
}
static void creature(int x,int y,uint32_t now,uint16_t ink){
 orb(x,y-4,4,ink);rect(x-3,y,7,6,ink);
 pixel(x-2,y-5,BLACK);pixel(x+2,y-5,BLACK);
 int step=(int)((now/140u)%2u);
 line(x-2,y+5,x-4+step*3,y+8,ink);line(x+2,y+5,x+4-step*3,y+8,ink);
}
static void encounter_draw(uint32_t now,int ship,int sy,uint16_t ink){
 if(!event_kind)return;
 uint32_t age=now-event_at;if(age>EVENT_MS)age=EVENT_MS;
 unsigned phase=age<APPROACH_MS?1u:age<LIFT_MS?2u:age<FINISH_MS?4u:8u;
 event_phase_mask|=phase;
 int x=event_x,base=event_kind==1?117:event_kind==2?116:119;
 int lift=age<APPROACH_MS?0:age>=LIFT_MS?1000:(int)((age-APPROACH_MS)*1000u/(LIFT_MS-APPROACH_MS));
 int y=base+(sy+8-base)*lift/1000;
 int lift_x=x;
#ifdef PLAT_HOST
 rendered_events|=1u<<(event_kind-1);
 if((event_phase_mask&15u)==15u)completed_events|=1u<<(event_kind-1);
 if(age>=LIFT_MS&&age<FINISH_MS)abducted_worlds|=1u<<(event_kind-1);
#endif
 switch(event_kind){
 case 1: /* abduct a vehicle */
  if(age<LIFT_MS)flying_car(lift_x,y,WHITE);
  else if(age<FINISH_MS)orb(ship,sy+10,4,WHITE);
  break;
 case 2: /* abduct an alien from the forest */
  if(age<LIFT_MS)creature(lift_x,y-6,now,ink);
  else if(age<FINISH_MS)orb(ship,sy+10,4,WHITE);
  break;
 case 3: /* meteor shower */
  for(int i=0;i<4;i++){int mx=12+(int)((age/9u+i*51u)%205u),my=50+(int)((age/32u+i*9u)%35u);
   line(mx,my,mx-8,my+6,WHITE);line(mx-8,my+6,mx-16,my+12,ink);}
  break;
 case 4: /* levitating crystal */
  if(age<LIFT_MS){line(lift_x,y-7,lift_x-5,y,WHITE);line(lift_x-5,y,lift_x,y+7,ink);
   line(lift_x,y+7,lift_x+5,y,ink);line(lift_x+5,y,lift_x,y-7,WHITE);}
  else if(age<FINISH_MS)orb(ship,sy+10,4,WHITE);break;
 case 5: /* lightning over volcano */
  if((age/130u)%3u==0){int q=x>120?x-30:x+30;line(q,49,q-10,66,WHITE);line(q-10,66,q+5,76,ink);line(q+5,76,q-4,104,WHITE);}
  for(int i=0;i<5;i++)pixel(x-12+i*6,105-(int)((age/30u+i*5u)%25u),ink);
  break;
 case 6: /* passing spacecraft and signal pulse */
  flying_car(15+(int)(age*205u/EVENT_MS),87,WHITE);
  for(int i=0;i<3;i++){int r=3+(int)((age/65u+i*5u)%15u);
   line(x-r,91-r,x+r,91-r,ink);line(x-r,91+r,x+r,91+r,ink);}
  break;
 case 7: /* opening portal */
  {int r=age<1100u?4+(int)(age/90u):age<3900u?16:4+(int)((EVENT_MS-age)*12u/(EVENT_MS-3900u));
   if(age<FINISH_MS){orb(x,102,r,ink);orb(x,102,r>3?r-3:1,BLACK);}
   if(age>1000u)creature(bound(x+(int)((age-1000u)/70u),15,222),bound(103+(int)((age-1000u)/180u),103,120),now,WHITE);}
  break;
 case 8: /* ice comet and visitor */
  {int mx=210-(int)(age*180u/EVENT_MS),my=56+(int)(age*29u/EVENT_MS);
   line(mx,my,mx+18,my-5,ink);orb(mx,my,3,WHITE);}
  break;
 }
 /* All worlds share a full capture sequence alongside their unique effect. */
 if(event_kind!=1&&event_kind!=2&&event_kind!=4){
  if(age<LIFT_MS){
   if(event_kind==3||event_kind==6)flying_car(x,y,WHITE);
   else if(event_kind==5){orb(x,y,5,RGB(255,160,60));pixel(x,y-2,WHITE);}
   else creature(x,y-6,now,ink);
  }else if(age<FINISH_MS)orb(ship,sy+10,4,WHITE);
 }
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
 int ship=ship_x(now);
 int bob=(int)((now/160u)%8u),sy=59+(bob>4?8-bob:bob);
 /* Ambient traffic and ground life animate even when no keys are held. */
 if(scene==0||scene==2||scene==5){
  int car=14+(int)((now/24u)%210u);flying_car(car,99-(int)((now/400u)%3u),ink);
  flying_car(224-(int)((now/33u)%210u),110,WHITE);
 }
 if(scene==1||scene==6||scene==7){int cx=14+(int)((now/70u)%210u);creature(cx,119,now,ink);}
 int abduct=event_kind!=0;
 uint32_t age=now-event_at;
 if(abduct){
  if(age<APPROACH_MS)ship=(event_origin*(int)(APPROACH_MS-age)+event_x*(int)age)/(int)APPROACH_MS;
  else if(age<FINISH_MS)ship=event_x;
  else {int u=bound((int)(age-FINISH_MS),0,(int)(EVENT_MS-FINISH_MS));ship=event_x+(ship-event_x)*u/(int)(EVENT_MS-FINISH_MS);}
 }
 int boosted=backing_style&&backing_hits&&now-backing_flash<350u;
 saucer(ship,sy,now,ink,abduct?(age>=APPROACH_MS&&age<LIFT_MS?(boosted?46:30):0):((latched||pulse>3)?pulse+1:0),119);
 if(abduct&&age>=APPROACH_MS&&age<LIFT_MS)for(int i=0;i<7;i++){int yy=sy+14+(int)((age/25u+i*11u)%36u);pixel(ship+(i-3)*3,yy,WHITE);}
 encounter_draw(now,ship,sy,ink);
 for(int i=0;i<macros[3]/8;i++){unsigned q=world_hash((unsigned)i+scene*91u);
  int x=7+(int)((q+(now/35u)*2u)%222u),y=50+(int)(((q>>8)+now/13u)%38u);line(x,y,x-2,y+4,RGB(86,120,155));}
 /* Original-style tape tearing during cuts, using the existing framebuffer. */
 uint32_t since=now-cut_at;
 if(!event_kind&&cut_count&&since<360u){
  for(int i=0;i<5;i++){unsigned h=world_hash(film_seed+since/40u*37u+(unsigned)i);int y=46+(int)(h%78u),d=(int)((h>>8)%29u)-14;
   for(int j=0;j<5;j++)lw_neon_shift_row(y+j,4,232,d);
  }
  rect(4,48+(int)(film_seed%76u),232,1,RGB(255,70,180));rect(4,48+(int)((film_seed>>9)%76u),232,1,ink);
 }
 if(!event_kind&&since<3000u){char tag[24];unsigned n=since/80u,k=0;while(k<n&&k<sizeof(tag)-1&&worlds[scene][k]){tag[k]=worlds[scene][k];k++;}tag[k]=0;
  rect(6,48,140,13,BLACK);text(9,48,tag,ink,FONT_SMALL);}
 if(!event_kind&&logs_on&&since>=3000u&&since<6500u){
  static const char *const log[NWORLD]={"CITY SIGNAL LOCKED","LIFEFORM DETECTED","ORBITAL LINK ONLINE","CRYSTAL RESONANCE","THERMAL ANOMALY","SIGNAL ACQUIRED","PORTAL ACTIVITY","ICE FIELD AHEAD"};
  char msg[24];unsigned n=(since-3000u)/65u,k=0;while(k<n&&k<sizeof(msg)-1&&log[scene][k]){msg[k]=log[scene][k];k++;}msg[k]=0;
  rect(6,48,165,13,BLACK);text(9,48,msg,ink,FONT_SMALL);
 }

 rect(0,45,3,93,BLACK);rect(237,45,3,93,BLACK);
 frame(3,45,234,93,ink);
#ifdef PLAT_HOST
 rendered_worlds|=1u<<scene;
#endif
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
static void backing_guide(uint32_t now,uint16_t neon,uint16_t pink,int y){
 char chord[24],score[12];int n=0;
 const char *p=pitch_names[backing_root];while(*p)chord[n++]=*p++;
 if(backing_minor)chord[n++]='m';
 chord[n++]=':';chord[n++]=' ';
 unsigned mask=backing_mask();
 for(int pc=0;pc<12;pc++)if(mask&PC(pc)){p=pitch_names[pc];while(*p)chord[n++]=*p++;chord[n++]=' ';}
 chord[n]=0;
 text(5,y,chord,neon,FONT_SMALL);
 unsigned period=backing_period(),remain=(int32_t)(backing_due-now)>0?backing_due-now:0;
 if(remain>period)remain=period;
 rect(183,y+4,50,4,RGB(40,48,64));rect(183,y+4,50*(period-remain)/period,4,neon);
 if(backing_hits&&now-backing_flash<350u)text(5,y+15,"IN HARMONY!",neon,FONT_SMALL);
 else text(5,y+15,"FILL IN LIT NOTES",WHITE,FONT_SMALL);
 format_int(score,(int)backing_hits);text(205,y+15,score,pink,FONT_SMALL);
}
 /* Instruction panel replaces only the synth controls during a lesson. */
static void lesson_draw(uint32_t now,uint16_t neon,uint16_t pink){
 char num[12],notes[24];int n=0;
 unsigned target=lesson_target();
 for(int pc=0;pc<12;pc++)if(target&PC(pc)){
  if(n)notes[n++]=' ';
  const char *p=pitch_names[pc];while(*p&&n<22)notes[n++]=*p++;
 }
 notes[n]=0;
 text(5,156,lesson_names[scene],neon,FONT_SMALL);
 if(lesson_wait==2){text(5,175,"ALL WORLDS MASTERED",WHITE,FONT_SMALL);text(5,194,"ENV: FREE PLAY",neon,FONT_SMALL);}
 else if(lesson_wait){text(5,175,"MISSION COMPLETE!",WHITE,FONT_SMALL);if(backing_style)backing_guide(now,neon,pink,190);else text(5,194,"PLAY ALONG FREELY",pink,FONT_SMALL); }
 else {
  text(5,175,(scene==3||scene==6)?"HOLD CHORD:":"PLAY:",WHITE,FONT_SMALL);
  text(5,190,notes,neon,FONT_BIG);
  if(scene==4){
   int age=lesson_beat?(int)(now-lesson_beat):0;
   int phase=age<0?0:age>600?600:age;
   rect(5,217,110,4,RGB(40,48,64));rect(5,217,110*phase/600,4,neon);
   text(120,210,lesson_step?"ON THE BEAT":"START WITH C",WHITE,FONT_SMALL);
  }else if(now-lesson_feedback_at<700u&&lesson_feedback)
   text(5,214,lesson_feedback>0?"CORRECT!":"TRY LIT KEYS",lesson_feedback>0?neon:pink,FONT_SMALL);
  else text(5,214,"FOLLOW LIT KEYS",WHITE,FONT_SMALL);
 }
 format_int(num,(int)lesson_score);text(175,156,num,pink,FONT_SMALL);
 int len=lesson_lengths[scene];for(int i=0;i<len;i++)rect(5+i*28,149,25,3,i<lesson_step?neon:RGB(40,48,64));
 text(5,229,"ENV EXIT",WHITE,FONT_SMALL);text(95,229,"LFO TRACK",WHITE,FONT_SMALL);
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
 static const char *const world_labels[4]={"K1 AMBI","K2 EVENT","K3 LOGS","K4 WORLD"};
 uint32_t now=millis();
 text(8,28,world_page?"WORLD SETTINGS":backing_style?backing_names[backing_style]:lesson?"ALIEN MUSIC QUEST":names[preset],pink,FONT_SMALL);
 alien_draw(now);
 text(8,140,worlds[scene],neon,FONT_SMALL);
 const char *status=auto_cut?"AUTO TOUR":"SCENE HOLD";
 if(event_kind){uint32_t age=now-event_at;int abduct=event_kind!=0;
  status=age<APPROACH_MS?(abduct?"TARGET LOCK":"CONTACT"):age<LIFT_MS?(abduct?"BEAM ACTIVE":"ENCOUNTER"):age<FINISH_MS?(abduct?"CAPTURED":"RESOLVING"):"COMPLETE";}
 text(240-text_width(status,FONT_SMALL)-5,140,status,event_kind?pink:WHITE,FONT_SMALL);
 if(lesson){lesson_draw(now,neon,pink);return;}
 if(backing_style&&!world_page){
  text(5,156,"PLAY ALONG / LIT KEYS",neon,FONT_SMALL);
  backing_guide(now,neon,pink,174);
  text(5,207,"LFO TRACK / ENV QUEST",WHITE,FONT_SMALL);
  text(5,221,"FX WORLD / HOME NEXT",WHITE,FONT_SMALL);return;
 }
 for(int i=0;i<4;i++){
  int x=5+i*59;char val[12];int v=macros[i];
  if(world_page){
   if(i==0){v=world_level*127/100;format_int(val,world_level);}
   else if(i==1||i==2){int on=i==1?idle_events:logs_on;v=on?127:0;strcpy(val,on?"ON":"OFF");}
   else {v=scene*127/(NWORLD-1);format_int(val,scene+1);}
  }else format_int(val,v);
  text(x,156,world_page?world_labels[i]:labels[i],WHITE,FONT_SMALL);
  rect(x,170,52,5,RGB(40,48,64));rect(x,170,52*v/127,5,neon);
  text(x,177,val,neon,FONT_SMALL);
 }
 text(5,194,drone?"PLAY HOLD:ON":"PLAY HOLD:OFF",drone?neon:WHITE,FONT_SMALL);
 text(125,194,epic?"ARP CHORD:ON":"ARP CHORD:OFF",epic?neon:WHITE,FONT_SMALL);
 text(5,207,memory?"REC FREEZE:ON":"REC FREEZE:OFF",memory?neon:WHITE,FONT_SMALL);
 text(125,207,"PRESET: SOUND",WHITE,FONT_SMALL);
 if(world_page){text(5,221,"SEL NEXT / GLO AUTO / EDIT EVENT",WHITE,FONT_SMALL);}

 else text(5,221,"ENV QUEST / LFO MUSIC / FX",WHITE,FONT_SMALL);
}
#ifdef PLAT_HOST
int lw_offworld_expect(const char *name,int *v){
 if(!strcmp(name,"offworld_active"))*v=mode==2;
 else if(!strcmp(name,"offworld_notes"))*v=mode==2?(int)eng_notes():0;
 else if(!strcmp(name,"offworld_error"))*v=init_error;
 else if(!strcmp(name,"offworld_scene"))*v=scene;
 else if(!strcmp(name,"offworld_preset"))*v=preset;
 else if(!strcmp(name,"offworld_drone"))*v=drone;
 else if(!strcmp(name,"offworld_event"))*v=event_kind;
 else if(!strcmp(name,"offworld_events"))*v=(int)event_count;
 else if(!strcmp(name,"offworld_worldpage"))*v=world_page;
 else if(!strcmp(name,"offworld_cuts"))*v=(int)cut_count;
 else if(!strcmp(name,"offworld_auto"))*v=auto_cut;
 else if(!strcmp(name,"offworld_idle_events"))*v=idle_events;
 else if(!strcmp(name,"offworld_rendered_worlds"))*v=(int)rendered_worlds;
 else if(!strcmp(name,"offworld_rendered_events"))*v=(int)rendered_events;
 else if(!strcmp(name,"offworld_completed_events"))*v=(int)completed_events;
 else if(!strcmp(name,"offworld_event_phase"))*v=(int)event_phase_mask;
 else if(!strcmp(name,"offworld_lesson"))*v=lesson;
 else if(!strcmp(name,"offworld_lesson_step"))*v=lesson_step;
 else if(!strcmp(name,"offworld_lesson_done"))*v=(int)lesson_done;
 else if(!strcmp(name,"offworld_lesson_score"))*v=(int)lesson_score;
 else if(!strcmp(name,"offworld_lesson_wait"))*v=lesson_wait;
 else if(!strcmp(name,"offworld_backing"))*v=backing_style;
 else if(!strcmp(name,"offworld_backing_notes"))*v=(int)backing_notes;
 else if(!strcmp(name,"offworld_backing_hits"))*v=(int)backing_hits;
 else if(!strcmp(name,"offworld_abductions"))*v=(int)abducted_worlds;
 else return 0;return 1;
}
#endif
