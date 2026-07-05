/* See COPYING.txt for license details. */

/*
*
* m1_please_wait_layout.h
*
* Pure geometry for the centered "Please wait..." box (hourglass rework).
* No u8g2 renderer dependency: the only external type is the u8g2 pixel scalar
* u8g2_uint_t. Firmware pulls that from u8g2.h; the host geometry test shims it
* by compiling with -DM1_PW_HOST_TEST.
*
* M1 Project
*
*/

#ifndef M1_PLEASE_WAIT_LAYOUT_H_
#define M1_PLEASE_WAIT_LAYOUT_H_

#ifdef M1_PW_HOST_TEST
#include <stdint.h>
typedef uint16_t u8g2_uint_t;		// host shim: matches u8g2 16-bit build
#else
#include "u8g2.h"					// firmware: real u8g2_uint_t
#endif

// Box geometry constants (see SPEC.md §Geometry).
#define M1_PW_ICON_W	18	// hourglass_18x32 width
#define M1_PW_ICON_H	32	// hourglass_18x32 height
#define M1_PW_RADIUS	4	// rounded-corner radius
#define M1_PW_SHADOW	2	// drop-shadow offset (down-right)
#define M1_PW_PAD		6	// inner padding around content
#define M1_PW_GAP		3	// vertical gap between icon and caption

typedef struct
{
	u8g2_uint_t x, y, w, h;			// box rect
	u8g2_uint_t icon_x, icon_y;		// hourglass top-left
	u8g2_uint_t text_x, text_baseline;	// caption anchor
} S_M1_PleaseWait_Layout;

/* Pure layout math for the centered box. disp_w/disp_h = 128/64; text_w is the
 * measured pixel width of "Please wait..."; text_ascent is the caption font
 * ascent. Box is centered in the display; icon and text are horizontally
 * centered on x + w/2; text baseline sits below the icon. */
S_M1_PleaseWait_Layout m1_please_wait_layout(u8g2_uint_t disp_w,
                                             u8g2_uint_t disp_h,
                                             u8g2_uint_t text_w,
                                             u8g2_uint_t text_ascent);

#endif /* M1_PLEASE_WAIT_LAYOUT_H_ */
