/* See COPYING.txt for license details. */

/*
 * test_nfc_read_progress.c  (HOST TEST)
 *
 * Unit test for the pure MIFARE Classic result classifier that drives the NFC
 * read-view feedback (Task 3/4). Compiles the REAL shared header
 * NFC/NFC_drv/legacy/nfc_read_progress.h (dependency-free) and exercises
 * mfc_classify_result() across the UID-only / partial / full boundaries.
 *
 * M1 Project — host test harness
 */

#include "nfc_read_progress.h"

#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK_EQ_STR(actual, expected, msg)                                    \
	do {                                                                       \
		g_checks++;                                                            \
		if (strcmp((actual), (expected)) != 0) {                              \
			g_failures++;                                                      \
			printf("  FAIL: %s  (got \"%s\", want \"%s\")  (%s:%d)\n",         \
			       (msg), (actual), (expected), __FILE__, __LINE__);           \
		}                                                                      \
	} while (0)

#define CHECK_EQ_INT(actual, expected, msg)                                    \
	do {                                                                       \
		g_checks++;                                                            \
		long _a = (long)(actual);                                             \
		long _e = (long)(expected);                                           \
		if (_a != _e) {                                                        \
			g_failures++;                                                      \
			printf("  FAIL: %s  (got %ld, want %ld)  (%s:%d)\n",               \
			       (msg), _a, _e, __FILE__, __LINE__);                         \
		}                                                                      \
	} while (0)

int main(void)
{
	/* 0 sectors authed -> identity floor only, regardless of layout size. */
	CHECK_EQ_INT(mfc_classify_result(0, 16), NFC_RD_RESULT_UID_ONLY,
	             "0/16 sectors -> UID_ONLY");
	CHECK_EQ_INT(mfc_classify_result(0, 40), NFC_RD_RESULT_UID_ONLY,
	             "0/40 sectors -> UID_ONLY");
	CHECK_EQ_INT(mfc_classify_result(0, 0), NFC_RD_RESULT_UID_ONLY,
	             "0/0 sectors -> UID_ONLY (degenerate)");

	/* Every sector authed -> full dump. */
	CHECK_EQ_INT(mfc_classify_result(16, 16), NFC_RD_RESULT_FULL,
	             "16/16 sectors -> FULL");
	CHECK_EQ_INT(mfc_classify_result(40, 40), NFC_RD_RESULT_FULL,
	             "40/40 sectors -> FULL");
	/* Defensive: success over-counts total -> still FULL, never PARTIAL. */
	CHECK_EQ_INT(mfc_classify_result(17, 16), NFC_RD_RESULT_FULL,
	             "17/16 sectors -> FULL (defensive)");

	/* Some but not all -> partial. */
	CHECK_EQ_INT(mfc_classify_result(1, 16), NFC_RD_RESULT_PARTIAL,
	             "1/16 sectors -> PARTIAL");
	CHECK_EQ_INT(mfc_classify_result(15, 16), NFC_RD_RESULT_PARTIAL,
	             "15/16 sectors -> PARTIAL");
	CHECK_EQ_INT(mfc_classify_result(39, 40), NFC_RD_RESULT_PARTIAL,
	             "39/40 sectors -> PARTIAL");

	/* Completion status line builder used by the read-done screen. */
	{
		char buf[24];
		nfc_read_completion_status(NFC_RD_RESULT_UID_ONLY, 0, 16, buf, sizeof(buf));
		CHECK_EQ_STR(buf, "Read (UID only)", "UID_ONLY -> 'Read (UID only)'");

		nfc_read_completion_status(NFC_RD_RESULT_FULL, 16, 16, buf, sizeof(buf));
		CHECK_EQ_STR(buf, "Sectors 16/16", "FULL -> 'Sectors 16/16'");

		nfc_read_completion_status(NFC_RD_RESULT_PARTIAL, 5, 16, buf, sizeof(buf));
		CHECK_EQ_STR(buf, "Sectors 5/16", "PARTIAL -> 'Sectors 5/16'");

		/* NONE = non-MFC read: empty so the caller keeps its default layout. */
		nfc_read_completion_status(NFC_RD_RESULT_NONE, 0, 0, buf, sizeof(buf));
		CHECK_EQ_STR(buf, "", "NONE -> '' (default layout preserved)");

		/* Must never overflow a small 128x64-sized buffer. */
		char small[16];
		nfc_read_completion_status(NFC_RD_RESULT_FULL, 40, 40, small, sizeof(small));
		g_checks++;
		if (strlen(small) >= sizeof(small)) {
			g_failures++;
			printf("  FAIL: completion status overran small buffer\n");
		}
	}

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
