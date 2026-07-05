/* See COPYING.txt for license details. */

/*
 * test_msgbox.c  (HOST TEST)
 *
 * Unit tests for the pure message-box layout core (m1_csrc/m1_msgbox_layout.c).
 * Compiles the REAL layout source with a len*6 stub measure callback and
 * asserts SPEC acceptance criteria:
 *   crit 1 (Task 1): word-wrap breaks at spaces, never exceeds width, preserves
 *                    word order; an over-long token is hard-broken not dropped;
 *                    output is capped at M1_MSGBOX_MAX_LINES.
 *
 * M1 Project — host test harness
 */

#include "m1_msgbox_layout.h"

#include <stdio.h>
#include <string.h>

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

#define CHECK_EQ_STR(actual, expected, msg)                                    \
	do {                                                                       \
		g_checks++;                                                            \
		if (strcmp((actual), (expected)) != 0) {                              \
			g_failures++;                                                      \
			printf("  FAIL: %s  (got \"%s\", want \"%s\")  (%s:%d)\n",         \
			       (msg), (actual), (expected), __FILE__, __LINE__);           \
		}                                                                      \
	} while (0)

/*************************** M E A S U R E   S T U B *************************/

/* Fixed 6px-per-char measure — mirrors M1_GUI_FONT_WIDTH for a mono host stub.
 * The host proves the *algorithm* (space-break, hard-break, order, cap); pixel
 * accuracy against the proportional font is a bench concern. */
static int measure_6px(const char *s, int len, void *ctx)
{
	(void)s;
	(void)ctx;
	return len * 6;
}

/* Copy one span into a NUL-terminated scratch buffer for string comparison. */
static const char *span_str(const m1_msgbox_line_t *ln, char *buf, size_t cap)
{
	int n = ln->len;
	if (n < 0) n = 0;
	if ((size_t)n >= cap) n = (int)cap - 1;
	memcpy(buf, ln->base + ln->off, (size_t)n);
	buf[n] = '\0';
	return buf;
}

/*************************** C R I T   1   T E S T S *************************/

/* Space-break: text flows into lines at word boundaries, in order, each within
 * the max width. */
static void test_wrap_breaks_at_spaces(void)
{
	m1_msgbox_line_t lines[M1_MSGBOX_MAX_LINES];
	char buf[128];
	/* max_width 60 => 10 chars per line with the 6px stub. */
	int n = m1_msgbox_wrap("the quick brown fox", 60, measure_6px, NULL,
	                       lines, 0, M1_MSGBOX_MAX_LINES);

	CHECK_EQ_INT(n, 2, "wrap: 'the quick brown fox' at w=60 -> 2 lines");
	CHECK_EQ_STR(span_str(&lines[0], buf, sizeof(buf)), "the quick",
	             "wrap: line 0 breaks at word boundary");
	CHECK_EQ_STR(span_str(&lines[1], buf, sizeof(buf)), "brown fox",
	             "wrap: line 1 continues in order");

	/* No emitted line exceeds the max width. */
	for (int i = 0; i < n; i++) {
		int w = measure_6px(lines[i].base + lines[i].off, lines[i].len, NULL);
		CHECK(w <= 60, "wrap: line fits within max width");
	}
}

/* A single word wider than a line is hard-broken across lines, not dropped. */
static void test_wrap_hard_breaks_long_token(void)
{
	m1_msgbox_line_t lines[M1_MSGBOX_MAX_LINES];
	char buf[128];
	/* 16-char token, w=60 => 10 chars per line. */
	int n = m1_msgbox_wrap("abcdefghijklmnop", 60, measure_6px, NULL,
	                       lines, 0, M1_MSGBOX_MAX_LINES);

	CHECK_EQ_INT(n, 2, "wrap: over-long token -> 2 hard-broken lines");
	CHECK_EQ_STR(span_str(&lines[0], buf, sizeof(buf)), "abcdefghij",
	             "wrap: hard-break fills the line");
	CHECK_EQ_STR(span_str(&lines[1], buf, sizeof(buf)), "klmnop",
	             "wrap: hard-break remainder preserved (not dropped)");
}

/* A mid-sentence long token hard-breaks but neighbours still wrap normally. */
static void test_wrap_mixed_long_token(void)
{
	m1_msgbox_line_t lines[M1_MSGBOX_MAX_LINES];
	char buf[128];
	/* "hi " (3) then 12-char token, w=60 => 10 chars/line. */
	int n = m1_msgbox_wrap("hi abcdefghijkl end", 60, measure_6px, NULL,
	                       lines, 0, M1_MSGBOX_MAX_LINES);

	/* "hi" fits; "hi abcdefghijkl" (15) too wide -> "hi" alone, then the token
	 * hard-breaks: "abcdefghij" + "kl end". */
	CHECK_EQ_INT(n, 3, "wrap: mixed long token -> 3 lines");
	CHECK_EQ_STR(span_str(&lines[0], buf, sizeof(buf)), "hi",
	             "wrap: short lead word on its own line");
	CHECK_EQ_STR(span_str(&lines[1], buf, sizeof(buf)), "abcdefghij",
	             "wrap: long token hard-break head");
	CHECK_EQ_STR(span_str(&lines[2], buf, sizeof(buf)), "kl end",
	             "wrap: token tail re-joins normal flow");
}

/* Appending (start_count) leaves earlier lines intact and returns cumulative. */
static void test_wrap_appends_from_start_count(void)
{
	m1_msgbox_line_t lines[M1_MSGBOX_MAX_LINES];
	char buf[128];
	lines[0].base = "SENTINEL";
	lines[0].off = 0;
	lines[0].len = 8;

	int n = m1_msgbox_wrap("one two", 60, measure_6px, NULL,
	                       lines, 1, M1_MSGBOX_MAX_LINES);

	CHECK_EQ_INT(n, 2, "wrap: append starts at start_count");
	CHECK_EQ_STR(span_str(&lines[0], buf, sizeof(buf)), "SENTINEL",
	             "wrap: pre-existing line untouched");
	CHECK_EQ_STR(span_str(&lines[1], buf, sizeof(buf)), "one two",
	             "wrap: new line appended after start_count");
}

/* Output never exceeds M1_MSGBOX_MAX_LINES; excess is truncated gracefully. */
static void test_wrap_caps_at_max_lines(void)
{
	m1_msgbox_line_t lines[M1_MSGBOX_MAX_LINES];
	/* Build a string of single chars separated by spaces: forces one word per
	 * line at w=6, far more than M1_MSGBOX_MAX_LINES. */
	char src[4 * (M1_MSGBOX_MAX_LINES + 10)];
	int p = 0;
	for (int i = 0; i < M1_MSGBOX_MAX_LINES + 8; i++) {
		src[p++] = (char)('a' + (i % 26));
		src[p++] = ' ';
	}
	src[p] = '\0';

	int n = m1_msgbox_wrap(src, 6, measure_6px, NULL,
	                       lines, 0, M1_MSGBOX_MAX_LINES);

	CHECK_EQ_INT(n, M1_MSGBOX_MAX_LINES,
	             "wrap: output capped at M1_MSGBOX_MAX_LINES");
}

/* NULL / empty input emits nothing and preserves the running count. */
static void test_wrap_empty_and_null(void)
{
	m1_msgbox_line_t lines[M1_MSGBOX_MAX_LINES];

	int n = m1_msgbox_wrap(NULL, 60, measure_6px, NULL, lines, 3,
	                       M1_MSGBOX_MAX_LINES);
	CHECK_EQ_INT(n, 3, "wrap: NULL input is a no-op");

	n = m1_msgbox_wrap("", 60, measure_6px, NULL, lines, 3,
	                   M1_MSGBOX_MAX_LINES);
	CHECK_EQ_INT(n, 3, "wrap: empty string is a no-op");

	n = m1_msgbox_wrap("   ", 60, measure_6px, NULL, lines, 3,
	                   M1_MSGBOX_MAX_LINES);
	CHECK_EQ_INT(n, 3, "wrap: whitespace-only string is a no-op");
}

/******************************* M A I N *************************************/

int main(void)
{
	printf("test_msgbox: message-box layout core\n");

	test_wrap_breaks_at_spaces();
	test_wrap_hard_breaks_long_token();
	test_wrap_mixed_long_token();
	test_wrap_appends_from_start_count();
	test_wrap_caps_at_max_lines();
	test_wrap_empty_and_null();

	printf("  %d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
