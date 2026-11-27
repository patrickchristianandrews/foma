/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include "plat.h"
#define APP_NAME "LIQUID WAVE WIFI"
#define APP_SPLASH_RGB 0x981f
#define APP_BLOCK 256u
#define APP_CANVAS_PX (240u * 48u)
#define APP_BIG_FONT FONT_M
enum { OBJ_PANEL, OBJ_BLE, OBJ_WIFI, OBJ_RADIO, OBJ_FAV, OBJ_LIQUID, OBJ_NOBJ };
#define APP_STORE_MAP {0xDC000u,0xDE000u,0xDA000u,0xD8000u,0xD6000u,0xD4000u}
#define APP_LOOP
#ifdef PLAT_HOST
#define APP_HOST_EXPECT
#endif
void app_init(void);
void app_render(int32_t *lr,uint32_t n,uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_loop(void);
void app_say(const char *a,const char *b);
