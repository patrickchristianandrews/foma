/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Base unity entry points. No platform registers or SDK internals. */
extern void lw_native_init(void);
extern void lw_native_step(void);
extern void lw_native_render(int32_t *,uint32_t,uint32_t);
extern void lw_native_panic(void);
extern void lw_native_resume(void);
extern int lw_graphics_init(void);
extern void lw_native_copy_rows(uint16_t *,int,int);
static int lw_ready;
extern int lw_native_dirty(int *);
void app_init(void){lw_ready=lw_graphics_init();if(lw_ready)lw_native_init();}
void app_ui_init(void){
 if(!lw_ready){draw_text_box(0,96,240,&FONT_M,"MEMORY ERROR",C_WHITE,1);return;}
 lw_native_resume();
}
void app_render(int32_t *lr,uint32_t n,uint32_t master_q12){if(lw_ready)lw_native_render(lr,n,master_q12);else for(uint32_t i=0;i<n*2u;i++)lr[i]=0;}
void app_panic(void){lw_native_panic();}
void app_loop(void){if(lw_ready)lw_native_step();}
void app_input_only(void){}
void app_frame(void){
 if(!lw_ready)return;
 int end,start=lw_native_dirty(&end);
 if(start<0)start=0;if(end>240)end=240;
 while(start<end){
  int h=end-start;if(h>48)h=48;
  lcd_sync();
  lw_native_copy_rows(cv_px,start,h);
  lcd_blit(0,(uint32_t)start,240,(uint32_t)h,cv_px);
  start+=h;
 }
 lcd_sync();
}
void app_say(const char *a,const char *b){(void)a;(void)b;}

#ifdef PLAT_HOST
extern int lw_offworld_expect(const char *,int *);
extern int lw_wifi_expect(const char *,int *);
extern int lw_radio_expect(const char *,int *);
extern int lw_loop_expect(const char *,int *);
extern uint32_t lw_native_key_lights(void);
static int app_expect(const char *name,int *v){
 if(!strcmp(name,"native_key_lights")){*v=(int)lw_native_key_lights();return 1;}
 if(!strcmp(name,"wifi_state")){*v=plat_wifi_state();return 1;}
 return lw_offworld_expect(name,v)||lw_wifi_expect(name,v)||lw_radio_expect(name,v)||lw_loop_expect(name,v);
}
#endif
