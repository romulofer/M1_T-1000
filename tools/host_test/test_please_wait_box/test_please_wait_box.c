/* See COPYING.txt for license details. */

/*
 * test_please_wait_box.c  (HOST TEST)
 *
 * Pure-geometry unit test for m1_please_wait_layout() — the centered
 * "Please wait..." box (hourglass rework). Compiles the REAL
 * m1_csrc/m1_please_wait_layout.c with -DM1_PW_HOST_TEST so the u8g2_uint_t
 * scalar is shimmed and no u8g2 renderer is needed (mirrors the test_flipper_ir
 * shim pattern).
 *
 * Asserts, for representative caption widths incl. the real courB08 caption:
 *   - box is centered in 128x64;
 *   - box + M1_PW_SHADOW stays within [0,128) x [0,64);
 *   - icon and text are horizontally centered on x + w/2;
 *   - text baseline sits below the icon;
 *   - box is large enough to contain both icon and text.
 *
 * M1 Project — host test harness
 */

#include "m1_please_wait_layout.h"

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

#define DISP_W	128
#define DISP_H	64

/* Verify the full invariant set for one (text_w, ascent) fixture. */
static void check_layout(u8g2_uint_t text_w, u8g2_uint_t ascent,
                         const char *label)
{
	S_M1_PleaseWait_Layout L =
		m1_please_wait_layout(DISP_W, DISP_H, text_w, ascent);

	printf("-- %s (text_w=%u ascent=%u): box=(%u,%u %ux%u)\n",
	       label, (unsigned)text_w, (unsigned)ascent,
	       (unsigned)L.x, (unsigned)L.y, (unsigned)L.w, (unsigned)L.h);

	/* Box dimensions per spec formula. */
	u8g2_uint_t content_w = (text_w > M1_PW_ICON_W) ? text_w : M1_PW_ICON_W;
	CHECK(L.w == (u8g2_uint_t)(content_w + 2 * M1_PW_PAD), "w = max(icon,text)+2*PAD");
	CHECK(L.h == (u8g2_uint_t)(M1_PW_PAD + M1_PW_ICON_H + M1_PW_GAP + ascent + M1_PW_PAD),
	      "h = PAD+ICON_H+GAP+ascent+PAD");

	/* Centered in the display. */
	CHECK(L.x == (u8g2_uint_t)((DISP_W - L.w) / 2), "box centered X");
	CHECK(L.y == (u8g2_uint_t)((DISP_H - L.h) / 2), "box centered Y");

	/* Box + shadow fully on-screen. u8g2_uint_t is unsigned, so x/y >= 0. */
	CHECK((long)L.x + L.w + M1_PW_SHADOW <= DISP_W, "box+shadow within width");
	CHECK((long)L.y + L.h + M1_PW_SHADOW <= DISP_H, "box+shadow within height");

	/* Icon horizontally centered on the box center line. */
	CHECK((u8g2_uint_t)(L.icon_x * 2 + M1_PW_ICON_W) == (u8g2_uint_t)(L.x * 2 + L.w),
	      "icon centered on x + w/2");
	/* Text horizontally centered on the box center line. */
	CHECK((u8g2_uint_t)(L.text_x * 2 + text_w) == (u8g2_uint_t)(L.x * 2 + L.w),
	      "text centered on x + w/2");

	/* Icon sits inside the box (top padding, within left/right). */
	CHECK(L.icon_y >= (u8g2_uint_t)(L.y + M1_PW_PAD), "icon below top pad");
	CHECK(L.icon_x >= L.x, "icon inside left edge");
	CHECK((u8g2_uint_t)(L.icon_x + M1_PW_ICON_W) <= (u8g2_uint_t)(L.x + L.w),
	      "icon inside right edge");
	CHECK((u8g2_uint_t)(L.icon_y + M1_PW_ICON_H) <= (u8g2_uint_t)(L.y + L.h),
	      "icon inside bottom edge");

	/* Text baseline is below the icon and within the box. */
	CHECK(L.text_baseline > (u8g2_uint_t)(L.icon_y + M1_PW_ICON_H),
	      "text baseline below icon");
	CHECK(L.text_baseline <= (u8g2_uint_t)(L.y + L.h), "text baseline within box");

	/* Box is wide enough to contain the caption. */
	CHECK(L.w >= text_w, "box wide enough for text");
	CHECK(L.w >= M1_PW_ICON_W, "box wide enough for icon");
}

int main(void)
{
	printf("== Please-wait box geometry tests ==\n");

	/* Real caption: courB08 "Please wait..." ~ 78px wide, ascent ~7. */
	check_layout(78, 7, "courB08 real caption");
	/* Narrow caption (icon dominates width). */
	check_layout(10, 7, "narrow caption");
	/* Wide-ish caption still fits 128px. */
	check_layout(100, 8, "wide caption");
	/* Icon exactly equals text width boundary. */
	check_layout(M1_PW_ICON_W, 6, "text == icon width");

	printf("== %d checks, %d failures ==\n", g_checks, g_failures);
	return (g_failures == 0) ? 0 : 1;
}
