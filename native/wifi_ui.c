/* SPDX-License-Identifier: GPL-3.0-only */
/* Native Wi-Fi setup screen. No credentials are logged or displayed. */
#include "plat.h"
#include "../platform/fm1.h"
static int pick,editing,len,choice;
static int wifi_boot_checked;
static const char *message="";
extern int lw_radio_can_power_off(void);
static char ssid[33],password[65];
static const char alphabet[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !@#$%^&*()-_=+[]{};:'\",.<>/?\\|~`";
static const char *const states[]={"OFF","STARTING","READY","JOINING","GETTING IP","CONNECTED","FAILED"};
static void wipe_password(void){volatile char *p=password;for(unsigned i=0;i<sizeof(password);i++)p[i]=0;len=0;}
static void join_selected(int force_password){
 int secured=0;
 if(!plat_wifi_found(pick,ssid,0,&secured)){message="SCAN FIRST";return;}
 if(!secured||(!force_password&&plat_wifi_kept(ssid))){message=plat_wifi_join(ssid,0)?"JOIN NOT READY":"";return;}
 wipe_password();editing=1;choice=0;
}
void lw_wifi_autostart(void){
 if(wifi_boot_checked)return;
 wifi_boot_checked=1;
 if(plat_wifi_saved(0,0)&&plat_wifi_state()==WIFI_OFF)plat_wifi_on(1);
}
void lw_wifi_leave(void){editing=0;wipe_password();}
int lw_wifi_step(void){
 int d=encoder(ENC_SELECT);
 int n=(int)sizeof(alphabet)-1;
 if(editing){
  int group=encoder(ENC_KNOB1);
  if(group){int g=choice<26?0:choice<52?1:choice<62?2:3;g=(g+group)%4;if(g<0)g+=4;choice=g==0?0:g==1?26:g==2?52:62;}
  if(d){choice=(choice+d)%n;if(choice<0)choice+=n;}
  if(button_pressed(BTN_SEL)&&len<63){password[len++]=alphabet[choice];password[len]=0;}
  if(button_pressed(BTN_EDIT)&&len)password[--len]=0;
  if(button_pressed(BTN_PLAY)){
   if(len<8)message="MINIMUM 8 CHARACTERS";
   else if(plat_wifi_join(ssid,password))message="JOIN NOT READY";
   else {lw_wifi_leave();message="";}
  }
  if(button_pressed(BTN_GLO)){lw_wifi_leave();}
  return d||group||button_pressed(BTN_SEL)||button_pressed(BTN_EDIT)||button_pressed(BTN_PLAY)||button_pressed(BTN_GLO);
 }
 if(d){pick+=d;if(pick<0)pick=0;if(pick>63)pick=63;while(pick>0&&!plat_wifi_found(pick,0,0,0))pick--;}
 if(button_pressed(BTN_GLO)){
  if(plat_wifi_state()==WIFI_OFF){plat_wifi_on(1);message="";}
  else if(lw_radio_can_power_off()){plat_wifi_on(0);message="";}
  else message="WAIT FOR RADIO TO STOP";
 }
 if(button_pressed(BTN_SEL)){pick=0;message=plat_wifi_scan()?"TURN WI-FI ON FIRST":"";}
 if(button_pressed(BTN_SAVE))join_selected(0);
 if(button_pressed(BTN_EDIT))join_selected(1);
 return d||button_pressed(BTN_GLO)||button_pressed(BTN_SEL)||button_pressed(BTN_SAVE)||button_pressed(BTN_EDIT);
}
static void clipped(int x,int y,const char *s,uint16_t color){
 char t[28];int n=0;
 while(n<27&&s&&s[n]){t[n]=s[n];n++;}t[n]=0;text(x,y,t,color,FONT_SMALL);
}
void lw_wifi_draw(void){
 uint16_t cyan=RGB(103,212,250),dim=RGB(146,146,160);
 int st=plat_wifi_state();
 rect(0,0,240,240,BLACK);
 text(8,4,"4 / WI-FI SETUP",cyan,FONT_SMALL);
 if(editing){
  clipped(8,30,ssid,WHITE);text(8,54,"ENTER NETWORK PASSWORD",cyan,FONT_SMALL);
  char mask[25];int i=0;while(i<len&&i<24)mask[i++]='*';mask[i]=0;
  text(8,80,mask,WHITE,FONT_SMALL);
  char ch[2]={alphabet[choice],0},count[12];format_int(count,len);
  text(8,104,"CHAR:",dim,FONT_SMALL);text(75,104,ch,cyan,FONT_SMALL);
  text(150,104,count,WHITE,FONT_SMALL);
  text(8,128,choice<26?"LOWERCASE a-z":choice<52?"UPPERCASE A-Z":choice<62?"NUMBERS 0-9":"SYMBOLS",cyan,FONT_SMALL);
  text(8,152,"K1 GROUP / SELECT CHAR",WHITE,FONT_SMALL);
  text(8,172,"SEL: ADD / EDIT: DELETE",WHITE,FONT_SMALL);
  text(8,192,"PLAY: JOIN / GLO: CANCEL",WHITE,FONT_SMALL);
  clipped(8,220,message,cyan);
  return;
 }
 text(8,30,st>=0&&st<=WIFI_FAILED?states[st]:"UNKNOWN",cyan,FONT_SMALL);
 clipped(8,50,st==WIFI_FAILED?plat_wifi_why():plat_wifi_ssid(),WHITE);
 if(*message)clipped(8,70,message,cyan);
 else if(st==WIFI_CONNECTED){
  char ip[16],*p=ip;uint32_t v=plat_wifi_ip();
  for(int i=0;i<4;i++){char b[12];format_int(b,(v>>(8*i))&255u);for(int j=0;b[j];j++)*p++=b[j];if(i<3)*p++='.';}*p=0;
  text(8,70,plat_wifi_kept(plat_wifi_ssid())?"SAVED / AUTO CONNECT":ip,dim,FONT_SMALL);
 }
 int first=pick>4?pick-4:0;
 for(int i=0;i<5;i++){
  char name[33];int sec=0,rssi=0,y=91+i*18;
  if(!plat_wifi_found(first+i,name,&rssi,&sec))continue;
  if(first+i==pick)rect(3,y,234,17,RGB(45,48,72));
  clipped(8,y,name,WHITE);if(sec)text(214,y,"*",cyan,FONT_SMALL);
 }
 text(8,184,"SEL SCAN / SAVE JOIN",WHITE,FONT_SMALL);
 text(8,202,"GLO ON/OFF / EDIT PASSWORD",WHITE,FONT_SMALL);
 text(8,222,"ARP RADIO / HOME NEXT",dim,FONT_SMALL);
}

#ifdef PLAT_HOST
#include <string.h>
int lw_wifi_expect(const char *name,int *v){
 if(!strcmp(name,"password_editing"))*v=editing;
 else if(!strcmp(name,"password_length"))*v=len;
 else return 0;
 return 1;
}
#endif
