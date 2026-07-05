/* See COPYING.txt for license details. */

/*
*
* m1_uremote_layout.c
*
* Pure geometry for the Universal Remotes 2-column icon grid. See header for
* the contract.
*
* M1 Project
*
*/

#include "m1_uremote_layout.h"

#include <stddef.h>

uint16_t m1_uremote_scroll_top(uint16_t count, uint16_t selection)
{
	if (count == 0)
		return 0;

	uint16_t sel_row = (uint16_t)(selection / M1_UREMOTE_COLS);
	uint16_t top_row;

	/* Keep the selected row visible: no scroll until it would fall below the
	 * last visible row, then scroll just enough to pin it at the bottom row
	 * (mirrors m1_card_list_window's clamp, one page = VISIBLE_ROWS). */
	if (sel_row < M1_UREMOTE_VISIBLE_ROWS)
		top_row = 0;
	else
		top_row = (uint16_t)(sel_row - M1_UREMOTE_VISIBLE_ROWS + 1);

	return (uint16_t)(top_row * M1_UREMOTE_COLS);
}

int m1_uremote_cell_rect(uint16_t idx, uint16_t top, uint16_t selection,
                         S_M1_Uremote_Cell *out)
{
	if (out == NULL)
		return 0;

	/* Reject anything outside the current visible window. */
	if (idx < top)
		return 0;

	uint16_t pos = (uint16_t)(idx - top);   /* slot within the window */
	if (pos >= M1_UREMOTE_VISIBLE_CELLS)
		return 0;

	uint16_t col = (uint16_t)(pos % M1_UREMOTE_COLS);
	uint16_t row = (uint16_t)(pos / M1_UREMOTE_COLS);

	out->x = (u8g2_uint_t)(M1_UREMOTE_MARGIN_X +
	                       col * (M1_UREMOTE_CELL_W + M1_UREMOTE_COL_GAP));
	out->y = (u8g2_uint_t)(M1_UREMOTE_START_Y +
	                       row * (M1_UREMOTE_CELL_H + M1_UREMOTE_ROW_GAP));
	out->w = M1_UREMOTE_CELL_W;
	out->h = M1_UREMOTE_CELL_H;

	/* Icon slot, vertically centered in the cell. */
	out->icon_x = (u8g2_uint_t)(out->x + M1_UREMOTE_ICON_PAD);
	out->icon_y = (u8g2_uint_t)(out->y + (M1_UREMOTE_CELL_H - M1_UREMOTE_ICON_H) / 2);

	/* Caption to the right of the icon; baseline centers the ascent. */
	out->label_x = (u8g2_uint_t)(out->icon_x + M1_UREMOTE_ICON_W + M1_UREMOTE_LABEL_PAD);
	out->label_baseline =
		(u8g2_uint_t)(out->y + (M1_UREMOTE_CELL_H + M1_UREMOTE_TEXT_ASCENT) / 2);

	out->selected = (idx == selection) ? 1 : 0;

	return 1;
}
