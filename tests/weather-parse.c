#include "weather.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int parse(const char *s,LiquidWeather *w){return liquid_weather_parse(s,(unsigned)strlen(s),w);}
int main(void){
 LiquidWeather w={0};
 assert(parse("{\"current_units\":{\"temperature_2m\":\"C\"},\"current\":{\"time\":\"2026-11-26T17:45\",\"temperature_2m\":-3.7,\"wind_speed_10m\":12.4,\"weather_code\":71}}",&w));
 assert(w.temperature10==-37&&w.wind10==124&&w.code==71&&w.local_minute==1065&&!w.has_aqi);
 assert(!parse("{\"current\":{\"time\":\"2026-11-26T24:45\",\"temperature_2m\":3,\"wind_speed_10m\":12,\"weather_code\":0}}",&w));
 assert(!parse("{\"current\":{\"time\":\"2026-11-26T17:45\",\"temperature_2m\":null,\"wind_speed_10m\":12,\"weather_code\":0}}",&w));
 assert(!parse("{\"current\":{\"time\":\"2026-11-26T17:45\",\"temperature_2m\":3,\"wind_speed_10m\":-12,\"weather_code\":0}}",&w));
 assert(!parse("{\"current\":{\"time\":\"2026-11-26T17:45\",\"temperature_2m\":3,\"wind_speed_10m\":12,\"weather_code\":90}}",&w));
 assert(!parse("{\"current\":{\"time\":\"2026-11-26T17:45\",\"temperature_2m\":3",&w));
 const char *offset="{\"utc_offset_seconds\":-18000}";
 assert(liquid_weather_clock(offset,strlen(offset),5*3600+14*60+23)==14*60+23);
 assert(liquid_weather_clock(offset,strlen(offset),4*3600+59*60)==23*3600+59*60);
 const char *half="{\"utc_offset_seconds\":19800}";
 assert(liquid_weather_clock(half,strlen(half),20*3600)==90*60);
 assert(liquid_weather_clock("{}",2,0)==-1);
 assert(liquid_weather_clock(offset,strlen(offset),-1)==-1);
 puts("Weather decoder checks passed");return 0;
}
