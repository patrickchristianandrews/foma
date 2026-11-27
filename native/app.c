/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Base unity entry points. No platform registers or SDK internals. */
extern void lw_native_init(void);
extern void lw_native_step(void);
extern void lw_native_render(int32_t *,uint32_t,uint32_t);
extern void lw_native_panic(void);
extern const uint16_t *lw_native_fb(void);
extern int lw_native_dirty(int *);
void app_init(void){lw_native_init();}
void app_ui_init(void){plat_wifi_on(1);}
void app_render(int32_t *lr,uint32_t n,uint32_t master_q12){lw_native_render(lr,n,master_q12);}
void app_panic(void){lw_native_panic();}
void app_loop(void){lw_native_step();}
void app_input_only(void){}
void app_frame(void){
 int end,start=lw_native_dirty(&end);
 const uint16_t *fb=lw_native_fb();
 if(start<0)start=0;if(end>240)end=240;
 while(start<end){
  int h=end-start;if(h>48)h=48;
  lcd_sync();
  lcd_blit(0,(uint32_t)start,240,(uint32_t)h,fb+start*240);
  start+=h;
 }
 lcd_sync();
}
void app_say(const char *a,const char *b){(void)a;(void)b;}
