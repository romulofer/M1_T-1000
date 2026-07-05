/* See COPYING.txt for license details. */

/*
*
* m1_card_list_layout.h
*
* Pure geometry for the rounded-card list rows used by the Custom Remotes
* editor (My Remotes manager, per-remote menu, button list, action menus).
* Mirrors the scroll-window math of m1_ir_universal draw_list_screen /
* m1_ir_custom ir_custom_draw_list, but lifts it out of the renderer so it is
* host-testable. No u8g2 renderer dependency: the only external type is the
* u8g2 pixel scalar u8g2_uint_t. Firmware pulls that from u8g2.h; the host
* geometry test shims it by compiling with -DM1_CARD_HOST_TEST.
*
* M1 Project
*
*/

#ifndef M1_CARD_LIST_LAYOUT_H_
#define M1_CARD_LIST_LAYOUT_H_

#ifdef M1_CARD_HOST_TEST
#include <stdint.h>
typedef uint16_t u8g2_uint_t;		// host shim: matches u8g2 16-bit build
#else
#include "u8g2.h"					// firmware: real u8g2_uint_t
#endif

/* Card-list geometry constants. The list area lives between the title
 * underline (y = HEADER_H) and the bottom action bar (top at BOTTOM_Y = 51,
 * drawn by m1_draw_bottom_bar). START_Y + VISIBLE_MAX*ROW_H must stay <=
 * BOTTOM_Y so rows never collide with the bar. */
#define M1_CARD_DISP_W        128
#define M1_CARD_DISP_H        64
#define M1_CARD_HEADER_H      12		// title underline y
#define M1_CARD_START_Y       14		// first row top (HEADER_H + 2)
#define M1_CARD_ROW_H         9			// card row height (== IR_CUSTOM_LIST_ITEM_H)
#define M1_CARD_BOTTOM_Y      51		// bottom action-bar top; rows stay above
#define M1_CARD_RADIUS        2			// rounded-card corner radius
#define M1_CARD_MARGIN_X      1			// left inset so rounded corners clear x=0
#define M1_CARD_ICON_W        8			// left row-icon slot width (8x8 glyphs)
#define M1_CARD_ICON_H        8
#define M1_CARD_ICON_PAD      2			// gap: card left edge -> icon
#define M1_CARD_LABEL_PAD     3			// gap: icon (or card left) -> label
#define M1_CARD_TEXT_ASCENT   7			// FUNC_MENU_FONT_N ascent (baseline centering)
#define M1_CARD_SCROLLBAR_W   2			// scroll-thumb width
#define M1_CARD_SCROLLBAR_X   126		// scroll-thumb left x (126..127)
#define M1_CARD_SCROLLBAR_MIN 4			// minimum thumb height

/* Visible scroll window for a list of `total` items with `selection`
 * highlighted, showing at most `visible_max` rows at once. */
typedef struct
{
	uint16_t start_idx;		// index of the first visible item
	uint16_t visible;		// number of rows actually drawn (<= visible_max)
} S_M1_CardList_Window;

/* One drawn row. `row_pos` is the row's slot within the window (0..visible-1),
 * NOT the absolute item index. */
typedef struct
{
	u8g2_uint_t x, y, w, h;				// rounded-card rect
	u8g2_uint_t icon_x, icon_y;			// left icon slot top-left (8x8)
	u8g2_uint_t label_x, label_baseline;	// label draw origin (u8g2 baseline)
} S_M1_CardList_Row;

/* Scroll-thumb rect. `visible` is 0 when the whole list fits (no thumb). */
typedef struct
{
	u8g2_uint_t x, y, w, h;
	int         visible;
} S_M1_CardList_Scrollbar;

/* Pure window math (mirrors draw_list_screen): clamps at both ends. */
S_M1_CardList_Window m1_card_list_window(uint16_t total,
                                         uint16_t selection,
                                         uint16_t visible_max);

/* Rect/icon/label geometry for the row at window slot `row_pos`.
 * has_icon shifts the label right to clear the icon slot. */
S_M1_CardList_Row m1_card_list_row(u8g2_uint_t row_pos, int has_icon);

/* Scroll-thumb rect (mirrors draw_list_screen's proportional thumb). */
S_M1_CardList_Scrollbar m1_card_list_scrollbar(uint16_t total,
                                               uint16_t start_idx,
                                               uint16_t visible_max);

#endif /* M1_CARD_LIST_LAYOUT_H_ */
