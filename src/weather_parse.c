/* SPDX-License-Identifier: GPL-3.0-only
 * Bounded Open-Meteo current-weather decoder, no heap or floating point.
 * Input must be a complete JSON response from a verified transport. */
#include "weather.h"
static int ws(char c){return c==' '||c=='\n'||c=='\r'||c=='\t';}
static const char *field(const char *p,const char *end,const char *name){
 for(;p<end;p++)if(*p=='"'){
  const char *q=p+1,*k=name;
  while(q<end&&*k&&*q==*k){q++;k++;}
  if(!*k&&q<end&&*q=='"'){q++;while(q<end&&ws(*q))q++;
   if(q<end&&*q==':'){q++;while(q<end&&ws(*q))q++;return q<end?q:0;}}
  while(++p<end&&*p!='"'){if(*p=='\\'&&p+1<end)p++;}
 }
 return 0;
}
static int digit(char c){return c>='0'&&c<='9';}
static int number10(const char *p,const char *end,int *out){
 int sign=1,n=0,f=0;
 if(!p||p>=end)return 0;
 if(*p=='-'){sign=-1;p++;}
 if(p==end||!digit(*p))return 0;
 while(p<end&&digit(*p)){if(n>10000)return 0;n=n*10+*p++-'0';}
 if(p<end&&*p=='.'){p++;if(p==end||!digit(*p))return 0;f=*p++-'0';while(p<end&&digit(*p))p++;}
 if(p<end&&!ws(*p)&&*p!=','&&*p!='}')return 0;
 *out=sign*(n*10+f);return 1;
}
int liquid_weather_parse(const char *json,unsigned len,LiquidWeather *out){
 if(!json||!out||!len||len>8192)return 0;
 const char *end=json+len,*p=field(json,end,"current");
 if(!p||*p!='{')return 0;
 const char *stop=p+1;while(stop<end&&*stop!='}')stop++;
 if(stop==end)return 0;
 int t,w,c,h,m;
 if(!number10(field(p,stop,"temperature_2m"),stop,&t)||
 !number10(field(p,stop,"wind_speed_10m"),stop,&w)||
 !number10(field(p,stop,"weather_code"),stop,&c))return 0;
 const char *time=field(p,stop,"time");
 if(!time||stop-time<18||*time!='"'||time[11]!='T'||time[14]!=':'||
 !digit(time[12])||!digit(time[13])||!digit(time[15])||!digit(time[16]))return 0;
 h=(time[12]-'0')*10+time[13]-'0';m=(time[15]-'0')*10+time[16]-'0';
 if(h>23||m>59||t< -1000||t>700||w<0||w>5000||c<0||c>990||c%10)return 0;
 LiquidWeather v={(int16_t)t,(uint16_t)w,0,(uint16_t)(60*h+m),(uint8_t)(c/10),0};
 if(!liquid_weather_valid(&v))return 0;
 *out=v;return 1;
}
