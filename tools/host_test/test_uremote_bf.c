/* See COPYING.txt for license details. */

/*
 * test_uremote_bf.c  (HOST TEST)
 *
 * Verifies the streaming, name-filtered universal-remote brute-force iterator
 * (m1_uremote_bf.c) against the real m1_csrc/flipper_file.c using the FatFs
 * host shim. Writes a small "IR library file" fixture with several brands'
 * Power / Vol_up records plus noise, then checks that uremote_bf_stream():
 *   - counts only records whose name matches the target (cb == NULL),
 *   - invokes the callback exactly on matching records, in file order,
 *   - stops early when the callback returns false,
 *   - accepts both "IR library file" and "IR signals file" headers.
 */

#include "m1_uremote_bf.h"
#include "flipper_file.h"

#include <stdio.h>
#include <string.h>

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

#define FIX_LIB     "uremote_lib.ir"

/* 3x Power, 2x Vol_up, 1x Mute, interleaved. */
static const char *k_lib =
	"Filetype: IR library file\n"
	"Version: 1\n"
	"#\n"
	"name: Power\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00 00\n"
	"#\n"
	"name: Vol_up\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 02 00 00 00\n"
	"#\n"
	"name: Power\ntype: parsed\nprotocol: SIRC\naddress: 01 00 00 00\ncommand: 15 00 00 00\n"
	"#\n"
	"name: Mute\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 0D 00 00 00\n"
	"#\n"
	"name: Vol_up\ntype: parsed\nprotocol: SIRC\naddress: 01 00 00 00\ncommand: 12 00 00 00\n"
	"#\n"
	"name: Power\ntype: parsed\nprotocol: Samsung32\naddress: 07 07 00 00\ncommand: 02 00 00 00\n"
	"#\n";

static void write_fixture(const char *path, const char *body)
{
	FILE *f = fopen(path, "wb");
	fwrite(body, 1, strlen(body), f);
	fclose(f);
}

/* Callback that records the names it is handed, and can abort after N calls. */
typedef struct {
	int calls;
	int abort_after;   /* return false once calls == abort_after; 0 = never */
	char names[16][IR_UNIVERSAL_NAME_MAX_LEN];
} capture_t;

static bool capture_cb(void *ctx, const ir_universal_cmd_t *cmd, uint16_t idx)
{
	capture_t *c = (capture_t *)ctx;
	(void)idx;
	if (c->calls < 16)
		strncpy(c->names[c->calls], cmd->name, IR_UNIVERSAL_NAME_MAX_LEN - 1);
	c->calls++;
	if (c->abort_after && c->calls >= c->abort_after)
		return false;
	return true;
}

int main(void)
{
	printf("== universal-remote brute-force stream test ==\n");
	write_fixture(FIX_LIB, k_lib);

	/* Count pass (cb == NULL) counts only matching records. */
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Power", NULL, NULL), 3, "count Power");
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Vol_up", NULL, NULL), 2, "count Vol_up");
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Mute", NULL, NULL), 1, "count Mute");
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Ch_next", NULL, NULL), 0, "count missing");

	/* Fire pass invokes cb only on matches, in order. */
	capture_t cap = {0};
	uint16_t n = uremote_bf_stream(FIX_LIB, "Power", capture_cb, &cap);
	CHECK_EQ_INT(n, 3, "fire Power returns 3");
	CHECK_EQ_INT(cap.calls, 3, "cb called 3x");
	CHECK(strcmp(cap.names[0], "Power") == 0, "match name 0");
	CHECK(strcmp(cap.names[2], "Power") == 0, "match name 2");

	/* Early abort: cb returns false after the 2nd call. */
	capture_t cap2 = {0};
	cap2.abort_after = 2;
	n = uremote_bf_stream(FIX_LIB, "Power", capture_cb, &cap2);
	CHECK_EQ_INT(cap2.calls, 2, "abort stops at 2 calls");
	CHECK_EQ_INT(n, 2, "abort returns 2");

	/* "IR signals file" header is also accepted. */
	char sig[2048];
	strcpy(sig, k_lib);
	memcpy(sig, "Filetype: IR signals file", 25); /* same length as library header */
	write_fixture("uremote_sig.ir", sig);
	CHECK_EQ_INT(uremote_bf_stream("uremote_sig.ir", "Power", NULL, NULL), 3,
	             "signals-file header accepted");

	remove(FIX_LIB);
	remove("uremote_sig.ir");

	printf("  %d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
