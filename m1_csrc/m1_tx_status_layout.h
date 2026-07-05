/* See COPYING.txt for license details. */

/*
*
* m1_tx_status_layout.h
*
* Pure geometry for the centered IR transmit/status card ("Transmitting…" /
* <button name> / "Addr:… Cmd:…", plus the Unsupported/error toasts and the
* power-blast progress/summary). Stacks 1..3 centered text lines (each with its
* own measured width/height/ascent, so the title and body may use different
* fonts) inside a rounded card centered in the 128x64 frame. No u8g2 renderer
* dependency: the only external type is the u8g2 pixel scalar u8g2_uint_t.
* Firmware pulls that from u8g2.h; the host test shims it via -DM1_TX_HOST_TEST.
*
* M1 Project
*
*/

#ifndef M1_TX_STATUS_LAYOUT_H_
#define M1_TX_STATUS_LAYOUT_H_

#ifdef M1_TX_HOST_TEST
#include <stdint.h>
#ifndef M1_HOST_TEST_U8G2_UINT_T
#define M1_HOST_TEST_U8G2_UINT_T
typedef uint16_t u8g2_uint_t;		// host shim: matches u8g2 16-bit build
#endif
#else
#include "u8g2.h"					// firmware: real u8g2_uint_t
#endif

#define M1_TX_MAX_LINES   3			// title + up to two body lines
#define M1_TX_RADIUS      4			// rounded-corner radius
#define M1_TX_SHADOW      2			// drop-shadow offset (down-right)
#define M1_TX_PAD         5			// inner padding around the text block
#define M1_TX_LINE_GAP    2			// vertical gap between stacked lines

/* One measured text line fed to the layout. */
typedef struct
{
	u8g2_uint_t w;			// pixel width of the (already-truncated) string
	u8g2_uint_t h;			// font line height (ascent + descent)
	u8g2_uint_t ascent;		// baseline offset from the line's top
} S_M1_TxStatus_Line;

/* Resolved card + per-line placement. */
typedef struct
{
	u8g2_uint_t x, y, w, h;						// card rect
	u8g2_uint_t line_x[M1_TX_MAX_LINES];		// left origin of each line (centered)
	u8g2_uint_t line_baseline[M1_TX_MAX_LINES];	// u8g2 baseline of each line
	int         n_lines;
} S_M1_TxStatus_Layout;

/* Pure layout: card centered in disp_w x disp_h, sized to the widest line plus
 * padding; lines stacked top-to-bottom, each horizontally centered in the card.
 * n_lines is clamped to [1, M1_TX_MAX_LINES]. */
S_M1_TxStatus_Layout m1_tx_status_layout(u8g2_uint_t disp_w,
                                         u8g2_uint_t disp_h,
                                         const S_M1_TxStatus_Line *lines,
                                         int n_lines);

#endif /* M1_TX_STATUS_LAYOUT_H_ */
