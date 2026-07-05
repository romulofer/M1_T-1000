/* See COPYING.txt for license details. */

/*
*
* m1_uremote_layout.h
*
* Pure geometry for the Universal Remotes icon panel: a 2-column x N-row grid
* of remote-key cells (Power, Vol +/-, Ch +/-, Mute, ...) with row scrolling.
* Mirrors the host-testable style of m1_card_list_layout.h -- no u8g2 renderer
* dependency; the only external type is the u8g2 pixel scalar u8g2_uint_t.
* Firmware pulls that from u8g2.h; the host geometry test shims it by
* compiling with -DM1_UREMOTE_HOST_TEST.
*
* The renderer (m1_uremote_panel() in m1_display.c) is a thin wrapper over
* these functions; all clip/scroll correctness lives here and is unit-tested.
*
* M1 Project
*
*/

#ifndef M1_UREMOTE_LAYOUT_H_
#define M1_UREMOTE_LAYOUT_H_

#ifdef M1_UREMOTE_HOST_TEST
#include <stdint.h>
typedef uint16_t u8g2_uint_t;		// host shim: matches u8g2 16-bit build
#else
#include "u8g2.h"					// firmware: real u8g2_uint_t
#endif

/* Grid geometry. The panel lives between the title underline (y = HEADER_H)
 * and the bottom action bar (top at BOTTOM_Y = 51). Two columns of CELL_W
 * cells with COL_GAP between them; rows of CELL_H with ROW_GAP between them.
 * VISIBLE_ROWS rows fit above the bar:
 *   START_Y + VISIBLE_ROWS*(CELL_H+ROW_GAP) - ROW_GAP <= BOTTOM_Y. */
#define M1_UREMOTE_DISP_W          128
#define M1_UREMOTE_DISP_H          64
#define M1_UREMOTE_HEADER_H        12		// title underline y
#define M1_UREMOTE_START_Y         14		// first row top (HEADER_H + 2)
#define M1_UREMOTE_BOTTOM_Y        51		// bottom action-bar top; cells stay above
#define M1_UREMOTE_COLS            2			// grid columns
#define M1_UREMOTE_MARGIN_X        2			// left/right frame inset
#define M1_UREMOTE_COL_GAP         2			// horizontal gap between the two columns
#define M1_UREMOTE_CELL_W          61		// (DISP_W - 2*MARGIN_X - COL_GAP)/COLS
#define M1_UREMOTE_CELL_H          11		// cell height
#define M1_UREMOTE_ROW_GAP         1			// vertical gap between rows (pitch = 12)
#define M1_UREMOTE_VISIBLE_ROWS    3			// rows drawn per page (14/26/38, bottoms <=49)
#define M1_UREMOTE_VISIBLE_CELLS   (M1_UREMOTE_COLS * M1_UREMOTE_VISIBLE_ROWS)  // 6
#define M1_UREMOTE_RADIUS          2			// rounded-cell corner radius
#define M1_UREMOTE_ICON_W          8			// left icon slot width (8x8 glyphs)
#define M1_UREMOTE_ICON_H          8
#define M1_UREMOTE_ICON_PAD        2			// gap: cell left edge -> icon
#define M1_UREMOTE_LABEL_PAD       2			// gap: icon -> label
#define M1_UREMOTE_TEXT_ASCENT     7			// caption font ascent (baseline centering)

/* One drawn grid cell. `selected` is 1 when this cell is the current
 * selection (renderer draws the highlight). */
typedef struct
{
	u8g2_uint_t x, y, w, h;					// cell rect
	u8g2_uint_t icon_x, icon_y;				// icon slot top-left (8x8)
	u8g2_uint_t label_x, label_baseline;	// caption draw origin (u8g2 baseline)
	int         selected;
} S_M1_Uremote_Cell;

/* Index of the top-left visible cell (row-aligned, a multiple of COLS) for a
 * grid of `count` functions with `selection` highlighted, keeping the selected
 * row on-screen. Mirrors m1_card_list_window's clamp, applied per row. */
uint16_t m1_uremote_scroll_top(uint16_t count, uint16_t selection);

/* Geometry for the cell at absolute index `idx` given the scroll window whose
 * first visible cell is `top`. Returns 1 and fills *out (with out->selected =
 * (idx == selection)) when idx is within the visible window
 * [top, top+VISIBLE_CELLS); returns 0 and leaves *out untouched otherwise. */
int m1_uremote_cell_rect(uint16_t idx, uint16_t top, uint16_t selection,
                         S_M1_Uremote_Cell *out);

#endif /* M1_UREMOTE_LAYOUT_H_ */
