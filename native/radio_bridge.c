/* SPDX-License-Identifier: GPL-3.0-only */
/* Uses the pinned upstream FM1 Radio decoder, not a simulated audio stream. */
#include "plat.h"
#include "../platform/fm1.h"
#include <string.h>
#define wifi_st plat_wifi_state()
#define WS_CONNECTED WIFI_CONNECTED
#define shim_log plat_log
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
static void w_cpy(char *d,const char *s,unsigned n){unsigned i=0;if(!n)return;while(i+1<n&&s&&s[i]){d[i]=s[i];i++;}d[i]=0;}
#include "reference/radio_core.c"
typedef struct {char id[12],title[32];} lw_station;
extern lw_station *rgn_list;
extern volatile uint32_t rgn_n;
extern volatile int rgn_state;
extern char rgn_why[32];
extern int rgn_start(const char *,const char *);
#ifdef PLAT_HOST
/* Host is intentionally offline: it verifies compile and UI, not real network audio. */
volatile int radio_net_state,radio_net_aac;
volatile uint32_t radio_rd,radio_wr;
char radio_net_why[32],radio_net_name[48];
int radio_net_start(const char *u,uint8_t *p,uint32_t n){(void)u;(void)p;(void)n;return -1;}
void radio_net_stop(void){}
int radio_net_running(void){return 0;}
lw_station *rgn_list;
volatile uint32_t rgn_n;
volatile int rgn_state;
char rgn_why[32];
int rgn_start(const char *a,const char *b){(void)a;(void)b;return -1;}
#endif
static const struct {const char *city,*country;} cities[]={
 {"Richmond","United States"},{"Virginia Beach","United States"},{"New York","United States"},
 {"Los Angeles","United States"},{"London","United Kingdom"},{"Paris","France"},{"Amsterdam","Netherlands"},
 {"Berlin","Germany"},{"Tokyo","Japan"},{"Sydney","Australia"},{"Toronto","Canada"},
 {"Mexico City","Mexico"},{"Cape Town","South Africa"},{"Sao Paulo","Brazil"}};
static unsigned city,station;
static int browsing,requested,lookup,active,volume=65;
static char url[96],title[32]="Groove Salad",fav_id[12],fav_title[32];
static const char fallback[]="http://ice4.somafm.com/groovesalad-128-mp3";
static uint8_t loaded;
static volatile int mix_on;
static void fav_load(void){
 if(loaded)return;loaded=1;
 struct {uint32_t magic;char id[12],title[32];} f;
 if(plat_store_load(OBJ_FAV,&f,sizeof f)==sizeof f&&f.magic==0x4c575246u){
  f.id[11]=0;f.title[31]=0;w_cpy(fav_id,f.id,sizeof fav_id);w_cpy(fav_title,f.title,sizeof fav_title);
 }
}
static void fav_save(void){
 if(!rgn_list||station>=rgn_n||rgn_state!=2)return;
 struct {uint32_t magic;char id[12],title[32];} f={0x4c575246u,{0},{0}};
 w_cpy(f.id,rgn_list[station].id,sizeof f.id);w_cpy(f.title,rgn_list[station].title,sizeof f.title);
 if(plat_store_save(OBJ_FAV,&f,sizeof f)==0){w_cpy(fav_id,f.id,sizeof fav_id);w_cpy(fav_title,f.title,sizeof fav_title);}
}
static void station_url(const char *id,const char *name){
 const char *p="https://radio.garden/api/ara/content/listen/",*suffix="/channel.mp3";unsigned n=0;
 while(*p)url[n++]=*p++;
 for(unsigned i=0;id[i]&&i<11;i++){char c=id[i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return;url[n++]=c;}
 while(*suffix)url[n++]=*suffix++;url[n]=0;
 w_cpy(title,name,sizeof title);radio_url=url;requested=1;
}
void lw_radio_service(void){
 if(!active)return;
 if(plat_wifi_state()!=WIFI_CONNECTED){mix_on=0;if(radio_st!=RS_OFF)radio_stop();return;}
 if(lookup&&!radio_net_running()&&rgn_state!=1){
  lookup=0;rgn_start(cities[city].city,cities[city].country);
 }
 if(requested&&!lookup&&rgn_state!=1&&!radio_net_running()){
  requested=0;radio_play(0);mix_on=1;
 }
 radio_poll();
}
void lw_radio_leave(void){active=0;requested=lookup=0;mix_on=0;radio_stop();}
int lw_radio_ui(void){
 fav_load();active=1;int d=encoder(ENC_SELECT),v=encoder(ENC_KNOB1);
 if(v){volume+=v*3;if(volume<0)volume=0;if(volume>100)volume=100;}
 if(button_pressed(BTN_SEL)){browsing=!browsing;}
 if(d){if(browsing){int n=(int)(sizeof cities/sizeof cities[0]);city=(city+d+n)%n;station=0;}else if(rgn_state==2&&rgn_n){int n=(int)rgn_n;station=(station+d+n)%n;}}
 if(button_pressed(BTN_EDIT)){mix_on=0;radio_stop();requested=0;lookup=1;browsing=0;station=0;}
 if(button_pressed(BTN_PLAY)){
  if(radio_st!=RS_OFF&&radio_st!=RS_ERROR){mix_on=0;radio_stop();requested=0;}
  else if(rgn_state==2&&rgn_list&&station<rgn_n)station_url(rgn_list[station].id,rgn_list[station].title);
  else {radio_url=fallback;w_cpy(title,"Groove Salad",sizeof title);requested=1;}
 }
 if(button_pressed(BTN_SAVE)&&radio_st==RS_OFF)fav_save();
 if(button_pressed(BTN_SEQ)&&fav_id[0]){mix_on=0;radio_stop();station_url(fav_id,fav_title);}
 return 1;
}
static void label_at(int y,const char *s,uint16_t c){char b[28];w_cpy(b,s,sizeof b);text(8,y,b,c,FONT_SMALL);}
void lw_radio_draw(void){
 uint16_t cyan=RGB(103,212,250);
 rect(0,0,240,240,BLACK);
 label_at(5,"4 / INTERNET RADIO",cyan);
 label_at(32,cities[city].city,WHITE);
 label_at(50,cities[city].country,WHITE);
 label_at(72,browsing?"SELECT CITY / EDIT SEARCH":rgn_state==1?"SEARCHING STATIONS":rgn_state==3?rgn_why:"SELECT STATION",cyan);
 if(rgn_state==2&&rgn_list&&station<rgn_n)label_at(91,rgn_list[station].title,WHITE);
 label_at(115,title,WHITE);
 static const char *const states[]={"OFF","CONNECTING","BUFFERING","PLAYING","ERROR"};
 label_at(135,radio_st==RS_ERROR?radio_why:states[radio_st<5?radio_st:0],cyan);
 char vol[12];format_int(vol,volume);label_at(155,"K1 RADIO VOLUME",WHITE);text(184,155,vol,WHITE,FONT_SMALL);
 label_at(175,"SEL CITY / EDIT SEARCH",WHITE);
 label_at(193,"PLAY / SAVE FAV / SEQ FAV",WHITE);
 label_at(216,"ARP WIFI / HOME NEXT",cyan);
}
void lw_radio_mix(int32_t *lr,uint32_t n,uint32_t gain){
 if(!mix_on||radio_st!=RS_PLAYING||!pcm_buf)return;
 uint32_t r=pcm_r,w=pcm_w,step=radio_step;
 int g=volume;
 for(uint32_t i=0;i<n&&r!=w;i++){
  const int16_t *a=&pcm_buf[(r%PCM_FRAMES)*2u],*b=r+1u!=w?&pcm_buf[((r+1u)%PCM_FRAMES)*2u]:a;
  for(unsigned ch=0;ch<2;ch++){
   int32_t sample=(int32_t)a[ch]+((int32_t)(b[ch]-a[ch])*(int32_t)(radio_frac>>1)>>15);
   int32_t s=lr[i*2u+ch]+(sample*g/100*(int32_t)gain/4096)*256;
   lr[i*2u+ch]=s>8388607?8388607:s<-8388608?-8388608:s;
  }
  radio_frac+=step;uint32_t adv=radio_frac>>16;radio_frac&=65535u;r+=adv<w-r?adv:w-r;
 }
 pcm_r=r;
}
