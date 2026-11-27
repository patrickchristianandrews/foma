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

/* Convert server UTC seconds of day using the forecast destination's
 * current UTC offset, including fractional-hour zones and midnight wrap. */
int liquid_weather_clock(const char *json,unsigned len,int utc){
 if(!json||len>8192||utc<0||utc>=86400)return -1;
 int offset10;
 if(!number10(field(json,json+len,"utc_offset_seconds"),json+len,&offset10))return -1;
 if(offset10%10||offset10 < -432000 || offset10 > 504000)return -1;
 return (utc+offset10/10+86400)%86400;
}
static int append(char **p,unsigned *left,const char *s){
 while(*s){if(*left<=1)return 0;*(*p)++=*s++;--*left;}return 1;
}
static int coordinate(char **p,unsigned *left,int32_t n){
 char digits[12];unsigned i=0;uint32_t v;
 if(n<0){if(!append(p,left,"-"))return 0;v=(uint32_t)(-n);}else v=(uint32_t)n;
 unsigned whole=v/100u;
 do{digits[i++]=(char)('0'+whole%10u);whole/=10u;}while(whole);
 while(i){char t[2]={digits[--i],0};if(!append(p,left,t))return 0;}
 char frac[4]={'.',(char)('0'+v/10u%10u),(char)('0'+v%10u),0};
 return append(p,left,frac);
}
unsigned liquid_weather_request(unsigned destination,char *out,unsigned capacity){
 if(!out||!capacity)return 0;
 out[0]=0;
 if(destination>=liquid_place_count)return 0;
 char *p=out;unsigned left=capacity;
 const LiquidPlace *place=&liquid_places[destination];
 if(!append(&p,&left,"/v1/forecast?latitude=")||
 !coordinate(&p,&left,place->latitude100)||
 !append(&p,&left,"&longitude=")||
 !coordinate(&p,&left,place->longitude100)||
 !append(&p,&left,"&current=temperature_2m,wind_speed_10m,weather_code&timezone=auto")){
 out[0]=0;return 0;}
 *p=0;return (unsigned)(p-out);
}
