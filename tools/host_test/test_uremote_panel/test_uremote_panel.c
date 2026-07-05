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

#include "m1_uremote_match.h"

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

int main(void)
{
	test_matcher();

	if (g_failures) {
		printf("uremote_panel: %d check(s) FAILED\n", g_failures);
		return 1;
	}
	printf("uremote_panel: all tests passed\n");
	return 0;
}
