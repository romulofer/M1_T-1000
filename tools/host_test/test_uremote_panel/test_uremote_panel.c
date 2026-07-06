/* See COPYING.txt for license details. */

/*
 * test_uremote_panel.c
 *
 * Host unit test for the Universal Remotes pure helpers. No firmware build,
 * no u8g2 renderer, no FreeRTOS.
 *
 *   T1: uremote_name_matches() -- the case-insensitive record-name matcher
 *       that decides which parsed .ir records a single-function blast sends.
 *       (This is the correctness core: a miss makes a key look "dead".)
 *
 * Compiles the REAL m1_csrc/m1_uremote_match.c. Later tasks extend this file
 * with the m1_uremote_layout grid-geometry assertions (compiled with
 * -DM1_UREMOTE_HOST_TEST so u8g2_uint_t is shimmed).
 *
 * M1 Project
 */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "m1_uremote_match.h"
#include "m1_uremote_layout.h"

static int g_failures = 0;

#define CHECK(cond)                                                    \
	do {                                                               \
		if (!(cond)) {                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			g_failures++;                                             \
		}                                                             \
	} while (0)

/*----------------------------------------------------------------------------*/
/* T1: uremote_name_matches() */
/*----------------------------------------------------------------------------*/
static void test_matcher(void)
{
	static const char *const vol_aliases[] = { "Vol_up", "Volume_Up", "VOL+", NULL };

	/* Exact match, case-insensitive against the record name. */
	CHECK(uremote_name_matches("Power", "Power", NULL));
	CHECK(uremote_name_matches("power", "Power", NULL));
	CHECK(uremote_name_matches("POWER", "power", NULL));

	/* Alias match, case-insensitive. */
	CHECK(uremote_name_matches("vol_up", "Vol+", vol_aliases));
	CHECK(uremote_name_matches("VOLUME_UP", "Vol+", vol_aliases));
	CHECK(uremote_name_matches("Vol+", "Vol+", vol_aliases));  /* record itself */

	/* Unknown names must not match. */
	CHECK(!uremote_name_matches("Mute", "Power", NULL));
	CHECK(!uremote_name_matches("Ch_up", "Vol+", vol_aliases));

	/* No substring false positives (either direction). */
	CHECK(!uremote_name_matches("Power_On", "Power", NULL));
	CHECK(!uremote_name_matches("Power", "Power_On", NULL));
	CHECK(!uremote_name_matches("Vol", "Vol+", vol_aliases));

	/* Defensive NULL handling. */
	CHECK(!uremote_name_matches(NULL, "Power", NULL));
	CHECK(!uremote_name_matches("Power", NULL, NULL));

	/* Empty strings never spuriously match a real name. */
	CHECK(!uremote_name_matches("", "Power", NULL));
	CHECK(!uremote_name_matches("Power", "", NULL));
}

/*----------------------------------------------------------------------------*/
/* T1b: uremote_all_present() + early-exit equivalence (Task 5) */
/*----------------------------------------------------------------------------*/

/* Reference model of uremote_scan_present()'s marking loop: pre-mark every
 * function muted, then for each record name clear the first matching function.
 * `early_exit` mirrors the Task 5 optimization (stop once all are present).
 * Returns the number of records consumed, so a test can prove early-exit reads
 * strictly fewer while producing the same muted[]. Uses the REAL matcher +
 * predicate, so it validates the exact logic wired into the firmware scan. */
static int scan_mark(const char *const *records, int nrec,
                     const char *const *fn_records, int nfn,
                     uint8_t *muted, bool early_exit)
{
	int consumed = 0;

	for (int j = 0; j < nfn; j++)
		muted[j] = 1;

	for (int r = 0; r < nrec; r++)
	{
		consumed++;
		for (int j = 0; j < nfn; j++)
		{
			if (muted[j] &&
			    uremote_name_matches(records[r], fn_records[j], NULL))
			{
				muted[j] = 0;
				break;
			}
		}
		if (early_exit && uremote_all_present(muted, (uint8_t)nfn))
			break;
	}

	return consumed;
}

static void test_all_present(void)
{
	static const char *const fns[] = { "Power", "Vol+", "Mute" };
	const int nfn = 3;

	uint8_t all_zero[3] = { 0, 0, 0 };
	uint8_t has_one[3]  = { 0, 1, 0 };
	uint8_t one_zero[1] = { 0 };
	uint8_t one_set[1]  = { 1 };

	/* Direct predicate contract. */
	CHECK(uremote_all_present(all_zero, 3));    /* all found       -> true  */
	CHECK(!uremote_all_present(has_one, 3));    /* one still muted -> false */
	CHECK(uremote_all_present(all_zero, 0));    /* n == 0 vacuous  -> true  */
	CHECK(!uremote_all_present(NULL, 3));       /* NULL defensive  -> false */
	CHECK(!uremote_all_present(NULL, 0));       /* NULL wins        -> false */
	CHECK(uremote_all_present(one_zero, 1));
	CHECK(!uremote_all_present(one_set, 1));

	/* Equivalence 1: all functions appear early, then trailing records follow.
	 * Early-exit must read fewer records yet leave muted[] identical -- proving
	 * the struck/transmitted cells are unchanged by the optimization. */
	{
		static const char *const stream[] = {
			"Power", "Vol+", "Mute",        /* all present after 3 records */
			"Ch+", "Ch-", "Input", "Menu"   /* trailing -- must not matter  */
		};
		uint8_t m_full[3], m_early[3];
		int c_full  = scan_mark(stream, 7, fns, nfn, m_full,  false);
		int c_early = scan_mark(stream, 7, fns, nfn, m_early, true);

		CHECK(c_full == 7);        /* full scan reads everything      */
		CHECK(c_early == 3);       /* early exit stops at all-present */
		CHECK(c_early < c_full);
		for (int j = 0; j < nfn; j++)
			CHECK(m_full[j] == m_early[j]);      /* identical output */
		CHECK(m_early[0] == 0 && m_early[1] == 0 && m_early[2] == 0);
	}

	/* Equivalence 2: a function is absent -> never "all present", so early-exit
	 * cannot trigger; it must read to EOF and match the full scan exactly. */
	{
		static const char *const stream[] = { "Power", "Ch+", "Mute" };  /* no Vol+ */
		uint8_t m_full[3], m_early[3];
		int c_full  = scan_mark(stream, 3, fns, nfn, m_full,  false);
		int c_early = scan_mark(stream, 3, fns, nfn, m_early, true);

		CHECK(c_full == 3 && c_early == 3);      /* no early stop */
		for (int j = 0; j < nfn; j++)
			CHECK(m_full[j] == m_early[j]);
		CHECK(m_early[0] == 0 && m_early[1] == 1 && m_early[2] == 0);  /* Vol+ muted */
	}
}

/*----------------------------------------------------------------------------*/
/* T2: m1_uremote_layout grid geometry */
/*----------------------------------------------------------------------------*/

/* Do two cell rects overlap? */
static int cells_overlap(const S_M1_Uremote_Cell *a, const S_M1_Uremote_Cell *b)
{
	int ax0 = a->x, ax1 = a->x + a->w;
	int ay0 = a->y, ay1 = a->y + a->h;
	int bx0 = b->x, bx1 = b->x + b->w;
	int by0 = b->y, by1 = b->y + b->h;
	return !(ax1 <= bx0 || bx1 <= ax0 || ay1 <= by0 || by1 <= ay0);
}

/* Every visible cell is in-frame, above the bottom bar, and holds its
 * icon slot + label baseline. */
static void check_cell_in_frame(const S_M1_Uremote_Cell *c)
{
	CHECK(c->x >= 0);
	CHECK(c->x + c->w <= M1_UREMOTE_DISP_W);
	CHECK(c->y >= M1_UREMOTE_HEADER_H);
	CHECK(c->y + c->h <= M1_UREMOTE_BOTTOM_Y);   /* clears the action bar */

	/* Icon slot inside the cell. */
	CHECK(c->icon_x >= c->x);
	CHECK(c->icon_x + M1_UREMOTE_ICON_W <= c->x + c->w);
	CHECK(c->icon_y >= c->y);
	CHECK(c->icon_y + M1_UREMOTE_ICON_H <= c->y + c->h);

	/* Label origin inside the cell; baseline within the cell band. */
	CHECK(c->label_x >= c->x);
	CHECK(c->label_x <= c->x + c->w);
	CHECK(c->label_baseline > c->y);
	CHECK(c->label_baseline <= c->y + c->h);
}

static void test_layout(void)
{
	/* A category large enough to force scrolling (> one page of 6). */
	const uint16_t count = 9;

	for (uint16_t selection = 0; selection < count; selection++)
	{
		uint16_t top = m1_uremote_scroll_top(count, selection);

		/* top is always a whole-row boundary. */
		CHECK((top % M1_UREMOTE_COLS) == 0);

		/* The selected cell is always inside the visible window. */
		S_M1_Uremote_Cell sel;
		CHECK(m1_uremote_cell_rect(selection, top, selection, &sel) == 1);
		CHECK(sel.selected == 1);

		/* Walk every slot; collect the visible ones. */
		S_M1_Uremote_Cell visible[M1_UREMOTE_VISIBLE_CELLS];
		int nvis = 0;

		for (uint16_t idx = 0; idx < count; idx++)
		{
			S_M1_Uremote_Cell c;
			int vis = m1_uremote_cell_rect(idx, top, selection, &c);

			if (idx < top || idx >= (uint16_t)(top + M1_UREMOTE_VISIBLE_CELLS))
			{
				CHECK(vis == 0);   /* outside the window */
				continue;
			}

			CHECK(vis == 1);
			check_cell_in_frame(&c);

			/* Column/row indexing is exact. */
			uint16_t pos = (uint16_t)(idx - top);
			uint16_t col = (uint16_t)(pos % M1_UREMOTE_COLS);
			uint16_t row = (uint16_t)(pos / M1_UREMOTE_COLS);
			CHECK(c.x == (u8g2_uint_t)(M1_UREMOTE_MARGIN_X +
			      col * (M1_UREMOTE_CELL_W + M1_UREMOTE_COL_GAP)));
			CHECK(c.y == (u8g2_uint_t)(M1_UREMOTE_START_Y +
			      row * (M1_UREMOTE_CELL_H + M1_UREMOTE_ROW_GAP)));

			/* selected flag only set for the current selection. */
			CHECK(c.selected == (idx == selection));

			visible[nvis++] = c;
		}

		/* No two visible cells overlap. */
		for (int i = 0; i < nvis; i++)
			for (int j = i + 1; j < nvis; j++)
				CHECK(!cells_overlap(&visible[i], &visible[j]));
	}

	/* A category that fits on one page never scrolls. */
	CHECK(m1_uremote_scroll_top(4, 0) == 0);
	CHECK(m1_uremote_scroll_top(4, 3) == 0);
	CHECK(m1_uremote_scroll_top(M1_UREMOTE_VISIBLE_CELLS, M1_UREMOTE_VISIBLE_CELLS - 1) == 0);
}

int main(void)
{
	test_matcher();
	test_all_present();
	test_layout();

	if (g_failures) {
		printf("uremote_panel: %d check(s) FAILED\n", g_failures);
		return 1;
	}
	printf("uremote_panel: all tests passed\n");
	return 0;
}
