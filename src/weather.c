/* SPDX-License-Identifier: GPL-3.0-only */
#include "weather.h"

/* Cities, rounded to hundredths of a degree. Home can later be set by the network setup UI. */
const LiquidPlace liquid_places[] = {
    {"RICHMOND, VA", 3754, -7744, 0}, {"REYKJAVIK, IS", 6415, -2194, 1},
    {"ACCRA, GH", 560, -20, 2}, {"TOKYO, JP", 3568, 13969, 3},
    {"LIMA, PE", -1205, -7704, 2}, {"WELLINGTON, NZ", -4129, 17478, 1},
    {"ST JOHNS, CA", 4756, -5271, 2}, {"CAPE TOWN, ZA", -3392, 1842, 3},
    {"HELSINKI, FI", 6017, 2494, 1}, {"MANILA, PH", 1460, 12098, 2},
    {"MEXICO CITY, MX", 1943, -9913, 3}, {"ISTANBUL, TR", 4101, 2897, 2},
    {"VIRGINIA BEACH, VA", 3685, -7598, 1},
};
const unsigned liquid_place_count = sizeof liquid_places / sizeof liquid_places[0];

const char *liquid_weather_condition(uint8_t code) {
    if (code == 0) return "CLEAR";
    if (code <= 3) return "CLOUDY";
    if (code == 45 || code == 48) return "FOG";
    if (code >= 95 && code <= 99) return "THUNDERSTORM";
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return "SNOW";
    if (code >= 51 && code <= 57) return "DRIZZLE";
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "RAIN";
    return "UNKNOWN";
}

int liquid_weather_valid(const LiquidWeather *weather) {
    if (!weather || weather->local_minute >= 1440 || weather->temperature10 < -1000 ||
        weather->temperature10 > 700 || weather->wind10 > 5000 || weather->has_aqi > 1 ||
        (weather->has_aqi && weather->aqi > 500)) return 0;
    return liquid_weather_condition(weather->code)[0] != 'U';
}

LiquidWeatherMusic liquid_weather_music(const LiquidWeather *weather, uint8_t rhythm, int moon) {
    static const int8_t keys[] = {0,-2,3,5};
    const uint8_t flavor = rhythm % 4;
    if (moon) return (LiquidWeatherMusic){2, 64, 0, 58, 100, -5};

    LiquidWeatherMusic result = {flavor, 100, 7, 90, 85, keys[flavor]};
    int tempo = 90 + (int)flavor * 4 + (int)weather->wind10 / 25;
    const char condition = liquid_weather_condition(weather->code)[0];
    if (condition == 'F' || condition == 'S') {
        result.style = 2;
        result.drum_mask = 4;
        result.pad_level = 100;
        tempo -= 20;
    } else if (condition == 'R' || condition == 'D' || condition == 'T') {
        result.style = condition == 'T' ? 3 : 1;
        result.drum_mask = condition == 'D' ? 5 : 7;
        tempo += condition == 'T' ? 16 : 5;
    }
    if (weather->temperature10 >= 260) {
        tempo += 6;
        result.transpose += 2;
    } else if (weather->temperature10 < 40) {
        tempo -= 7;
        result.transpose -= 2;
    }
    if (weather->has_aqi && weather->aqi > 100) result.lead_level = 65;
    if (weather->local_minute < 360 || weather->local_minute >= 1320) {
        tempo -= 9;
        result.drum_mask &= 5;
        result.pad_level = 100;
    }
    result.bpm = (uint8_t)(tempo < 60 ? 60 : tempo > 160 ? 160 : tempo);
    return result;
}
