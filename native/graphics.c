/* SPDX-License-Identifier: GPL-3.0-only */
/* Native FM1 Base bridge. Independent from the published Felucca target.
 * Initial bring-up uses retained RGB565 graphics for visual parity.
 * The full framebuffer is a known memory constraint for the Wi-Fi build. */
#define FOMA_BLUETOOTH 0
#define FOMA_USB_AUDIO 0
#define FOMA_TRS_MIDI 0
#define FOMA_MIDI_CLOCK 0
#include "../platform/fm1.h"
#include <stdint.h>
typedef struct {
 uint8_t h,pad,first,last;
 const uint8_t *adv,*bw;
 const uint16_t *off;
 const uint8_t *data;
} felucca_font_t;
#include "../platform/gen/felucca_font.h"
static void fmt_int(char *d,int32_t value) {
 char t[12]; unsigned n=0;
 uint32_t u=value<0?0u-(uint32_t)value:(uint32_t)value;
 if(value<0)*d++='-';
 do {t[n++]=(char)('0'+u%10u);u/=10u;}while(u);
 while(n)*d++=t[--n];
 *d=0;
}
#include "../platform/common.c"
const uint16_t *lw_native_fb(void){return fb;}
int lw_native_dirty(int *end){
 int start=fb_y0;*end=fb_y1;
 fb_y0=SCREEN_H;fb_y1=0;
 return start;
}
