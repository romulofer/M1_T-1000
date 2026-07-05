/* See COPYING.txt for license details. */

/*
*
* m1_tx_status_layout.c
*
* Pure geometry for the centered IR transmit/status card. See header.
*
* M1 Project
*
*/

#include "m1_tx_status_layout.h"

S_M1_TxStatus_Layout m1_tx_status_layout(u8g2_uint_t disp_w,
                                         u8g2_uint_t disp_h,
                                         const S_M1_TxStatus_Line *lines,
                                         int n_lines)
{
	S_M1_TxStatus_Layout L;
	u8g2_uint_t content_w = 0;
	u8g2_uint_t content_h = 0;
	u8g2_uint_t row_top;
	int i;

	if (n_lines < 1)
		n_lines = 1;
	if (n_lines > M1_TX_MAX_LINES)
		n_lines = M1_TX_MAX_LINES;
	L.n_lines = n_lines;

	/* Content box = widest line wide, sum of line heights + gaps tall. */
	for (i = 0; i < n_lines; i++)
	{
		if (lines[i].w > content_w)
			content_w = lines[i].w;
		content_h = (u8g2_uint_t)(content_h + lines[i].h);
		if (i > 0)
			content_h = (u8g2_uint_t)(content_h + M1_TX_LINE_GAP);
	}

	L.w = (u8g2_uint_t)(content_w + 2 * M1_TX_PAD);
	L.h = (u8g2_uint_t)(content_h + 2 * M1_TX_PAD);
	L.x = (u8g2_uint_t)((disp_w - L.w) / 2);
	L.y = (u8g2_uint_t)((disp_h - L.h) / 2);

	/* Stack the lines, each horizontally centered in the card. */
	row_top = (u8g2_uint_t)(L.y + M1_TX_PAD);
	for (i = 0; i < n_lines; i++)
	{
		L.line_x[i] = (u8g2_uint_t)(L.x + (L.w - lines[i].w) / 2);
		L.line_baseline[i] = (u8g2_uint_t)(row_top + lines[i].ascent);
		row_top = (u8g2_uint_t)(row_top + lines[i].h + M1_TX_LINE_GAP);
	}
	/* Zero the unused slots for determinism. */
	for (; i < M1_TX_MAX_LINES; i++)
	{
		L.line_x[i] = 0;
		L.line_baseline[i] = 0;
	}

	return L;
}
