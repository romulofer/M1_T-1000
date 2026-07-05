/* See COPYING.txt for license details. */

/*
*
* m1_card_list_layout.c
*
* Pure geometry for the rounded-card list rows. See header for contract.
*
* M1 Project
*
*/

#include "m1_card_list_layout.h"

/* Visible scroll window (mirrors draw_list_screen / ir_custom_draw_list). */
S_M1_CardList_Window m1_card_list_window(uint16_t total,
                                         uint16_t selection,
                                         uint16_t visible_max)
{
	S_M1_CardList_Window W;

	if (total == 0 || visible_max == 0)
	{
		W.start_idx = 0;
		W.visible = 0;
		return W;
	}

	if (selection < visible_max)
		W.start_idx = 0;
	else
		W.start_idx = (uint16_t)(selection - visible_max + 1);

	W.visible = (uint16_t)(total - W.start_idx);
	if (W.visible > visible_max)
		W.visible = visible_max;

	return W;
}

/* Rect/icon/label geometry for the row at window slot `row_pos`. */
S_M1_CardList_Row m1_card_list_row(u8g2_uint_t row_pos, int has_icon)
{
	S_M1_CardList_Row R;

	R.y = (u8g2_uint_t)(M1_CARD_START_Y + row_pos * M1_CARD_ROW_H);
	R.h = M1_CARD_ROW_H;
	R.x = M1_CARD_MARGIN_X;
	/* Card spans from the left margin up to (but not into) the scroll column. */
	R.w = (u8g2_uint_t)(M1_CARD_SCROLLBAR_X - M1_CARD_MARGIN_X);

	/* Left icon slot, vertically centered in the row. */
	R.icon_x = (u8g2_uint_t)(R.x + M1_CARD_ICON_PAD);
	R.icon_y = (u8g2_uint_t)(R.y + (M1_CARD_ROW_H - M1_CARD_ICON_H) / 2);

	/* Label: right of the icon slot when present, else just inside the card. */
	if (has_icon)
		R.label_x = (u8g2_uint_t)(R.icon_x + M1_CARD_ICON_W + M1_CARD_LABEL_PAD);
	else
		R.label_x = (u8g2_uint_t)(R.x + M1_CARD_LABEL_PAD);

	/* Baseline centers the ascent within the row (matches y+8 at ROW_H=9). */
	R.label_baseline =
		(u8g2_uint_t)(R.y + (M1_CARD_ROW_H + M1_CARD_TEXT_ASCENT) / 2);

	return R;
}

/* Scroll-thumb rect (mirrors draw_list_screen's proportional thumb). */
S_M1_CardList_Scrollbar m1_card_list_scrollbar(uint16_t total,
                                               uint16_t start_idx,
                                               uint16_t visible_max)
{
	S_M1_CardList_Scrollbar S;
	u8g2_uint_t track_h;
	u8g2_uint_t thumb_h;

	S.x = M1_CARD_SCROLLBAR_X;
	S.w = M1_CARD_SCROLLBAR_W;

	if (visible_max == 0 || total <= visible_max)
	{
		S.y = 0;
		S.h = 0;
		S.visible = 0;
		return S;
	}

	track_h = (u8g2_uint_t)(visible_max * M1_CARD_ROW_H);
	thumb_h = (u8g2_uint_t)((track_h * visible_max) / total);
	if (thumb_h < M1_CARD_SCROLLBAR_MIN)
		thumb_h = M1_CARD_SCROLLBAR_MIN;

	S.y = (u8g2_uint_t)(M1_CARD_START_Y +
	                    (start_idx * (track_h - thumb_h)) / (total - visible_max));
	S.h = thumb_h;
	S.visible = 1;

	return S;
}
