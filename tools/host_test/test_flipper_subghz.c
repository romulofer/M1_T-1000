/* See COPYING.txt for license details. */

/*
 * test_flipper_subghz.c  (HOST TEST)
 *
 * Unit tests for the Flipper .sub file layer (m1_csrc/flipper_subghz.c) and its
 * two mapping helpers. The real module is compiled against the host FatFs shim
 * plus a stub stm32h5xx_hal.h, so the REAL S_M1_SubGHz_Band /
 * S_M1_SubGHz_Modulation enums from m1_sub_ghz.h are the ones under test.
 *
 * Verifies:
 *   - RAW save -> load round-trip, including multi-line RAW_Data splitting
 *   - RAW sample clamping to int16 and the FLIPPER_SUBGHZ_RAW_MAX_SAMPLES cap
 *   - parsed (Key) save -> load round-trip: frequency, bit count, 64-bit key, TE
 *   - files that must be rejected: missing, no header, empty RAW, keyless parsed
 *   - flipper_subghz_preset_to_modulation() over the shipped preset strings
 *   - flipper_subghz_freq_to_band() at every band boundary
 *
 * M1 Project — host test harness
 */

#include "flipper_subghz.h"
#include "m1_sub_ghz.h"

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
		long long _a = (long long)(actual);                                    \
		long long _e = (long long)(expected);                                  \
		if (_a != _e) {                                                        \
			g_failures++;                                                      \
			printf("  FAIL: %s  (got %lld, want %lld)  (%s:%d)\n",             \
			       (msg), _a, _e, __FILE__, __LINE__);                         \
		}                                                                      \
	} while (0)

#define CHECK_EQ_STR(actual, expected, msg)                                    \
	do {                                                                       \
		g_checks++;                                                            \
		if (strcmp((actual), (expected)) != 0) {                               \
			g_failures++;                                                      \
			printf("  FAIL: %s  (got \"%s\", want \"%s\")  (%s:%d)\n",         \
			       (msg), (actual), (expected), __FILE__, __LINE__);           \
		}                                                                      \
	} while (0)

/*************************** F I X T U R E S *********************************/

#define TEST_PATH   "m1_flipper_roundtrip.sub"

#define OOK_PRESET  "FuriHalSubGhzPresetOok650Async"

/* Write a fixture file verbatim (host stdio, bypassing the module under test). */
static void write_file(const char *path, const char *contents)
{
	FILE *f = fopen(path, "wb");
	if (f == NULL) {
		g_failures++;
		printf("  FAIL: fixture fopen(%s)\n", path);
		return;
	}
	fwrite(contents, 1, strlen(contents), f);
	fclose(f);
}

/* Deterministic alternating mark/space pattern for sample index i. */
static int16_t sample_at(uint16_t i)
{
	int16_t mag = (int16_t)(200 + (i % 97) * 7);
	return (i & 1u) ? (int16_t)(-mag) : mag;
}

/*************************** T E S T S ***************************************/

/* A RAW capture survives save -> load, including the RAW_Data line splitting. */
static void test_raw_roundtrip(void)
{
	static flipper_subghz_signal_t sig;
	static flipper_subghz_signal_t got;
	uint16_t i;
	bool samples_match = true;

	printf("test_raw_roundtrip\n");
	f_unlink(TEST_PATH);

	memset(&sig, 0, sizeof(sig));
	sig.type = FLIPPER_SUBGHZ_TYPE_RAW;
	sig.frequency = 433920000u;
	strcpy(sig.preset, OOK_PRESET);
	strcpy(sig.protocol, "RAW");
	sig.raw_count = 600;                 /* forces several RAW_Data lines */
	for (i = 0; i < sig.raw_count; i++)
		sig.raw_data[i] = sample_at(i);

	CHECK(flipper_subghz_save(TEST_PATH, &sig), "raw: save");

	memset(&got, 0, sizeof(got));
	CHECK(flipper_subghz_load(TEST_PATH, &got), "raw: load");
	CHECK_EQ_INT(got.type, FLIPPER_SUBGHZ_TYPE_RAW, "raw: type");
	CHECK_EQ_INT(got.frequency, 433920000u, "raw: frequency");
	CHECK_EQ_STR(got.preset, OOK_PRESET, "raw: preset");
	CHECK_EQ_STR(got.protocol, "RAW", "raw: protocol");
	CHECK_EQ_INT(got.raw_count, sig.raw_count, "raw: sample count");

	for (i = 0; i < sig.raw_count && i < got.raw_count; i++) {
		if (got.raw_data[i] != sig.raw_data[i]) {
			samples_match = false;
			printf("  first mismatch at %u: got %d want %d\n",
			       (unsigned)i, got.raw_data[i], sig.raw_data[i]);
			break;
		}
	}
	CHECK(samples_match, "raw: every sample survives the split/join");

	f_unlink(TEST_PATH);
}

/* Out-of-int16 samples clamp instead of wrapping. */
static void test_raw_sample_clamping(void)
{
	static flipper_subghz_signal_t got;

	printf("test_raw_sample_clamping\n");
	f_unlink(TEST_PATH);

	write_file(TEST_PATH,
	           "Filetype: Flipper SubGhz RAW File\n"
	           "Version: 1\n"
	           "Frequency: 433920000\n"
	           "Preset: " OOK_PRESET "\n"
	           "Protocol: RAW\n"
	           "RAW_Data: 40000 -40000 32767 -32768 100\n");

	memset(&got, 0, sizeof(got));
	CHECK(flipper_subghz_load(TEST_PATH, &got), "clamp: load");
	CHECK_EQ_INT(got.raw_count, 5, "clamp: sample count");
	CHECK_EQ_INT(got.raw_data[0], 32767, "clamp: above INT16_MAX saturates");
	CHECK_EQ_INT(got.raw_data[1], -32768, "clamp: below INT16_MIN saturates");
	CHECK_EQ_INT(got.raw_data[2], 32767, "clamp: INT16_MAX untouched");
	CHECK_EQ_INT(got.raw_data[3], -32768, "clamp: INT16_MIN untouched");
	CHECK_EQ_INT(got.raw_data[4], 100, "clamp: ordinary sample untouched");

	f_unlink(TEST_PATH);
}

/* A capture longer than the buffer stops at the cap instead of overflowing. */
static void test_raw_sample_cap(void)
{
	static flipper_subghz_signal_t got;
	FILE *f;
	uint16_t i;

	printf("test_raw_sample_cap\n");
	f_unlink(TEST_PATH);

	/* 2400 samples in 100-sample lines: 352 more than the buffer holds. */
	f = fopen(TEST_PATH, "wb");
	if (f == NULL) {
		g_failures++;
		printf("  FAIL: cap fixture fopen\n");
		return;
	}
	fprintf(f, "Filetype: Flipper SubGhz RAW File\nVersion: 1\n");
	fprintf(f, "Frequency: 433920000\nPreset: %s\n", OOK_PRESET);
	for (i = 0; i < 2400; i++) {
		if ((i % 100) == 0)
			fprintf(f, "%sRAW_Data:", (i == 0) ? "" : "\n");
		fprintf(f, " %d", (int)sample_at(i));
	}
	fprintf(f, "\n");
	fclose(f);

	memset(&got, 0, sizeof(got));
	CHECK(flipper_subghz_load(TEST_PATH, &got), "cap: load");
	CHECK_EQ_INT(got.raw_count, FLIPPER_SUBGHZ_RAW_MAX_SAMPLES, "cap: count stops at the buffer size");
	CHECK_EQ_INT(got.raw_data[0], sample_at(0), "cap: first sample kept");
	CHECK_EQ_INT(got.raw_data[FLIPPER_SUBGHZ_RAW_MAX_SAMPLES - 1],
	             sample_at(FLIPPER_SUBGHZ_RAW_MAX_SAMPLES - 1),
	             "cap: last in-range sample kept");

	f_unlink(TEST_PATH);
}

/* A decoded (Key) signal survives save -> load with its full 64-bit key. */
static void test_parsed_roundtrip(void)
{
	static flipper_subghz_signal_t sig;
	static flipper_subghz_signal_t got;

	printf("test_parsed_roundtrip\n");
	f_unlink(TEST_PATH);

	memset(&sig, 0, sizeof(sig));
	sig.type = FLIPPER_SUBGHZ_TYPE_PARSED;
	sig.frequency = 315000000u;
	strcpy(sig.preset, OOK_PRESET);
	strcpy(sig.protocol, "Princeton");
	sig.bit_count = 24;
	sig.key = 0x0000000701D0F3A5ull;
	sig.te = 400;

	CHECK(flipper_subghz_save(TEST_PATH, &sig), "parsed: save");

	memset(&got, 0, sizeof(got));
	CHECK(flipper_subghz_load(TEST_PATH, &got), "parsed: load");
	CHECK_EQ_INT(got.type, FLIPPER_SUBGHZ_TYPE_PARSED, "parsed: type");
	CHECK_EQ_INT(got.frequency, 315000000u, "parsed: frequency");
	CHECK_EQ_STR(got.preset, OOK_PRESET, "parsed: preset");
	CHECK_EQ_STR(got.protocol, "Princeton", "parsed: protocol");
	CHECK_EQ_INT(got.bit_count, 24, "parsed: bit count");
	CHECK_EQ_INT(got.key, 0x0000000701D0F3A5ull, "parsed: 64-bit key");
	CHECK_EQ_INT(got.te, 400, "parsed: TE");
	CHECK_EQ_INT(got.raw_count, 0, "parsed: no raw samples");

	/* A key with the top byte set must not lose it to sign/width slips. */
	sig.key = 0xA5A5A5A5DEADBEEFull;
	sig.te = 0;                          /* TE omitted when zero */
	CHECK(flipper_subghz_save(TEST_PATH, &sig), "parsed: save wide key");
	memset(&got, 0, sizeof(got));
	CHECK(flipper_subghz_load(TEST_PATH, &got), "parsed: load wide key");
	CHECK_EQ_INT(got.key, 0xA5A5A5A5DEADBEEFull, "parsed: MSB-set key round-trips");
	CHECK_EQ_INT(got.te, 0, "parsed: TE absent stays zero");

	f_unlink(TEST_PATH);
}

/* A hand-written Flipper file (the common on-card shape) parses as expected. */
static void test_parsed_from_flipper_file(void)
{
	static flipper_subghz_signal_t got;

	printf("test_parsed_from_flipper_file\n");
	f_unlink(TEST_PATH);

	write_file(TEST_PATH,
	           "Filetype: Flipper SubGhz Key File\n"
	           "Version: 1\n"
	           "Frequency: 433920000\n"
	           "Preset: " OOK_PRESET "\n"
	           "Protocol: Nice FLO\n"
	           "Bit: 12\n"
	           "Key: 00 00 00 00 00 00 0C 5A\n");

	memset(&got, 0, sizeof(got));
	CHECK(flipper_subghz_load(TEST_PATH, &got), "flipper file: load");
	CHECK_EQ_INT(got.type, FLIPPER_SUBGHZ_TYPE_PARSED, "flipper file: type");
	CHECK_EQ_STR(got.protocol, "Nice FLO", "flipper file: protocol with a space");
	CHECK_EQ_INT(got.bit_count, 12, "flipper file: bit count");
	CHECK_EQ_INT(got.key, 0x0C5Aull, "flipper file: big-endian key assembly");

	f_unlink(TEST_PATH);
}

/* Malformed or empty files are rejected rather than half-loaded. */
static void test_load_rejects(void)
{
	static flipper_subghz_signal_t got;

	printf("test_load_rejects\n");
	f_unlink(TEST_PATH);

	CHECK(!flipper_subghz_load(TEST_PATH, &got), "reject: missing file");
	CHECK(!flipper_subghz_load(NULL, &got), "reject: NULL path");
	CHECK(!flipper_subghz_load(TEST_PATH, NULL), "reject: NULL out");
	CHECK(!flipper_subghz_save(NULL, &got), "reject: save NULL path");
	CHECK(!flipper_subghz_save(TEST_PATH, NULL), "reject: save NULL signal");

	write_file(TEST_PATH, "Frequency: 433920000\nRAW_Data: 100 -100\n");
	CHECK(!flipper_subghz_load(TEST_PATH, &got), "reject: no Filetype line");

	write_file(TEST_PATH,
	           "Filetype: Flipper SubGhz RAW File\nRAW_Data: 100 -100\n");
	CHECK(!flipper_subghz_load(TEST_PATH, &got), "reject: no Version line");

	write_file(TEST_PATH,
	           "Filetype: Flipper SubGhz RAW File\n"
	           "Version: 1\n"
	           "Frequency: 433920000\n"
	           "Preset: " OOK_PRESET "\n");
	CHECK(!flipper_subghz_load(TEST_PATH, &got), "reject: RAW file with no samples");

	write_file(TEST_PATH,
	           "Filetype: Flipper SubGhz Key File\n"
	           "Version: 1\n"
	           "Frequency: 433920000\n"
	           "Bit: 24\n"
	           "Key: 00 00 00 00 00 00 0C 5A\n");
	CHECK(!flipper_subghz_load(TEST_PATH, &got), "reject: parsed file with no Protocol");

	write_file(TEST_PATH,
	           "Filetype: Flipper SubGhz Key File\n"
	           "Version: 1\n"
	           "Protocol: Nice FLO\n"
	           "Bit: 24\n"
	           "Key: 00 00 00 00 00 00 0C 5A\n");
	CHECK(!flipper_subghz_load(TEST_PATH, &got), "reject: parsed file with no Frequency");

	/* Version gate: the loader only understands "1" and "2" today. A file
	 * stamped with any other version is refused outright. */
	write_file(TEST_PATH,
	           "Filetype: Flipper SubGhz RAW File\n"
	           "Version: 3\n"
	           "Frequency: 433920000\n"
	           "RAW_Data: 100 -100\n");
	CHECK(!flipper_subghz_load(TEST_PATH, &got), "reject: unknown Version 3");

	write_file(TEST_PATH,
	           "Filetype: Flipper SubGhz RAW File\n"
	           "Version: 2\n"
	           "Frequency: 433920000\n"
	           "RAW_Data: 100 -100\n");
	CHECK(flipper_subghz_load(TEST_PATH, &got), "accept: Version 2");

	f_unlink(TEST_PATH);
}

/* Preset string -> modulation enum. */
static void test_preset_to_modulation(void)
{
	printf("test_preset_to_modulation\n");

	CHECK_EQ_INT(flipper_subghz_preset_to_modulation("FuriHalSubGhzPresetOok270Async"),
	             MODULATION_OOK, "preset: Ook270 -> OOK");
	CHECK_EQ_INT(flipper_subghz_preset_to_modulation(OOK_PRESET),
	             MODULATION_OOK, "preset: Ook650 -> OOK");
	CHECK_EQ_INT(flipper_subghz_preset_to_modulation("CustomOOKPreset"),
	             MODULATION_OOK, "preset: upper-case OOK -> OOK");
	CHECK_EQ_INT(flipper_subghz_preset_to_modulation("FuriHalSubGhzPreset2FSKDev238Async"),
	             MODULATION_FSK, "preset: 2FSKDev238 -> FSK");
	CHECK_EQ_INT(flipper_subghz_preset_to_modulation("FuriHalSubGhzPreset2FSKDev476Async"),
	             MODULATION_FSK, "preset: 2FSKDev476 -> FSK");
	CHECK_EQ_INT(flipper_subghz_preset_to_modulation("FuriHalSubGhzPresetMSK99_97KbAsync"),
	             MODULATION_UNKNOWN, "preset: MSK is not mapped");
	CHECK_EQ_INT(flipper_subghz_preset_to_modulation(""),
	             MODULATION_UNKNOWN, "preset: empty -> UNKNOWN");
	CHECK_EQ_INT(flipper_subghz_preset_to_modulation(NULL),
	             MODULATION_UNKNOWN, "preset: NULL -> UNKNOWN");
}

/* Frequency -> SI4463 band, checked on both sides of every boundary. */
static void test_freq_to_band(void)
{
	printf("test_freq_to_band\n");

	CHECK_EQ_INT(flipper_subghz_freq_to_band(0), SUB_GHZ_BAND_300, "band: 0 Hz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(300000000u), SUB_GHZ_BAND_300, "band: 300.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(305999999u), SUB_GHZ_BAND_300, "band: 305.99 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(306000000u), SUB_GHZ_BAND_310, "band: 306.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(312000000u), SUB_GHZ_BAND_310, "band: 312.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(313000000u), SUB_GHZ_BAND_315, "band: 313.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(315000000u), SUB_GHZ_BAND_315, "band: 315.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(320000000u), SUB_GHZ_BAND_315, "band: 320.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(321000000u), SUB_GHZ_BAND_345, "band: 321.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(345000000u), SUB_GHZ_BAND_345, "band: 345.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(358000000u), SUB_GHZ_BAND_345, "band: 358.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(359000000u), SUB_GHZ_BAND_372, "band: 359.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(381000000u), SUB_GHZ_BAND_372, "band: 381.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(382000000u), SUB_GHZ_BAND_390, "band: 382.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(390000000u), SUB_GHZ_BAND_390, "band: 390.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(412000000u), SUB_GHZ_BAND_390, "band: 412.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(413000000u), SUB_GHZ_BAND_433, "band: 413.00 MHz");

	/* NOTE: the mapping truncates Hz to whole MHz, so 433.92 MHz — the most
	 * common Flipper frequency — lands in SUB_GHZ_BAND_433, not
	 * SUB_GHZ_BAND_433_92. Pinned here as current behaviour. */
	CHECK_EQ_INT(flipper_subghz_freq_to_band(433920000u), SUB_GHZ_BAND_433, "band: 433.92 MHz truncates to 433");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(433999999u), SUB_GHZ_BAND_433, "band: 433.99 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(434000000u), SUB_GHZ_BAND_433_92, "band: 434.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(500000000u), SUB_GHZ_BAND_433_92, "band: 500.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(501000000u), SUB_GHZ_BAND_915, "band: 501.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(915000000u), SUB_GHZ_BAND_915, "band: 915.00 MHz");
	CHECK_EQ_INT(flipper_subghz_freq_to_band(4000000000u), SUB_GHZ_BAND_915, "band: far above range");
}

int main(void)
{
	printf("== Flipper .sub host tests ==\n");

	test_raw_roundtrip();
	test_raw_sample_clamping();
	test_raw_sample_cap();
	test_parsed_roundtrip();
	test_parsed_from_flipper_file();
	test_load_rejects();
	test_preset_to_modulation();
	test_freq_to_band();

	printf("== %d checks, %d failures ==\n", g_checks, g_failures);
	return (g_failures == 0) ? 0 : 1;
}
