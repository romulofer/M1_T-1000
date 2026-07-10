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
	"#\n"
	"name: Power\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 9000 4500 560 560 560 1690\n"
	"#\n"
	"name: Off\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\n"
	"data: 9000 4500 560 560 560 1690 560 40000\n"
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

static int32_t *g_rawbuf;   /* set in main() to the buffer passed to stream */
static bool raw_probe_cb(void *ctx, const ir_universal_cmd_t *cmd, uint16_t idx)
{
	(void)idx;
	struct RP { uint16_t count; bool is_raw; int32_t first; int32_t last; } *r = ctx;
	r->count  = cmd->raw_count;
	r->is_raw = cmd->is_raw;
	if (cmd->raw_count > 0) { r->first = g_rawbuf[0]; r->last = g_rawbuf[cmd->raw_count - 1]; }
	return true;
}

int main(void)
{
	printf("== universal-remote brute-force stream test ==\n");
	write_fixture(FIX_LIB, k_lib);

	/* Count pass (cb == NULL) counts only matching records. */
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Power", NULL, NULL, NULL, 0), 4, "count Power (raw now included)");
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Vol_up", NULL, NULL, NULL, 0), 2, "count Vol_up");
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Mute", NULL, NULL, NULL, 0), 1, "count Mute");
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Ch_next", NULL, NULL, NULL, 0), 0, "count missing");

	/* Fire pass invokes cb only on matches, in order. */
	capture_t cap = {0};
	uint16_t n = uremote_bf_stream(FIX_LIB, "Power", capture_cb, &cap, NULL, 0);
	CHECK_EQ_INT(n, 4, "fire Power returns 4");
	CHECK_EQ_INT(cap.calls, 4, "cb called 4x");
	CHECK(strcmp(cap.names[0], "Power") == 0, "match name 0");
	CHECK(strcmp(cap.names[2], "Power") == 0, "match name 2");

	/* Early abort: cb returns false after the 2nd call. */
	capture_t cap2 = {0};
	cap2.abort_after = 2;
	n = uremote_bf_stream(FIX_LIB, "Power", capture_cb, &cap2, NULL, 0);
	CHECK_EQ_INT(cap2.calls, 2, "abort stops at 2 calls");
	CHECK_EQ_INT(n, 2, "abort returns 2");

	/* "IR signals file" header is also accepted. */
	char sig[2048];
	strcpy(sig, k_lib);
	memcpy(sig, "Filetype: IR signals file", 25); /* same length as library header */
	write_fixture("uremote_sig.ir", sig);
	CHECK_EQ_INT(uremote_bf_stream("uremote_sig.ir", "Power", NULL, NULL, NULL, 0), 4,
	             "signals-file header accepted");

	/* --- Raw capture + fire (AC support) --- */
	int32_t rawbuf[64];
	uint16_t rawcap = 64;
	g_rawbuf = rawbuf;
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Off", NULL, NULL, rawbuf, rawcap), 1,
	             "raw Off counted");
	struct { uint16_t count; bool is_raw; int32_t first; int32_t last; } rawcap_res = {0};
	n = uremote_bf_stream(FIX_LIB, "Off", raw_probe_cb, &rawcap_res, rawbuf, rawcap);
	CHECK_EQ_INT(n, 1, "raw Off fired once");
	CHECK(rawcap_res.is_raw, "Off record flagged raw");
	CHECK_EQ_INT(rawcap_res.count, 8, "raw Off sample count");
	CHECK_EQ_INT((int)rawcap_res.first, 9000, "first sample = 9000");
	CHECK_EQ_INT((int)rawcap_res.last, 40000, "last sample = 40000 (int32, no 16-bit clip)");

	/* clamp path: cap smaller than the record's sample count */
	struct { uint16_t count; bool is_raw; int32_t first; int32_t last; } clamp_res = {0};
	uremote_bf_stream(FIX_LIB, "Off", raw_probe_cb, &clamp_res, rawbuf, 3);
	CHECK_EQ_INT(clamp_res.count, 3, "raw clamp: raw_count == cap");
	CHECK_EQ_INT((int)rawbuf[2], 560, "raw clamp: 3rd sample stored");

	remove(FIX_LIB);
	remove("uremote_sig.ir");

	printf("  %d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
