/* See COPYING.txt for license details. */

/*
 * test_flipper_ir.c  (HOST TEST)
 *
 * Round-trip tests for the Flipper .ir file layer (Task 1: append + rewrite).
 * Compiles the real m1_csrc/flipper_file.c + flipper_ir.c against the host FatFs
 * shim and verifies:
 *   1. A parsed and a raw signal appended to an existing header-only file
 *      re-parse with matching name/protocol/address/command/sample_count.
 *   2. flipper_ir_rewrite() can rename one signal, drop another, and preserve
 *      the rest — the foundation for the button rename/delete edit tasks.
 *
 * M1 Project — host test harness
 */

#include "flipper_ir.h"
#include "flipper_file.h"

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

/*************************** F I X T U R E S *********************************/

#define TEST_PATH   "m1_flipper_roundtrip.ir"
#define FREAD_PATH  "m1_fread_micro.ir"
#define BIGPATH     "m1_perf_big.ir"

/* Known raw sample stream (alternating mark/space microseconds). */
static const int32_t k_raw_samples[] = {
	9024, -4512, 579, -552, 579, -552, 579, -1665
};
#define K_RAW_COUNT ((uint16_t)(sizeof(k_raw_samples) / sizeof(k_raw_samples[0])))

static void make_parsed(flipper_ir_signal_t *s, const char *name,
                        uint8_t proto, uint16_t addr, uint16_t cmd)
{
	memset(s, 0, sizeof(*s));
	strncpy(s->name, name, FLIPPER_IR_NAME_MAX_LEN - 1);
	s->type = FLIPPER_IR_SIGNAL_PARSED;
	s->valid = true;
	s->parsed.protocol = proto;
	s->parsed.address = addr;
	s->parsed.command = cmd;
}

static void make_raw(flipper_ir_signal_t *s, const char *name)
{
	memset(s, 0, sizeof(*s));
	strncpy(s->name, name, FLIPPER_IR_NAME_MAX_LEN - 1);
	s->type = FLIPPER_IR_SIGNAL_RAW;
	s->valid = true;
	s->raw.frequency = 38000;
	s->raw.duty_cycle = 0.33f;
	s->raw.sample_count = K_RAW_COUNT;
	memcpy(s->raw.samples, k_raw_samples, sizeof(k_raw_samples));
}

/* Read every signal in the file into caller storage; returns the count. */
static uint16_t read_all(const char *path, flipper_ir_signal_t *out, uint16_t max)
{
	flipper_file_t ff;
	uint16_t n = 0;

	if (!flipper_ir_open(&ff, path))
		return 0;

	while (n < max && flipper_ir_read_signal(&ff, &out[n]))
		n++;

	ff_close(&ff);
	return n;
}

/*************************** T E S T S ***************************************/

/* Task 1a: append a parsed + a raw signal to a header-only file, re-parse. */
static void test_append_roundtrip(void)
{
	flipper_file_t ff;
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[8];
	uint16_t n;

	printf("test_append_roundtrip\n");

	f_unlink(TEST_PATH);

	/* Seed: header-only file, then one parsed signal via the normal write path. */
	CHECK(ff_open_write(&ff, TEST_PATH), "seed: open_write");
	CHECK(flipper_ir_write_header(&ff), "seed: write header");
	make_parsed(&sig, "Power", IRMP_NEC_PROTOCOL, 0x0007, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "seed: write Power");
	ff_close(&ff);

	/* Append a second parsed signal WITHOUT rewriting the header. */
	CHECK(flipper_ir_open_append(&ff, TEST_PATH), "append: open parsed");
	make_parsed(&sig, "Vol_Up", IRMP_SAMSUNG32_PROTOCOL, 0x0707, 0x00E0);
	CHECK(flipper_ir_write_signal(&ff, &sig), "append: write Vol_Up");
	ff_close(&ff);

	/* Append a raw signal (IRMP-undecodable fallback shape). */
	CHECK(flipper_ir_open_append(&ff, TEST_PATH), "append: open raw");
	make_raw(&sig, "RawBtn");
	CHECK(flipper_ir_write_signal(&ff, &sig), "append: write RawBtn");
	ff_close(&ff);

	/* Re-parse and assert all three survived with correct fields. */
	n = read_all(TEST_PATH, got, 8);
	CHECK_EQ_INT(n, 3, "reparse: signal count");

	CHECK_EQ_STR(got[0].name, "Power", "sig0 name");
	CHECK_EQ_INT(got[0].type, FLIPPER_IR_SIGNAL_PARSED, "sig0 type");
	CHECK_EQ_INT(got[0].parsed.protocol, IRMP_NEC_PROTOCOL, "sig0 protocol");
	CHECK_EQ_INT(got[0].parsed.address, 0x0007, "sig0 address");
	CHECK_EQ_INT(got[0].parsed.command, 0x0002, "sig0 command");

	CHECK_EQ_STR(got[1].name, "Vol_Up", "sig1 name");
	CHECK_EQ_INT(got[1].type, FLIPPER_IR_SIGNAL_PARSED, "sig1 type");
	CHECK_EQ_INT(got[1].parsed.protocol, IRMP_SAMSUNG32_PROTOCOL, "sig1 protocol");
	CHECK_EQ_INT(got[1].parsed.address, 0x0707, "sig1 address");
	CHECK_EQ_INT(got[1].parsed.command, 0x00E0, "sig1 command");

	CHECK_EQ_STR(got[2].name, "RawBtn", "sig2 name");
	CHECK_EQ_INT(got[2].type, FLIPPER_IR_SIGNAL_RAW, "sig2 type");
	CHECK_EQ_INT(got[2].raw.frequency, 38000, "sig2 frequency");
	CHECK_EQ_INT(got[2].raw.sample_count, K_RAW_COUNT, "sig2 sample_count");
	CHECK(memcmp(got[2].raw.samples, k_raw_samples, sizeof(k_raw_samples)) == 0,
	      "sig2 samples match");

	f_unlink(TEST_PATH);
}

/* Rewrite callback: rename signal 0, delete signal 1, keep the rest. */
static bool edit_cb(uint16_t index, flipper_ir_signal_t *sig, void *user)
{
	(void)user;
	if (index == 0) {
		strncpy(sig->name, "PowerToggle", FLIPPER_IR_NAME_MAX_LEN - 1);
		sig->name[FLIPPER_IR_NAME_MAX_LEN - 1] = '\0';
		return true;   /* keep, renamed */
	}
	if (index == 1)
		return false;  /* drop Vol_Up */
	return true;       /* keep RawBtn */
}

/* Task 1b: rewrite a 3-signal file -> rename one, drop one, preserve the rest. */
static void test_rewrite_edit(void)
{
	flipper_file_t ff;
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[8];
	uint16_t n;

	printf("test_rewrite_edit\n");

	f_unlink(TEST_PATH);

	/* Build a 3-signal file: Power (parsed), Vol_Up (parsed), RawBtn (raw). */
	CHECK(ff_open_write(&ff, TEST_PATH), "seed: open_write");
	CHECK(flipper_ir_write_header(&ff), "seed: write header");
	make_parsed(&sig, "Power", IRMP_NEC_PROTOCOL, 0x0007, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "seed: write Power");
	make_parsed(&sig, "Vol_Up", IRMP_SAMSUNG32_PROTOCOL, 0x0707, 0x00E0);
	CHECK(flipper_ir_write_signal(&ff, &sig), "seed: write Vol_Up");
	make_raw(&sig, "RawBtn");
	CHECK(flipper_ir_write_signal(&ff, &sig), "seed: write RawBtn");
	ff_close(&ff);

	/* Rewrite: rename #0, drop #1, keep #2. Atomic temp-file replace. */
	CHECK(flipper_ir_rewrite(TEST_PATH, edit_cb, NULL), "rewrite returns true");

	n = read_all(TEST_PATH, got, 8);
	CHECK_EQ_INT(n, 2, "rewrite: signal count drops to 2");

	CHECK_EQ_STR(got[0].name, "PowerToggle", "rewrite: sig0 renamed");
	CHECK_EQ_INT(got[0].type, FLIPPER_IR_SIGNAL_PARSED, "rewrite: sig0 type");
	CHECK_EQ_INT(got[0].parsed.protocol, IRMP_NEC_PROTOCOL, "rewrite: sig0 protocol");
	CHECK_EQ_INT(got[0].parsed.address, 0x0007, "rewrite: sig0 address");
	CHECK_EQ_INT(got[0].parsed.command, 0x0002, "rewrite: sig0 command");

	CHECK_EQ_STR(got[1].name, "RawBtn", "rewrite: sig1 is RawBtn");
	CHECK_EQ_INT(got[1].type, FLIPPER_IR_SIGNAL_RAW, "rewrite: sig1 type");
	CHECK_EQ_INT(got[1].raw.sample_count, K_RAW_COUNT, "rewrite: sig1 sample_count");
	CHECK(memcmp(got[1].raw.samples, k_raw_samples, sizeof(k_raw_samples)) == 0,
	      "rewrite: sig1 samples preserved");

	/* The temp file must not be left behind. */
	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH ".tmp"), 0, "temp file removed");

	f_unlink(TEST_PATH);
}

/* Task 3: a freshly created remote is a valid, empty, appendable .ir. This is
 * the file-level contract behind ir_custom_create_empty() — the UI/VKB/sanitize
 * layers are firmware-coupled and verified on-device. */
static void test_create_empty_remote(void)
{
	flipper_file_t ff;
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[4];
	uint16_t n;

	printf("test_create_empty_remote\n");

	f_unlink(TEST_PATH);

	/* Create an empty remote: header only, no signals. */
	CHECK(ff_open_write(&ff, TEST_PATH), "create: open_write");
	CHECK(flipper_ir_write_header(&ff), "create: write header");
	ff_close(&ff);

	/* A brand-new remote must parse as a valid .ir with zero buttons. */
	CHECK(flipper_ir_open(&ff, TEST_PATH), "create: reopens as valid .ir");
	ff_close(&ff);
	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH), 0, "create: 0 signals");

	/* And it must be immediately appendable (the learn flow adds buttons). */
	CHECK(flipper_ir_open_append(&ff, TEST_PATH), "create: open_append");
	make_parsed(&sig, "Power", IRMP_NEC_PROTOCOL, 0x0007, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "create: append first button");
	ff_close(&ff);

	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH), 1, "create: 1 signal after append");
	n = read_all(TEST_PATH, got, 4);
	CHECK_EQ_INT(n, 1, "create: reparse count");
	CHECK_EQ_STR(got[0].name, "Power", "create: appended button name");

	f_unlink(TEST_PATH);
}

/* Task 7: raw edge accumulator builds a signed mark/space sample stream that
 * round-trips through the writer/reader. This is the math behind the raw learn
 * fallback (the RX-event -> edge conversion is firmware, verified on-device). */
static void test_raw_accumulate(void)
{
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[2];
	flipper_file_t ff;
	uint16_t n, i;

	static const uint32_t durs[]   = { 9000, 4500, 560, 560, 560, 1690 };
	static const bool     marks[]  = { true, false, true, false, true, false };
	static const int32_t  expect[] = { 9000, -4500, 560, -560, 560, -1690 };
	const uint16_t ecount = 6;

	printf("test_raw_accumulate\n");
	f_unlink(TEST_PATH);

	flipper_ir_raw_begin(&sig, "RawLearned");
	for (i = 0; i < ecount; i++)
		CHECK(flipper_ir_raw_add_edge(&sig, durs[i], marks[i]), "raw: add edge");
	CHECK(flipper_ir_raw_finish(&sig, 38000, 0.33f), "raw: finish valid");

	CHECK_EQ_INT(sig.type, FLIPPER_IR_SIGNAL_RAW, "raw: type");
	CHECK_EQ_STR(sig.name, "RawLearned", "raw: name");
	CHECK_EQ_INT(sig.raw.frequency, 38000, "raw: frequency");
	CHECK_EQ_INT(sig.raw.sample_count, ecount, "raw: sample_count");
	CHECK(memcmp(sig.raw.samples, expect, sizeof(expect)) == 0, "raw: signed samples");

	/* Round-trip through the writer/reader. */
	CHECK(ff_open_write(&ff, TEST_PATH), "raw: open_write");
	CHECK(flipper_ir_write_header(&ff), "raw: header");
	CHECK(flipper_ir_write_signal(&ff, &sig), "raw: write signal");
	ff_close(&ff);

	n = read_all(TEST_PATH, got, 2);
	CHECK_EQ_INT(n, 1, "raw: reparse count");
	CHECK_EQ_INT(got[0].raw.sample_count, ecount, "raw: reparse sample_count");
	CHECK(memcmp(got[0].raw.samples, expect, sizeof(expect)) == 0, "raw: reparse samples");

	/* Capacity guard: edges beyond FLIPPER_IR_RAW_MAX_SAMPLES are rejected. */
	flipper_ir_raw_begin(&sig, "Full");
	for (i = 0; i < FLIPPER_IR_RAW_MAX_SAMPLES; i++)
		flipper_ir_raw_add_edge(&sig, 100, (i % 2) == 0);
	CHECK(!flipper_ir_raw_add_edge(&sig, 100, true), "raw: rejects past capacity");
	CHECK_EQ_INT(sig.raw.sample_count, FLIPPER_IR_RAW_MAX_SAMPLES, "raw: capped count");

	f_unlink(TEST_PATH);
}

/* Task 6: raw learn fallback frame-boundary logic. flipper_ir_raw_feed()
 * accumulates mark/space edges and, on the inter-frame timeout marker, either
 * finalizes the signal (>= min_samples) or discards it as noise. This is the
 * IRMP-UNKNOWN capture path minus the on-device RX event source (which supplies
 * the edge durations and the timeout marker). */
static void test_raw_feed_frame(void)
{
	flipper_ir_signal_t sig;
	uint16_t i;

	/* Alternating mark/space: a rising edge terminates a mark (stored +us),
	 * a falling edge terminates a space (stored -us). */
	static const uint32_t durs[] = { 9000, 4500, 560, 560, 560, 1690, 560 };
	static const bool     mark[] = { true, false, true, false, true, false, true };
	static const int32_t  exp[]  = { 9000, -4500, 560, -560, 560, -1690, 560 };
	const uint16_t ecount = 7;

	printf("test_raw_feed_frame\n");

	/* Too few edges before the timeout marker -> discarded as noise. */
	flipper_ir_raw_begin(&sig, "Btn");
	for (i = 0; i < 3; i++)
		CHECK_EQ_INT(flipper_ir_raw_feed(&sig, durs[i], mark[i], false, 8, 38000, 0.33f),
		             FLIPPER_IR_RAW_EDGE_ACCUMULATED, "feed: accumulate");
	CHECK_EQ_INT(flipper_ir_raw_feed(&sig, 0, false, true, 8, 38000, 0.33f),
	             FLIPPER_IR_RAW_FRAME_NOISE, "feed: noise on short frame");
	CHECK_EQ_INT(sig.raw.sample_count, 0, "feed: noise resets count");
	CHECK_EQ_STR(sig.name, "Btn", "feed: noise keeps name");
	CHECK(!sig.valid, "feed: noise not valid");

	/* Enough edges, then the timeout marker finalizes the signal. */
	flipper_ir_raw_begin(&sig, "Power");
	for (i = 0; i < ecount; i++)
		CHECK_EQ_INT(flipper_ir_raw_feed(&sig, durs[i], mark[i], false, 6, 38000, 0.33f),
		             FLIPPER_IR_RAW_EDGE_ACCUMULATED, "feed: accumulate valid");
	CHECK_EQ_INT(flipper_ir_raw_feed(&sig, 15501, true, true, 6, 38000, 0.33f),
	             FLIPPER_IR_RAW_FRAME_COMPLETE, "feed: complete on timeout");
	CHECK(sig.valid, "feed: complete valid");
	CHECK_EQ_INT(sig.raw.sample_count, ecount, "feed: complete count");
	CHECK_EQ_INT(sig.raw.frequency, 38000, "feed: complete frequency");
	CHECK(memcmp(sig.raw.samples, exp, sizeof(exp)) == 0, "feed: signed samples");

	/* Edges past the buffer are dropped without corrupting the count. */
	flipper_ir_raw_begin(&sig, "Full");
	for (i = 0; i < FLIPPER_IR_RAW_MAX_SAMPLES; i++)
		flipper_ir_raw_feed(&sig, 100, (i % 2) == 0, false, 6, 38000, 0.33f);
	CHECK_EQ_INT(flipper_ir_raw_feed(&sig, 100, true, false, 6, 38000, 0.33f),
	             FLIPPER_IR_RAW_EDGE_DROPPED, "feed: drop past capacity");
	CHECK_EQ_INT(sig.raw.sample_count, FLIPPER_IR_RAW_MAX_SAMPLES, "feed: capped");
}

/* Task 8: rename the button at a target index, preserving every other signal
 * (name/type/data/order). Reusable wrapper over flipper_ir_rewrite used by the
 * custom-remote button editor. */
static void test_rename_signal(void)
{
	flipper_file_t ff;
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[8];
	uint16_t n;

	printf("test_rename_signal\n");
	f_unlink(TEST_PATH);

	/* Seed: Power (parsed), Vol_Up (parsed), RawBtn (raw). */
	CHECK(ff_open_write(&ff, TEST_PATH), "rename seed: open_write");
	CHECK(flipper_ir_write_header(&ff), "rename seed: header");
	make_parsed(&sig, "Power", IRMP_NEC_PROTOCOL, 0x0007, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "rename seed: Power");
	make_parsed(&sig, "Vol_Up", IRMP_SAMSUNG32_PROTOCOL, 0x0707, 0x00E0);
	CHECK(flipper_ir_write_signal(&ff, &sig), "rename seed: Vol_Up");
	make_raw(&sig, "RawBtn");
	CHECK(flipper_ir_write_signal(&ff, &sig), "rename seed: RawBtn");
	ff_close(&ff);

	/* Rename the middle button; all others must be untouched. */
	CHECK(flipper_ir_rename_signal(TEST_PATH, 1, "Volume+"), "rename returns true");

	n = read_all(TEST_PATH, got, 8);
	CHECK_EQ_INT(n, 3, "rename: count unchanged");
	CHECK_EQ_STR(got[0].name, "Power", "rename: sig0 name intact");
	CHECK_EQ_STR(got[1].name, "Volume+", "rename: sig1 renamed");
	CHECK_EQ_INT(got[1].parsed.protocol, IRMP_SAMSUNG32_PROTOCOL, "rename: sig1 proto intact");
	CHECK_EQ_INT(got[1].parsed.address, 0x0707, "rename: sig1 addr intact");
	CHECK_EQ_INT(got[1].parsed.command, 0x00E0, "rename: sig1 cmd intact");
	CHECK_EQ_STR(got[2].name, "RawBtn", "rename: sig2 name intact");
	CHECK_EQ_INT(got[2].raw.sample_count, K_RAW_COUNT, "rename: sig2 samples intact");

	/* Out-of-range index is a no-op that still succeeds (file unchanged). */
	CHECK(flipper_ir_rename_signal(TEST_PATH, 9, "Nope"), "rename: OOB still true");
	n = read_all(TEST_PATH, got, 8);
	CHECK_EQ_STR(got[1].name, "Volume+", "rename: OOB left file unchanged");

	f_unlink(TEST_PATH);
}

/* Task 9: delete the button at a target index, preserving every other signal
 * and the file order; deleting the last remaining button leaves a valid,
 * header-only .ir. Reusable wrapper over flipper_ir_rewrite. */
static void test_delete_signal(void)
{
	flipper_file_t ff;
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[8];
	uint16_t n;

	printf("test_delete_signal\n");
	f_unlink(TEST_PATH);

	/* Seed: Power (parsed), Vol_Up (parsed), RawBtn (raw). */
	CHECK(ff_open_write(&ff, TEST_PATH), "delete seed: open_write");
	CHECK(flipper_ir_write_header(&ff), "delete seed: header");
	make_parsed(&sig, "Power", IRMP_NEC_PROTOCOL, 0x0007, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "delete seed: Power");
	make_parsed(&sig, "Vol_Up", IRMP_SAMSUNG32_PROTOCOL, 0x0707, 0x00E0);
	CHECK(flipper_ir_write_signal(&ff, &sig), "delete seed: Vol_Up");
	make_raw(&sig, "RawBtn");
	CHECK(flipper_ir_write_signal(&ff, &sig), "delete seed: RawBtn");
	ff_close(&ff);

	/* Drop the middle button; count falls by 1, neighbours intact + reordered. */
	CHECK(flipper_ir_delete_signal(TEST_PATH, 1), "delete returns true");
	n = read_all(TEST_PATH, got, 8);
	CHECK_EQ_INT(n, 2, "delete: count drops to 2");
	CHECK_EQ_STR(got[0].name, "Power", "delete: sig0 preserved");
	CHECK_EQ_INT(got[0].parsed.command, 0x0002, "delete: sig0 cmd intact");
	CHECK_EQ_STR(got[1].name, "RawBtn", "delete: sig1 is now RawBtn");
	CHECK_EQ_INT(got[1].raw.sample_count, K_RAW_COUNT, "delete: raw samples intact");

	/* Out-of-range index is a no-op (still true, nothing removed). */
	CHECK(flipper_ir_delete_signal(TEST_PATH, 9), "delete: OOB still true");
	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH), 2, "delete: OOB count unchanged");

	/* Delete down to zero: the file stays a valid, appendable header-only .ir. */
	CHECK(flipper_ir_delete_signal(TEST_PATH, 0), "delete: remove first of two");
	CHECK(flipper_ir_delete_signal(TEST_PATH, 0), "delete: remove last button");
	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH), 0, "delete: empty remote");
	CHECK(flipper_ir_open(&ff, TEST_PATH), "delete: empty file still valid");
	ff_close(&ff);
	CHECK(flipper_ir_open_append(&ff, TEST_PATH), "delete: empty file appendable");
	make_parsed(&sig, "Re", IRMP_NEC_PROTOCOL, 0x0001, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "delete: append after empty");
	ff_close(&ff);
	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH), 1, "delete: re-append works");

	f_unlink(TEST_PATH);
}

/* Task 1 (buffered-reader groundwork): the host FatFs shim gains a real f_read
 * over stdio fread plus a call counter, so the buffered ff_read_line() rewrite
 * can be exercised on the PC and its read-call count asserted. Nothing in
 * flipper_file.c calls f_read yet — this proves the shim addition works in
 * isolation: chunked reads return the right bytes with a short count at EOF, and
 * the counter ticks exactly once per call (the seam the perf guard later relies
 * on). */
static void test_f_read_chunks(void)
{
	FIL   fp;
	FILE *w;
	char  buf[4];
	UINT  br;

	printf("test_f_read_chunks\n");

	/* Seed a known 10-byte payload straddling the 4-byte read window. */
	w = fopen(FREAD_PATH, "wb");
	CHECK(w != NULL, "f_read: seed open");
	CHECK_EQ_INT(fwrite("ABCDEFGHIJ", 1, 10, w), 10, "f_read: seed write");
	fclose(w);

	CHECK(f_open(&fp, FREAD_PATH, FA_READ | FA_OPEN_EXISTING) == FR_OK,
	      "f_read: open");
	ff_shim_read_calls_reset();
	CHECK_EQ_INT(ff_shim_read_calls(), 0, "f_read: counter reset to 0");

	/* Full window #1: ABCD. */
	CHECK(f_read(&fp, buf, 4, &br) == FR_OK, "f_read: call 1 FR_OK");
	CHECK_EQ_INT(br, 4, "f_read: call 1 read 4");
	CHECK(memcmp(buf, "ABCD", 4) == 0, "f_read: call 1 bytes");

	/* Full window #2: EFGH. */
	CHECK(f_read(&fp, buf, 4, &br) == FR_OK, "f_read: call 2 FR_OK");
	CHECK_EQ_INT(br, 4, "f_read: call 2 read 4");
	CHECK(memcmp(buf, "EFGH", 4) == 0, "f_read: call 2 bytes");

	/* Short window at EOF: IJ (2 of 4 requested), still FR_OK. */
	CHECK(f_read(&fp, buf, 4, &br) == FR_OK, "f_read: call 3 FR_OK");
	CHECK_EQ_INT(br, 2, "f_read: call 3 short count at EOF");
	CHECK(memcmp(buf, "IJ", 2) == 0, "f_read: call 3 bytes");

	/* Past EOF: zero bytes, still FR_OK (real FatFs contract). */
	CHECK(f_read(&fp, buf, 4, &br) == FR_OK, "f_read: call 4 FR_OK at EOF");
	CHECK_EQ_INT(br, 0, "f_read: call 4 zero at EOF");

	/* Counter ticked exactly once per f_read call (4 calls above). */
	CHECK_EQ_INT(ff_shim_read_calls(), 4, "f_read: counted one per call");

	f_close(&fp);
	f_unlink(FREAD_PATH);
}

/*----------------------------------------------------------------------------*
 * Task 2 — ff_read_line() contract characterization.
 *
 * These pin ff_read_line()'s observable behavior on the CURRENT f_gets reader
 * so the Task 3 buffered rewrite is guarded for byte-for-byte parity. Each case
 * makes a falsifiable assertion about line assembly, trailing-whitespace
 * stripping, comment handling, and EOF — the exact seams the raw data: sample
 * counting and ff_is_separator() logic depend on.
 *----------------------------------------------------------------------------*/

/* A physical line longer than the line buffer is delivered in FF_LINE_BUF_LEN-1
 * sized pieces across successive calls (f_gets cap), never dropped. This locks
 * the split point that raw data: sample counting relies on. */
static void test_read_long_line_splits_at_buf(void)
{
	flipper_file_t ff;
	FILE *w;
	int   i;
	const int total = FF_LINE_BUF_LEN + 88;   /* 600: overruns the 512 buffer */

	printf("test_read_long_line_splits_at_buf\n");
	f_unlink(TEST_PATH);

	w = fopen(TEST_PATH, "wb");
	CHECK(w != NULL, "longline: seed open");
	for (i = 0; i < total; i++)
		fputc('A', w);
	fputc('\n', w);
	fputs("next: line\n", w);
	fclose(w);

	CHECK(ff_open(&ff, TEST_PATH), "longline: open");

	/* Chunk 1: capped at exactly FF_LINE_BUF_LEN-1 characters. */
	CHECK(ff_read_line(&ff), "longline: read 1");
	CHECK_EQ_INT((long)strlen(ff.line), FF_LINE_BUF_LEN - 1, "longline: chunk1 == 511");

	/* Chunk 2: the remainder continues on the next call, not lost. */
	CHECK(ff_read_line(&ff), "longline: read 2 (continuation)");
	CHECK_EQ_INT((long)strlen(ff.line), total - (FF_LINE_BUF_LEN - 1),
	             "longline: chunk2 == remainder");

	/* The following real line still parses normally. */
	CHECK(ff_read_line(&ff), "longline: read 3");
	CHECK(ff_parse_kv(&ff), "longline: read 3 kv");
	CHECK_EQ_STR(ff_get_key(&ff), "next", "longline: key after long line");
	CHECK_EQ_STR(ff_get_value(&ff), "line", "longline: value after long line");

	CHECK(!ff_read_line(&ff), "longline: eof");
	CHECK(ff.eof, "longline: eof flag");

	ff_close(&ff);
	f_unlink(TEST_PATH);
}

/* CRLF (\r\n) endings are stripped identically to LF: no trailing CR survives
 * into the parsed value. */
static void test_read_crlf_stripped_like_lf(void)
{
	flipper_file_t ff;
	FILE *w;

	printf("test_read_crlf_stripped_like_lf\n");
	f_unlink(TEST_PATH);

	w = fopen(TEST_PATH, "wb");
	CHECK(w != NULL, "crlf: seed open");
	fputs("crlf_key: 1\r\n", w);   /* CRLF ending */
	fputs("lf_key: 2\n", w);       /* LF ending  */
	fclose(w);

	CHECK(ff_open(&ff, TEST_PATH), "crlf: open");

	CHECK(ff_read_line(&ff), "crlf: read 1");
	CHECK(ff_parse_kv(&ff), "crlf: read 1 kv");
	CHECK_EQ_STR(ff_get_key(&ff), "crlf_key", "crlf: key (CR stripped)");
	CHECK_EQ_STR(ff_get_value(&ff), "1", "crlf: value carries no trailing CR");

	CHECK(ff_read_line(&ff), "crlf: read 2");
	CHECK(ff_parse_kv(&ff), "crlf: read 2 kv");
	CHECK_EQ_STR(ff_get_key(&ff), "lf_key", "crlf: LF key");
	CHECK_EQ_STR(ff_get_value(&ff), "2", "crlf: LF value");

	CHECK(!ff_read_line(&ff), "crlf: eof");

	ff_close(&ff);
	f_unlink(TEST_PATH);
}

/* The final line with no trailing newline is returned exactly once, then EOF is
 * clean (no phantom repeat, no lost line). */
static void test_read_no_trailing_newline(void)
{
	flipper_file_t ff;
	FILE *w;

	printf("test_read_no_trailing_newline\n");
	f_unlink(TEST_PATH);

	w = fopen(TEST_PATH, "wb");
	CHECK(w != NULL, "nonl: seed open");
	fputs("first: 1\nlast: 2", w);   /* last line: NO trailing newline */
	fclose(w);

	CHECK(ff_open(&ff, TEST_PATH), "nonl: open");

	CHECK(ff_read_line(&ff), "nonl: read 1");
	CHECK(ff_parse_kv(&ff), "nonl: read 1 kv");
	CHECK_EQ_STR(ff_get_key(&ff), "first", "nonl: first key");

	CHECK(ff_read_line(&ff), "nonl: read 2 (newline-less last line returned once)");
	CHECK(ff_parse_kv(&ff), "nonl: read 2 kv");
	CHECK_EQ_STR(ff_get_key(&ff), "last", "nonl: last key");
	CHECK_EQ_STR(ff_get_value(&ff), "2", "nonl: last value");

	CHECK(!ff_read_line(&ff), "nonl: clean eof after last line");
	CHECK(ff.eof, "nonl: eof flag set");

	ff_close(&ff);
	f_unlink(TEST_PATH);
}

/* Comment/separator lines (#) are RETURNED (so ff_is_separator stays true);
 * blank and whitespace-only lines are skipped. */
static void test_read_comment_returned_blanks_skipped(void)
{
	flipper_file_t ff;
	FILE *w;

	printf("test_read_comment_returned_blanks_skipped\n");
	f_unlink(TEST_PATH);

	w = fopen(TEST_PATH, "wb");
	CHECK(w != NULL, "cmt: seed open");
	fputs("key1: a\n", w);
	fputs("\n", w);           /* blank line          -> skipped */
	fputs("   \t \n", w);      /* whitespace-only     -> skipped */
	fputs("#\n", w);           /* separator (as written by ff_write_separator) */
	fputs("key2: b\n", w);
	fclose(w);

	CHECK(ff_open(&ff, TEST_PATH), "cmt: open");

	CHECK(ff_read_line(&ff), "cmt: read 1 (key1)");
	CHECK(!ff_is_separator(&ff), "cmt: key1 not a separator");
	CHECK_EQ_STR(ff.line, "key1: a", "cmt: key1 line");

	/* Blank + whitespace-only lines skipped inside one call; the '#' is returned. */
	CHECK(ff_read_line(&ff), "cmt: read 2 (separator survives blank skip)");
	CHECK(ff_is_separator(&ff), "cmt: '#' line is a separator");
	CHECK_EQ_STR(ff.line, "#", "cmt: separator line content");
	CHECK(!ff_parse_kv(&ff), "cmt: separator is not key-value");

	CHECK(ff_read_line(&ff), "cmt: read 3 (key2)");
	CHECK(!ff_is_separator(&ff), "cmt: key2 not a separator");
	CHECK(ff_parse_kv(&ff), "cmt: key2 kv");
	CHECK_EQ_STR(ff_get_key(&ff), "key2", "cmt: key2 key");

	CHECK(!ff_read_line(&ff), "cmt: eof");

	ff_close(&ff);
	f_unlink(TEST_PATH);
}

/* Empty and header-only files reach EOF cleanly with no over-read past EOF. */
static void test_read_empty_and_header_only(void)
{
	flipper_file_t ff;
	FILE *w;

	printf("test_read_empty_and_header_only\n");

	/* --- Empty file: first read is EOF; repeated reads stay false. --- */
	f_unlink(TEST_PATH);
	w = fopen(TEST_PATH, "wb");
	CHECK(w != NULL, "empty: seed open");
	fclose(w);   /* zero bytes */

	CHECK(ff_open(&ff, TEST_PATH), "empty: open");
	CHECK(!ff_read_line(&ff), "empty: first read is EOF");
	CHECK(ff.eof, "empty: eof flag");
	CHECK(!ff_read_line(&ff), "empty: no over-read past EOF");
	ff_close(&ff);

	/* --- Header-only file: two lines, then a clean EOF (no phantom signal). --- */
	f_unlink(TEST_PATH);
	w = fopen(TEST_PATH, "wb");
	CHECK(w != NULL, "hdr: seed open");
	fputs("Filetype: IR signals file\n", w);
	fputs("Version: 1\n", w);
	fclose(w);

	CHECK(ff_open(&ff, TEST_PATH), "hdr: open");
	CHECK(ff_read_line(&ff), "hdr: read Filetype");
	CHECK(ff_parse_kv(&ff), "hdr: Filetype kv");
	CHECK_EQ_STR(ff_get_key(&ff), "Filetype", "hdr: Filetype key");
	CHECK(ff_read_line(&ff), "hdr: read Version");
	CHECK(ff_parse_kv(&ff), "hdr: Version kv");
	CHECK_EQ_STR(ff_get_key(&ff), "Version", "hdr: Version key");
	CHECK_EQ_STR(ff_get_value(&ff), "1", "hdr: Version value");
	CHECK(!ff_read_line(&ff), "hdr: clean EOF after header");
	CHECK(ff.eof, "hdr: eof flag");
	ff_close(&ff);

	f_unlink(TEST_PATH);
}

/*----------------------------------------------------------------------------*
 * Task 3 — chunk-boundary cases specific to the buffered reader (reference
 * FF_READ_CHUNK). They prove a refill in the middle of a line reassembles it
 * without corruption or loss.
 *----------------------------------------------------------------------------*/

/* A KV value longer than FF_READ_CHUNK (but within FF_LINE_BUF_LEN) forces a
 * refill partway through the value; it must reassemble intact. A short leading
 * line makes the 256-byte crossing land in the middle of the value. */
static void test_read_field_straddles_chunk(void)
{
	flipper_file_t ff;
	FILE *w;
	int   i;
	const int vlen = FF_READ_CHUNK + 44;   /* 300: value overruns one chunk */

	printf("test_read_field_straddles_chunk\n");
	f_unlink(TEST_PATH);

	w = fopen(TEST_PATH, "wb");
	CHECK(w != NULL, "straddle: seed open");
	fputs("a: b\n", w);            /* short first line -> crossing lands mid-value */
	fputs("key: ", w);
	for (i = 0; i < vlen; i++)
		fputc('v', w);
	fputc('\n', w);
	fputs("tail: end\n", w);
	fclose(w);

	CHECK(ff_open(&ff, TEST_PATH), "straddle: open");

	CHECK(ff_read_line(&ff), "straddle: read 1");
	CHECK(ff_parse_kv(&ff), "straddle: read 1 kv");
	CHECK_EQ_STR(ff_get_key(&ff), "a", "straddle: first key");

	/* The long value crosses the 256-byte refill boundary intact. */
	CHECK(ff_read_line(&ff), "straddle: read 2 (long value)");
	CHECK(ff_parse_kv(&ff), "straddle: read 2 kv");
	CHECK_EQ_STR(ff_get_key(&ff), "key", "straddle: long-line key");
	CHECK_EQ_INT((long)strlen(ff_get_value(&ff)), vlen, "straddle: full value length");
	CHECK(strspn(ff_get_value(&ff), "v") == (size_t)vlen,
	      "straddle: value all 'v' (no corruption across refill)");

	CHECK(ff_read_line(&ff), "straddle: read 3");
	CHECK(ff_parse_kv(&ff), "straddle: read 3 kv");
	CHECK_EQ_STR(ff_get_key(&ff), "tail", "straddle: tail key");
	CHECK_EQ_STR(ff_get_value(&ff), "end", "straddle: tail value");

	CHECK(!ff_read_line(&ff), "straddle: eof");

	ff_close(&ff);
	f_unlink(TEST_PATH);
}

/* A line whose content length equals FF_READ_CHUNK exactly: the chunk drains
 * precisely at the line's last content byte and the newline lands in the next
 * refill. The parsed value must still be intact. */
static void test_read_line_equals_chunk(void)
{
	flipper_file_t ff;
	FILE *w;
	int   i;
	const int vlen = FF_READ_CHUNK - 3;    /* "k: " (3) + vlen == FF_READ_CHUNK */

	printf("test_read_line_equals_chunk\n");
	f_unlink(TEST_PATH);

	w = fopen(TEST_PATH, "wb");
	CHECK(w != NULL, "eqchunk: seed open");
	fputs("k: ", w);               /* 3 bytes */
	for (i = 0; i < vlen; i++)
		fputc('x', w);             /* content length now == FF_READ_CHUNK */
	fputc('\n', w);                /* newline is the byte after the chunk */
	fputs("z: 9\n", w);
	fclose(w);

	CHECK(ff_open(&ff, TEST_PATH), "eqchunk: open");

	CHECK(ff_read_line(&ff), "eqchunk: read 1 (== chunk)");
	CHECK(ff_parse_kv(&ff), "eqchunk: read 1 kv");
	CHECK_EQ_STR(ff_get_key(&ff), "k", "eqchunk: key");
	CHECK_EQ_INT((long)strlen(ff_get_value(&ff)), vlen, "eqchunk: value length");
	CHECK(strspn(ff_get_value(&ff), "x") == (size_t)vlen, "eqchunk: value all 'x'");

	CHECK(ff_read_line(&ff), "eqchunk: read 2 (line after exact boundary)");
	CHECK(ff_parse_kv(&ff), "eqchunk: read 2 kv");
	CHECK_EQ_STR(ff_get_key(&ff), "z", "eqchunk: next key");
	CHECK_EQ_STR(ff_get_value(&ff), "9", "eqchunk: next value");

	CHECK(!ff_read_line(&ff), "eqchunk: eof");

	ff_close(&ff);
	f_unlink(TEST_PATH);
}

/*----------------------------------------------------------------------------*
 * Task 4 — perf guard. With the buffered reader in place, a full parse of a
 * tv.ir-sized (~90-record) file must issue O(bytes / FF_READ_CHUNK) f_read
 * calls, never one per byte. This fails loudly if ff_read_line ever reverts to
 * a per-character read.
 *----------------------------------------------------------------------------*/
static void test_read_call_count_is_chunked(void)
{
	flipper_file_t      ff;
	flipper_ir_signal_t sig;
	FILINFO             fno;
	char                nm[16];
	int                 i;
	const int           NREC = 90;
	unsigned long       calls, ceil_chunks, bytes;
	uint16_t            n;

	printf("test_read_call_count_is_chunked\n");
	f_unlink(BIGPATH);

	/* Synthesize a ~90-record parsed .ir (tv.ir-sized). */
	CHECK(ff_open_write(&ff, BIGPATH), "perf: open_write");
	CHECK(flipper_ir_write_header(&ff), "perf: header");
	for (i = 0; i < NREC; i++)
	{
		snprintf(nm, sizeof(nm), "Fn_%02d", i);
		make_parsed(&sig, nm, IRMP_NEC_PROTOCOL,
		            (uint16_t)(0x0700 + i), (uint16_t)(0x00E0 + i));
		CHECK(flipper_ir_write_signal(&ff, &sig), "perf: write record");
	}
	ff_close(&ff);

	CHECK(f_stat(BIGPATH, &fno) == FR_OK, "perf: stat file");
	bytes = (unsigned long)fno.fsize;

	/* Full parse to EOF under the read-call counter. */
	ff_shim_read_calls_reset();
	n = flipper_ir_count_signals(BIGPATH);
	calls = ff_shim_read_calls();

	printf("  (perf: %lu bytes, %lu f_read calls)\n", bytes, calls);

	CHECK_EQ_INT(n, NREC, "perf: exact record count");

	/* The file must actually span several chunks, else the guard is vacuous. */
	CHECK(bytes > (unsigned long)(4 * FF_READ_CHUNK), "perf: file spans several chunks");

	/* Upper bound: ~ceil(bytes / chunk) refills plus slack for the EOF read. */
	ceil_chunks = (bytes + FF_READ_CHUNK - 1) / FF_READ_CHUNK;
	CHECK(calls <= ceil_chunks + 2, "perf: O(bytes / FF_READ_CHUNK) f_read calls");

	/* Hard guard: byte-by-byte would be ~bytes calls; even 8 bytes/call fails. */
	CHECK(calls < bytes / 8, "perf: not reading byte-by-byte");

	f_unlink(BIGPATH);
}

int main(void)
{
	printf("== Flipper .ir host round-trip tests ==\n");

	test_f_read_chunks();
	test_read_long_line_splits_at_buf();
	test_read_crlf_stripped_like_lf();
	test_read_no_trailing_newline();
	test_read_comment_returned_blanks_skipped();
	test_read_empty_and_header_only();
	test_read_field_straddles_chunk();
	test_read_line_equals_chunk();
	test_read_call_count_is_chunked();
	test_append_roundtrip();
	test_rewrite_edit();
	test_create_empty_remote();
	test_raw_accumulate();
	test_raw_feed_frame();
	test_rename_signal();
	test_delete_signal();

	printf("== %d checks, %d failures ==\n", g_checks, g_failures);
	return (g_failures == 0) ? 0 : 1;
}
