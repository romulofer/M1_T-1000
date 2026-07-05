/* See COPYING.txt for license details. */

/*
 * test_remotes_ui.c  (HOST TEST)
 *
 * Pure-geometry unit test for the Custom Remotes UI rework helpers. Compiles
 * the REAL m1_csrc/m1_card_list_layout.c with -DM1_CARD_HOST_TEST so the
 * u8g2_uint_t scalar is shimmed and no u8g2 renderer is needed (mirrors the
 * test_please_wait_box shim pattern).
 *
 * Card-list layout asserts:
 *   - scroll window clamps at both ends and matches draw_list_screen math;
 *   - every row card/icon/label rect and the scrollbar thumb stay within
 *     0..127 x 0..63 (and above the bottom action bar);
 *   - card / icon / label / scrollbar regions do not overlap;
 *   - the selected card fully contains its label baseline.
 *
 * M1 Project — host test harness
 */

#include "m1_card_list_layout.h"
#include "m1_tx_status_layout.h"

#include <stdio.h>

/*************************** T E S T   H A R N E S S **************************/

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
	do {                                                                       \
		g_checks++;                                                            \
		if (!(cond)) {                                                         \
			g_failures++;                                                      \
			printf("  FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);        \
		}                                                                      \
	} while (0)

/* Reference scroll-window math, copied verbatim from draw_list_screen /
 * ir_custom_draw_list so the helper is checked against the shipped behavior. */
static void ref_window(uint16_t total, uint16_t selection, uint16_t vmax,
                       uint16_t *start_idx, uint16_t *visible)
{
	uint16_t s, v;
	if (selection < vmax)
		s = 0;
	else
		s = (uint16_t)(selection - vmax + 1);
	v = (uint16_t)(total - s);
	if (v > vmax)
		v = vmax;
	*start_idx = s;
	*visible = v;
}

/*************************** W I N D O W   T E S T S **************************/

static void check_window(uint16_t total, uint16_t selection, uint16_t vmax,
                         const char *label)
{
	S_M1_CardList_Window W = m1_card_list_window(total, selection, vmax);
	uint16_t ref_start, ref_vis;
	ref_window(total, selection, vmax, &ref_start, &ref_vis);

	printf("-- window %s (total=%u sel=%u vmax=%u): start=%u vis=%u\n",
	       label, total, selection, vmax,
	       (unsigned)W.start_idx, (unsigned)W.visible);

	CHECK(W.start_idx == ref_start, "start_idx matches draw_list_screen math");
	CHECK(W.visible == ref_vis, "visible matches draw_list_screen math");
	CHECK(W.visible <= vmax, "visible never exceeds vmax");
	/* selection is always inside the returned window. */
	CHECK(selection >= W.start_idx, "selection at/after window start");
	CHECK(selection < (uint16_t)(W.start_idx + W.visible),
	      "selection before window end");
}

/****************************** R O W   T E S T S ****************************/

static void check_row(u8g2_uint_t row_pos, int has_icon, const char *label)
{
	S_M1_CardList_Row R = m1_card_list_row(row_pos, has_icon);

	printf("-- row %s (pos=%u icon=%d): card=(%u,%u %ux%u) icon=(%u,%u) "
	       "label=(%u base %u)\n", label, (unsigned)row_pos, has_icon,
	       (unsigned)R.x, (unsigned)R.y, (unsigned)R.w, (unsigned)R.h,
	       (unsigned)R.icon_x, (unsigned)R.icon_y,
	       (unsigned)R.label_x, (unsigned)R.label_baseline);

	/* Card fully within the frame and above the bottom action bar. */
	CHECK((long)R.x + R.w <= M1_CARD_SCROLLBAR_X, "card clears scrollbar column");
	CHECK(R.x >= M1_CARD_MARGIN_X, "card past left margin");
	CHECK(R.y >= M1_CARD_START_Y, "card at/below list top");
	CHECK((long)R.y + R.h <= M1_CARD_BOTTOM_Y, "card above bottom bar");
	CHECK((long)R.y + R.h <= M1_CARD_DISP_H, "card within display height");

	/* Icon slot inside the card. */
	if (has_icon)
	{
		CHECK(R.icon_x >= R.x, "icon inside card left");
		CHECK((long)R.icon_x + M1_CARD_ICON_W <= (long)R.x + R.w,
		      "icon inside card right");
		CHECK(R.icon_y >= R.y, "icon inside card top");
		CHECK((long)R.icon_y + M1_CARD_ICON_H <= (long)R.y + R.h,
		      "icon inside card bottom");
		/* Label starts to the right of the icon slot. */
		CHECK(R.label_x >= (u8g2_uint_t)(R.icon_x + M1_CARD_ICON_W),
		      "label clears icon slot");
	}
	else
	{
		CHECK(R.label_x >= R.x, "label inside card left");
	}

	/* Selected card must fully contain its label baseline. */
	CHECK(R.label_baseline > R.y, "baseline below card top");
	CHECK(R.label_baseline <= (u8g2_uint_t)(R.y + R.h),
	      "baseline within card bottom");
	CHECK((long)R.label_baseline - M1_CARD_TEXT_ASCENT >= R.y,
	      "text top within card");
}

/* Rows within one window do not overlap vertically. */
static void check_rows_disjoint(uint16_t vmax)
{
	uint16_t i;
	for (i = 1; i < vmax; i++)
	{
		S_M1_CardList_Row prev = m1_card_list_row((u8g2_uint_t)(i - 1), 1);
		S_M1_CardList_Row cur = m1_card_list_row((u8g2_uint_t)i, 1);
		CHECK((u8g2_uint_t)(prev.y + prev.h) <= cur.y,
		      "adjacent rows do not overlap");
	}
}

/************************ S C R O L L B A R   T E S T S **********************/

static void check_scrollbar(uint16_t total, uint16_t start_idx, uint16_t vmax,
                            const char *label)
{
	S_M1_CardList_Scrollbar S = m1_card_list_scrollbar(total, start_idx, vmax);

	printf("-- scrollbar %s (total=%u start=%u vmax=%u): vis=%d rect=(%u,%u %ux%u)\n",
	       label, total, start_idx, vmax, S.visible,
	       (unsigned)S.x, (unsigned)S.y, (unsigned)S.w, (unsigned)S.h);

	if (total <= vmax)
	{
		CHECK(S.visible == 0, "no thumb when list fits");
		return;
	}

	CHECK(S.visible != 0, "thumb present when list overflows");
	CHECK(S.x == M1_CARD_SCROLLBAR_X, "thumb x at scrollbar column");
	CHECK(S.w == M1_CARD_SCROLLBAR_W, "thumb width");
	CHECK((long)S.x + S.w <= M1_CARD_DISP_W, "thumb within display width");
	CHECK(S.h >= M1_CARD_SCROLLBAR_MIN, "thumb at least min height");
	CHECK(S.y >= M1_CARD_START_Y, "thumb at/below list top");
	CHECK((long)S.y + S.h <= M1_CARD_START_Y + vmax * M1_CARD_ROW_H,
	      "thumb within list track");
	CHECK((long)S.y + S.h <= M1_CARD_BOTTOM_Y, "thumb above bottom bar");
	/* Thumb column (x=126..127) does not intrude on the card column. */
	S_M1_CardList_Row R = m1_card_list_row(0, 1);
	CHECK((long)R.x + R.w <= S.x, "card does not overlap thumb column");
}

/************************ T X - S T A T U S   T E S T S **********************/

#define TX_DISP_W 128
#define TX_DISP_H 64

static void check_tx_status(const S_M1_TxStatus_Line *lines, int n,
                            const char *label)
{
	S_M1_TxStatus_Layout L = m1_tx_status_layout(TX_DISP_W, TX_DISP_H, lines, n);
	int i;
	u8g2_uint_t widest = 0;

	printf("-- tx %s (n=%d): card=(%u,%u %ux%u) n_lines=%d\n",
	       label, n, (unsigned)L.x, (unsigned)L.y,
	       (unsigned)L.w, (unsigned)L.h, L.n_lines);

	CHECK(L.n_lines == n, "n_lines echoes requested count");

	/* Card centered and fully on-screen (with room for the drop shadow). */
	CHECK(L.x == (u8g2_uint_t)((TX_DISP_W - L.w) / 2), "card centered X");
	CHECK(L.y == (u8g2_uint_t)((TX_DISP_H - L.h) / 2), "card centered Y");
	CHECK((long)L.x + L.w + M1_TX_SHADOW <= TX_DISP_W, "card+shadow within width");
	CHECK((long)L.y + L.h + M1_TX_SHADOW <= TX_DISP_H, "card+shadow within height");

	for (i = 0; i < n; i++)
		if (lines[i].w > widest)
			widest = lines[i].w;
	CHECK(L.w >= (u8g2_uint_t)(widest + 2 * M1_TX_PAD), "card wide enough for widest line + pad");

	for (i = 0; i < n; i++)
	{
		/* Each line centered within the card. */
		CHECK((u8g2_uint_t)(L.line_x[i] * 2 + lines[i].w) == (u8g2_uint_t)(L.x * 2 + L.w),
		      "line centered on card x + w/2");
		CHECK(L.line_x[i] >= L.x, "line inside card left");
		CHECK((long)L.line_x[i] + lines[i].w <= (long)L.x + L.w, "line inside card right");
		/* Baseline within the card, text top within the card. */
		CHECK(L.line_baseline[i] <= (u8g2_uint_t)(L.y + L.h), "baseline within card bottom");
		CHECK((long)L.line_baseline[i] - lines[i].ascent >= L.y, "line top within card");
		if (i > 0)
			CHECK(L.line_baseline[i] > L.line_baseline[i - 1],
			      "lines stack top-to-bottom");
	}
}

int main(void)
{
	printf("== Custom Remotes UI geometry tests ==\n");

	const uint16_t VMAX = 4;	/* IR_CUSTOM_LIST_VISIBLE */

	/* Window clamp: short list, first, middle, last selection, long list. */
	check_window(1, 0, VMAX, "N<window, first");
	check_window(3, 2, VMAX, "N<window, last");
	check_window(4, 3, VMAX, "N==window, last");
	check_window(24, 0, VMAX, "long list, first");
	check_window(24, 3, VMAX, "long list, at window edge");
	check_window(24, 4, VMAX, "long list, one past edge (scrolls)");
	check_window(24, 12, VMAX, "long list, middle");
	check_window(24, 23, VMAX, "long list, last");

	/* Rows: every slot in a full window, iconed and icon-less. */
	for (u8g2_uint_t p = 0; p < VMAX; p++)
	{
		check_row(p, 1, "iconed");
		check_row(p, 0, "no icon");
	}
	check_rows_disjoint(VMAX);

	/* Scrollbar: fits (no thumb), overflow at first/middle/last window. */
	check_scrollbar(4, 0, VMAX, "exact fit");
	check_scrollbar(24, 0, VMAX, "overflow, top");
	check_scrollbar(24, 10, VMAX, "overflow, middle");
	check_scrollbar(24, 20, VMAX, "overflow, bottom (start=total-vmax)");

	/* Tx-status card: title (bold courB08 ~ h10/asc7) + body lines (spleen5x8
	 * h8/asc6). Widths are representative of the real transmit strings. */
	{
		/* "Transmitting…" + a truncated name + "Addr:0x.. Cmd:0x..". */
		S_M1_TxStatus_Line three[3] = {
			{ 78, 10, 7 },   /* "Transmitting…" bold */
			{ 96, 8, 6 },    /* button name, truncated to fit */
			{ 92, 8, 6 },    /* "Addr:0xFFFF Cmd:0xFFFF" */
		};
		check_tx_status(three, 3, "transmit: title+name+addr");

		/* Single-line toast ("Unsupported"). */
		S_M1_TxStatus_Line one[1] = { { 64, 10, 7 } };
		check_tx_status(one, 1, "toast: single line");

		/* Two-line ("File read error" / path). */
		S_M1_TxStatus_Line two[2] = { { 84, 10, 7 }, { 70, 8, 6 } };
		check_tx_status(two, 2, "toast: two lines");

		/* Widest line the renderer may present: a centered card plus its drop
		 * shadow must fit (w <= disp_w - 2*SHADOW), so the text truncation
		 * budget is disp_w - 2*PAD - 2*SHADOW. */
		S_M1_TxStatus_Line wide[2] = { { 78, 10, 7 },
		                               { (u8g2_uint_t)(TX_DISP_W - 2 * M1_TX_PAD - 2 * M1_TX_SHADOW), 8, 6 } };
		check_tx_status(wide, 2, "widest line fits frame");
	}

	printf("== %d checks, %d failures ==\n", g_checks, g_failures);
	return (g_failures == 0) ? 0 : 1;
}
