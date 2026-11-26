/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LIQUID_WEATHER_H
#define LIQUID_WEATHER_H
#include <stdint.h>

typedef struct {
    int16_t temperature10;
    uint16_t wind10, aqi, local_minute;
    uint8_t code, has_aqi;
} LiquidWeather;

typedef struct {
    const char *name;
    int32_t latitude100, longitude100;
    uint8_t rhythm;
} LiquidPlace;

typedef struct {
    uint8_t style, bpm, drum_mask, lead_level, pad_level;
    int8_t transpose;
} LiquidWeatherMusic;

enum { LIQUID_MOON = -1 };
extern const LiquidPlace liquid_places[];
extern const unsigned liquid_place_count;
const char *liquid_weather_condition(uint8_t code);
int liquid_weather_valid(const LiquidWeather *weather);
LiquidWeatherMusic liquid_weather_music(const LiquidWeather *weather, uint8_t rhythm, int moon);
/* Call from the main loop after taking a complete reading from the weather backend. */
int liquid_weather_accept(unsigned destination, const LiquidWeather *reading);
int liquid_weather_destination(void);

#endif
