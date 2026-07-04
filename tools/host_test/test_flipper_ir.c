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

int main(void)
{
	printf("== Flipper .ir host round-trip tests ==\n");

	test_append_roundtrip();
	test_rewrite_edit();

	printf("== %d checks, %d failures ==\n", g_checks, g_failures);
	return (g_failures == 0) ? 0 : 1;
}
