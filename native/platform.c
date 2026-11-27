/* SPDX-License-Identifier: GPL-3.0-only */
#define FOMA_BLUETOOTH 0
#define FOMA_USB_AUDIO 0
#define FOMA_TRS_MIDI 0
#define FOMA_MIDI_CLOCK 0
#include "../platform/fm1.h"
#include "app.h"
static const uint8_t button_map[BTN_COUNT]={B_OCTDN,B_OCTUP,B_FX,B_SEL,B_ENV,B_LFO,B_EDIT,B_GLO,B_HOME,B_SAVE,B_ARP,B_SEQ,B_PLAY,B_REC};
static const uint8_t enc_map[ENC_COUNT]={EN_SELECT,EN_ALGO,EN_K1,EN_K2,EN_K3,EN_K4,EN_PRESET};
static uint32_t held_keys,held_btn,down_keys,up_keys,down_btn,up_btn,lights_btn,lights_key;
static int32_t delta[ENC_COUNT];
static int glow;
static volatile int muted;
uint32_t millis(void){return plat_ms();}
uint32_t micros(void){return plat_ticks24()/PLAT_TICKS_PER_US;}
int battery_bars(void){int v=plat_battery();return v>3?3:v;}
uint32_t keys(void){return held_keys;}
uint32_t buttons(void){return held_btn;}
static int bit(uint32_t v,int n,int count){return (unsigned)n<(unsigned)count && ((v>>n)&1u);}
int key_held(int k){return bit(held_keys,k,KEY_COUNT);}
int key_pressed(int k){return bit(down_keys,k,KEY_COUNT);}
int key_released(int k){return bit(up_keys,k,KEY_COUNT);}
int button_held(int b){return bit(held_btn,b,BTN_COUNT);}
int button_pressed(int b){return bit(down_btn,b,BTN_COUNT);}
int button_released(int b){return bit(up_btn,b,BTN_COUNT);}
int32_t encoder(int e){return (unsigned)e<ENC_COUNT?delta[e]:0;}
uint32_t master_knob(void){return plat_master()*1023u/4096u;}
void led_key(int k,int on){if((unsigned)k<KEY_COUNT){uint32_t m=1u<<k;lights_key=on?lights_key|m:lights_key&~m;}}
void led_button(int b,int on){if((unsigned)b<BTN_COUNT){uint32_t m=1u<<button_map[b];lights_btn=on?lights_btn|m:lights_btn&~m;}}
void led_backlight(int level){glow=level<0?0:level>2?2:level;}
int midi_read(uint8_t *status,uint8_t *d1,uint8_t *d2){
 uint32_t pkt;int port;
 while(plat_midi_in(&pkt,&port)){
  uint8_t st=(uint8_t)(pkt>>8);
  if(st<0x80u||st==0xf0u||st==0xf7u)continue;
  *status=st;*d1=(uint8_t)(pkt>>16);*d2=(uint8_t)(pkt>>24);
  return port==PORT_USB?MIDI_USB:port==PORT_TRS?MIDI_TRS:MIDI_BLE;
 }
 return 0;
}
void midi_send(uint8_t st,uint8_t d1,uint8_t d2){
 unsigned cin=st>=0xf8?15u:st>>4;
 if(st<0x80u||(st>=0xf0u&&st<0xf8u))return;
 plat_midi_out(cin|((uint32_t)st<<8)|((uint32_t)d1<<16)|((uint32_t)d2<<24));
}
void midi_frame(uint32_t at){(void)at;}
int settings_load(void *p,uint32_t n){
 if(n>PLAT_STORE_MAX||plat_store_size(OBJ_LIQUID)!=(int)n)return -1;
 return plat_store_load(OBJ_LIQUID,p,n)==(int)n?0:-1;
}
int settings_save(const void *p,uint32_t n){return n>PLAT_STORE_MAX?-1:plat_store_save(OBJ_LIQUID,p,n);}
void print(const char *s){plat_log(s);}
extern void lw_radio_service(void);
extern void lw_weather_service(void);
extern void lw_wifi_autostart(void);
extern int liquid_boot_complete(void);
extern void lw_radio_mix(int32_t *,uint32_t,uint32_t);
void lw_native_init(void){setup();}
void lw_native_step(void){
 uint32_t raw=plat_buttons(),b=0,k=plat_keys();
 for(int i=0;i<BTN_COUNT;i++)if(raw&(1u<<button_map[i]))b|=1u<<i;
 down_btn=b&~held_btn;up_btn=held_btn&~b;held_btn=b;
 down_keys=k&~held_keys;up_keys=held_keys&~k;held_keys=k;
 for(int i=0;i<ENC_COUNT;i++)delta[i]=plat_enc(enc_map[i]);
 loop();
 if(liquid_boot_complete())lw_wifi_autostart();
 lw_radio_service();
 lw_weather_service();
 plat_lights(lights_btn,lights_key,0,0,glow);
}
void lw_native_render(int32_t *lr,uint32_t n,uint32_t gain){
 for(uint32_t i=0;i<n*2u;i++)lr[i]=0;
 if(muted)return;
 audio(lr,n);
 if(gain>4096u)gain=4096u;
 for(uint32_t i=0;i<n*2u;i++){
  int32_t s=lr[i];if(s>32767)s=32767;if(s<-32768)s=-32768;
  lr[i]=(s*(int32_t)gain/4096)*256;
 }
 lw_radio_mix(lr,n,gain);
}
void lw_native_panic(void){muted=1;}
void lw_native_resume(void){muted=0;}
