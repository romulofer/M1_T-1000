/* See COPYING.txt for license details. */

/*
*
* m1_please_wait_layout.c
*
* Pure geometry for the centered "Please wait..." box. See header for contract.
*
* M1 Project
*
*/

#include "m1_please_wait_layout.h"

S_M1_PleaseWait_Layout m1_please_wait_layout(u8g2_uint_t disp_w,
                                             u8g2_uint_t disp_h,
                                             u8g2_uint_t text_w,
                                             u8g2_uint_t text_ascent)
{
	S_M1_PleaseWait_Layout L;

	u8g2_uint_t content_w = (text_w > M1_PW_ICON_W) ? text_w : M1_PW_ICON_W;

	L.w = content_w + 2 * M1_PW_PAD;
	L.h = M1_PW_PAD + M1_PW_ICON_H + M1_PW_GAP + text_ascent + M1_PW_PAD;
	L.x = (disp_w - L.w) / 2;
	L.y = (disp_h - L.h) / 2;

	// Icon and caption horizontally centered on the box center line.
	L.icon_x = L.x + (L.w - M1_PW_ICON_W) / 2;
	L.icon_y = L.y + M1_PW_PAD;
	L.text_x = L.x + (L.w - text_w) / 2;
	L.text_baseline = L.icon_y + M1_PW_ICON_H + M1_PW_GAP + text_ascent;

	return L;
}
