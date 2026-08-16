/* See COPYING.txt for license details. */

/*
 * test_flipper_file.c  (HOST TEST)
 *
 * Unit tests for the generic Flipper key-value file layer (m1_csrc/flipper_file.c),
 * the parser every .ir / .sub / .nfc / .rfid reader is built on. The real module
 * is compiled against the host FatFs shim; no firmware build, no hardware.
 *
 * Verifies:
 *   - ff_read_line(): blank lines skipped, comments surfaced as separators,
 *     CRLF and trailing whitespace stripped, over-long lines split not lost
 *   - ff_parse_kv(): ": " separator contract, key/value trimming, first-colon
 *     wins, value clamped to FF_VALUE_MAX_LEN
 *   - ff_validate_header(): filetype match + minimum version
 *   - write helpers: str / uint32 / float / hex / separator / comment round-trip
 *   - ff_parse_hex_bytes(): pairs, lone digits, junk skipping, max_len cap
 *   - ff_parse_int32_array(): signs, junk skipping, max_count cap
 *   - NULL / missing-file guards on every entry point
 *
 * These assertions pin CURRENT behaviour so a refactor of the parser cannot
 * silently change what the file layer accepts.
 *
 * M1 Project — host test harness
 */

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
		long _a = (long)(actual);                                              \
		long _e = (long)(expected);                                            \
		if (_a != _e) {                                                        \
			g_failures++;                                                      \
			printf("  FAIL: %s  (got %ld, want %ld)  (%s:%d)\n",               \
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

#define TEST_PATH   "m1_flipper_file.txt"
#define MISSING_PATH "m1_flipper_file_absent.txt"

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

/*************************** T E S T S ***************************************/

/* Blank lines vanish, comments come back flagged, CR/LF + padding stripped. */
static void test_read_line_filtering(void)
{
	flipper_file_t ff;

	printf("test_read_line_filtering\n");

	write_file(TEST_PATH,
	           "Filetype: Flipper Test\r\n"       /* CRLF                     */
	           "\n"                               /* blank -> skipped         */
	           "   \t \n"                         /* whitespace -> skipped    */
	           "Version: 1   \n"                  /* trailing pad -> stripped */
	           "#\n"                              /* separator                */
	           "  # indented comment\n"
	           "name: Power\n");

	CHECK(ff_open(&ff, TEST_PATH), "open fixture");

	CHECK(ff_read_line(&ff), "line 1 read");
	CHECK_EQ_STR(ff.line, "Filetype: Flipper Test", "CR stripped from line 1");
	CHECK(!ff_is_separator(&ff), "line 1 is not a separator");

	CHECK(ff_read_line(&ff), "line 2 read (blanks skipped)");
	CHECK_EQ_STR(ff.line, "Version: 1", "trailing spaces stripped");

	CHECK(ff_read_line(&ff), "line 3 read");
	CHECK(ff_is_separator(&ff), "'#' is a separator");

	CHECK(ff_read_line(&ff), "line 4 read");
	CHECK(ff_is_separator(&ff), "indented '#' is a separator");

	CHECK(ff_read_line(&ff), "line 5 read");
	CHECK(ff_parse_kv(&ff), "line 5 parses as kv");
	CHECK_EQ_STR(ff_get_key(&ff), "name", "line 5 key");

	CHECK(!ff_read_line(&ff), "EOF returns false");
	CHECK(!ff_read_line(&ff), "EOF is sticky");

	ff_close(&ff);
	f_unlink(TEST_PATH);
}

/* The key-value contract: ": " separator, trimming, first colon wins. */
static void test_parse_kv_contract(void)
{
	flipper_file_t ff;

	printf("test_parse_kv_contract\n");

	write_file(TEST_PATH,
	           "  Device type  : ISO14443-3A\n"   /* padded key, padded value */
	           "NoSpaceAfterColon:value\n"        /* NOT a kv line            */
	           "Preset: Furi:Ook 650\n"           /* ':' inside the value     */
	           "Note: a: b\n"                     /* first ': ' wins          */
	           ": orphan value\n"                 /* empty key -> rejected    */
	           "Empty: \n");                      /* value stripped to ""     */

	CHECK(ff_open(&ff, TEST_PATH), "open fixture");

	CHECK(ff_read_line(&ff), "kv line 1 read");
	CHECK(ff_parse_kv(&ff), "padded line parses");
	CHECK_EQ_STR(ff_get_key(&ff), "Device type", "key trimmed both sides");
	CHECK_EQ_STR(ff_get_value(&ff), "ISO14443-3A", "value left-trimmed");

	CHECK(ff_read_line(&ff), "kv line 2 read");
	CHECK(!ff_parse_kv(&ff), "'key:value' without space is not kv");
	CHECK_EQ_STR(ff_get_key(&ff), "", "failed parse clears key");
	CHECK_EQ_STR(ff_get_value(&ff), "", "failed parse clears value");

	CHECK(ff_read_line(&ff), "kv line 3 read");
	CHECK(ff_parse_kv(&ff), "colon inside value still parses");
	CHECK_EQ_STR(ff_get_key(&ff), "Preset", "key stops at first ': '");
	CHECK_EQ_STR(ff_get_value(&ff), "Furi:Ook 650", "bare ':' stays in value");

	CHECK(ff_read_line(&ff), "kv line 4 read");
	CHECK(ff_parse_kv(&ff), "double ': ' parses");
	CHECK_EQ_STR(ff_get_key(&ff), "Note", "first ': ' wins for the key");
	CHECK_EQ_STR(ff_get_value(&ff), "a: b", "rest of line is the value");

	CHECK(ff_read_line(&ff), "kv line 5 read");
	CHECK(!ff_parse_kv(&ff), "empty key is rejected");

	CHECK(ff_read_line(&ff), "kv line 6 read");
	/* Trailing whitespace is stripped at read time, so "Empty: " becomes
	 * "Empty:" — with no ": " left, the line is no longer key-value. This is
	 * the same shape ff_write_kv_hex() emits for a zero-length byte array. */
	CHECK(!ff_parse_kv(&ff), "empty value line is not kv (': ' stripped)");

	ff_close(&ff);
	f_unlink(TEST_PATH);
}

/* Lines longer than the buffer are split, not dropped; values are clamped. */
static void test_long_line_clamping(void)
{
	flipper_file_t ff;
	char big[900];
	char line[1024];
	size_t vlen;

	printf("test_long_line_clamping\n");

	memset(big, 'A', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	snprintf(line, sizeof(line), "Data: %s\n", big);
	write_file(TEST_PATH, line);

	CHECK(ff_open(&ff, TEST_PATH), "open long-line fixture");

	CHECK(ff_read_line(&ff), "long line: first chunk read");
	CHECK_EQ_INT(strlen(ff.line), FF_LINE_BUF_LEN - 1, "chunk fills the line buffer");
	CHECK(ff_parse_kv(&ff), "first chunk parses as kv");
	CHECK_EQ_STR(ff_get_key(&ff), "Data", "long line key");
	/* The chunk carries 505 value bytes but the value buffer holds 447 + NUL,
	 * so ff_parse_kv() truncates instead of overflowing. */
	vlen = strlen(ff_get_value(&ff));
	CHECK_EQ_INT(vlen, FF_VALUE_MAX_LEN - 1, "value clamped to the value buffer");
	CHECK_EQ_INT(ff_get_value(&ff)[vlen - 1], 'A', "clamped value keeps its content");

	CHECK(ff_read_line(&ff), "long line: remainder becomes its own line");
	CHECK(!ff_parse_kv(&ff), "remainder has no key");
	CHECK(!ff_read_line(&ff), "long line: EOF after remainder");

	ff_close(&ff);
	f_unlink(TEST_PATH);
}

/* Header gate: exact filetype string, version >= minimum. */
static void test_validate_header(void)
{
	flipper_file_t ff;

	printf("test_validate_header\n");

	write_file(TEST_PATH, "Filetype: Flipper NFC device\nVersion: 4\nUID: 01 02\n");
	CHECK(ff_open(&ff, TEST_PATH), "header: open good");
	CHECK(ff_validate_header(&ff, "Flipper NFC device", 2), "version above minimum passes");
	ff_close(&ff);

	CHECK(ff_open(&ff, TEST_PATH), "header: reopen good");
	CHECK(ff_validate_header(&ff, "Flipper NFC device", 4), "version equal to minimum passes");
	ff_close(&ff);

	CHECK(ff_open(&ff, TEST_PATH), "header: reopen for too-new minimum");
	CHECK(!ff_validate_header(&ff, "Flipper NFC device", 5), "version below minimum fails");
	ff_close(&ff);

	CHECK(ff_open(&ff, TEST_PATH), "header: reopen for wrong filetype");
	CHECK(!ff_validate_header(&ff, "Flipper SubGhz RAW File", 2), "wrong filetype fails");
	ff_close(&ff);

	write_file(TEST_PATH, "Filetype: Flipper NFC device\n");
	CHECK(ff_open(&ff, TEST_PATH), "header: open truncated");
	CHECK(!ff_validate_header(&ff, "Flipper NFC device", 2), "missing Version line fails");
	ff_close(&ff);

	write_file(TEST_PATH, "Version: 4\nFiletype: Flipper NFC device\n");
	CHECK(ff_open(&ff, TEST_PATH), "header: open reordered");
	CHECK(!ff_validate_header(&ff, "Flipper NFC device", 2), "Version before Filetype fails");
	ff_close(&ff);

	f_unlink(TEST_PATH);
}

/* Every write helper emits a line the reader can take back. */
static void test_write_helpers_roundtrip(void)
{
	flipper_file_t ff;
	const uint8_t uid[7] = { 0x04, 0x68, 0x95, 0x71, 0xFA, 0x5C, 0x64 };
	uint8_t got[7];

	printf("test_write_helpers_roundtrip\n");

	CHECK(ff_open_write(&ff, TEST_PATH), "write: open");
	CHECK(ff_write_kv_str(&ff, "Filetype", "Flipper Test"), "write str");
	CHECK(ff_write_kv_uint32(&ff, "Version", 4), "write uint32");
	CHECK(ff_write_kv_uint32(&ff, "Big", 4294967295u), "write uint32 max");
	CHECK(ff_write_kv_float(&ff, "Duty", 0.33f), "write float");
	CHECK(ff_write_kv_float(&ff, "Neg", -1.5f), "write negative float");
	CHECK(ff_write_kv_hex(&ff, "UID", uid, 7), "write hex");
	CHECK(ff_write_separator(&ff), "write separator");
	CHECK(ff_write_comment(&ff, "hello"), "write comment");
	ff_close(&ff);

	CHECK(ff_open(&ff, TEST_PATH), "write: reopen for read");

	CHECK(ff_read_line(&ff) && ff_parse_kv(&ff), "read back str");
	CHECK_EQ_STR(ff_get_key(&ff), "Filetype", "str key");
	CHECK_EQ_STR(ff_get_value(&ff), "Flipper Test", "str value");

	CHECK(ff_read_line(&ff) && ff_parse_kv(&ff), "read back uint32");
	CHECK_EQ_STR(ff_get_value(&ff), "4", "uint32 value");

	CHECK(ff_read_line(&ff) && ff_parse_kv(&ff), "read back uint32 max");
	CHECK_EQ_STR(ff_get_value(&ff), "4294967295", "uint32 max value");

	CHECK(ff_read_line(&ff) && ff_parse_kv(&ff), "read back float");
	CHECK_EQ_STR(ff_get_value(&ff), "0.33", "float formatted to 2 decimals");

	CHECK(ff_read_line(&ff) && ff_parse_kv(&ff), "read back negative float");
	CHECK_EQ_STR(ff_get_value(&ff), "-1.50", "negative float keeps sign");

	CHECK(ff_read_line(&ff) && ff_parse_kv(&ff), "read back hex");
	CHECK_EQ_STR(ff_get_value(&ff), "04 68 95 71 FA 5C 64", "hex is uppercase, space separated");
	CHECK_EQ_INT(ff_parse_hex_bytes(ff_get_value(&ff), got, 7), 7, "hex re-parses to 7 bytes");
	CHECK(memcmp(got, uid, 7) == 0, "hex round-trips byte for byte");

	CHECK(ff_read_line(&ff), "read back separator");
	CHECK(ff_is_separator(&ff), "separator flagged");

	CHECK(ff_read_line(&ff), "read back comment");
	CHECK(ff_is_separator(&ff), "comment flagged as separator");
	CHECK_EQ_STR(ff.line, "# hello", "comment text preserved");

	CHECK(!ff_read_line(&ff), "write: EOF");
	ff_close(&ff);
	f_unlink(TEST_PATH);
}

/* Hex scanner: pairs, lone digits, junk, and the max_len ceiling. */
static void test_parse_hex_bytes(void)
{
	uint8_t out[8];
	uint8_t n;

	printf("test_parse_hex_bytes\n");

	memset(out, 0, sizeof(out));
	n = ff_parse_hex_bytes("04 68 95 71", out, 8);
	CHECK_EQ_INT(n, 4, "spaced pairs: count");
	CHECK_EQ_INT(out[0], 0x04, "spaced pairs: byte 0");
	CHECK_EQ_INT(out[3], 0x71, "spaced pairs: byte 3");

	memset(out, 0, sizeof(out));
	n = ff_parse_hex_bytes("0468ab", out, 8);
	CHECK_EQ_INT(n, 3, "unspaced pairs: count");
	CHECK_EQ_INT(out[2], 0xAB, "lowercase hex accepted");

	memset(out, 0, sizeof(out));
	n = ff_parse_hex_bytes("0A B", out, 8);
	CHECK_EQ_INT(n, 2, "trailing lone digit: count");
	CHECK_EQ_INT(out[0], 0x0A, "trailing lone digit: pair");
	CHECK_EQ_INT(out[1], 0x0B, "lone digit becomes its own byte");

	memset(out, 0, sizeof(out));
	n = ff_parse_hex_bytes("ZZ 7F ??", out, 8);
	CHECK_EQ_INT(n, 1, "junk characters are skipped");
	CHECK_EQ_INT(out[0], 0x7F, "byte after junk still parsed");

	memset(out, 0, sizeof(out));
	n = ff_parse_hex_bytes("01 02 03 04", out, 2);
	CHECK_EQ_INT(n, 2, "max_len caps the count");
	CHECK_EQ_INT(out[1], 0x02, "max_len keeps the leading bytes");

	CHECK_EQ_INT(ff_parse_hex_bytes("", out, 8), 0, "empty string parses nothing");
	CHECK_EQ_INT(ff_parse_hex_bytes("   ", out, 8), 0, "whitespace only parses nothing");
	CHECK_EQ_INT(ff_parse_hex_bytes("01", NULL, 8), 0, "NULL out is rejected");
	CHECK_EQ_INT(ff_parse_hex_bytes(NULL, out, 8), 0, "NULL str is rejected");
	CHECK_EQ_INT(ff_parse_hex_bytes("01", out, 0), 0, "zero max_len is rejected");
}

/* int32 scanner: signs, junk, and the max_count ceiling. */
static void test_parse_int32_array(void)
{
	int32_t out[8];
	uint16_t n;

	printf("test_parse_int32_array\n");

	memset(out, 0, sizeof(out));
	n = ff_parse_int32_array("9024 -4512 579 -552", out, 8);
	CHECK_EQ_INT(n, 4, "signed samples: count");
	CHECK_EQ_INT(out[0], 9024, "sample 0");
	CHECK_EQ_INT(out[1], -4512, "negative sample");
	CHECK_EQ_INT(out[3], -552, "sample 3");

	memset(out, 0, sizeof(out));
	n = ff_parse_int32_array("  12\t\t-7  ", out, 8);
	CHECK_EQ_INT(n, 2, "tabs and padding tolerated");
	CHECK_EQ_INT(out[1], -7, "tab separated negative");

	memset(out, 0, sizeof(out));
	n = ff_parse_int32_array("12 abc 34", out, 8);
	CHECK_EQ_INT(n, 2, "non-numeric tokens are skipped");
	CHECK_EQ_INT(out[1], 34, "value after junk still parsed");

	memset(out, 0, sizeof(out));
	n = ff_parse_int32_array("1 2 3 4 5", out, 3);
	CHECK_EQ_INT(n, 3, "max_count caps the parse");
	CHECK_EQ_INT(out[2], 3, "max_count keeps the leading values");

	memset(out, 0, sizeof(out));
	n = ff_parse_int32_array("2147483647 -2147483648", out, 8);
	CHECK_EQ_INT(n, 2, "int32 extremes: count");
	CHECK_EQ_INT(out[0], 2147483647L, "INT32_MAX preserved");
	CHECK_EQ_INT(out[1], -2147483647L - 1L, "INT32_MIN preserved");

	CHECK_EQ_INT(ff_parse_int32_array("", out, 8), 0, "empty string parses nothing");
	CHECK_EQ_INT(ff_parse_int32_array("1", NULL, 8), 0, "NULL out is rejected");
	CHECK_EQ_INT(ff_parse_int32_array(NULL, out, 8), 0, "NULL str is rejected");
	CHECK_EQ_INT(ff_parse_int32_array("1", out, 0), 0, "zero max_count is rejected");
}

/* Bad paths and NULL contexts never reach the file system. */
static void test_open_guards(void)
{
	flipper_file_t ff;

	printf("test_open_guards\n");

	f_unlink(MISSING_PATH);
	CHECK(!ff_open(&ff, MISSING_PATH), "open of a missing file fails");
	CHECK(!ff_open(NULL, TEST_PATH), "NULL ctx rejected by ff_open");
	CHECK(!ff_open(&ff, NULL), "NULL path rejected by ff_open");
	CHECK(!ff_open_write(NULL, TEST_PATH), "NULL ctx rejected by ff_open_write");
	CHECK(!ff_open_append(NULL, TEST_PATH), "NULL ctx rejected by ff_open_append");
	CHECK(!ff_read_line(NULL), "NULL ctx rejected by ff_read_line");
	CHECK(!ff_parse_kv(NULL), "NULL ctx rejected by ff_parse_kv");
	CHECK(!ff_is_separator(NULL), "NULL ctx is not a separator");
	CHECK_EQ_STR(ff_get_key(NULL), "", "NULL ctx key is empty");
	CHECK_EQ_STR(ff_get_value(NULL), "", "NULL ctx value is empty");
	CHECK(!ff_write_kv_str(NULL, "k", "v"), "NULL ctx rejected by ff_write_kv_str");
	ff_close(NULL);   /* must not crash */

	/* A context that was never opened refuses writes. */
	memset(&ff, 0, sizeof(ff));
	CHECK(!ff_write_kv_str(&ff, "k", "v"), "unopened ctx refuses str write");
	CHECK(!ff_write_kv_uint32(&ff, "k", 1), "unopened ctx refuses uint32 write");
	CHECK(!ff_write_separator(&ff), "unopened ctx refuses separator");

	/* Append creates the file when it does not exist yet. */
	CHECK(ff_open_append(&ff, MISSING_PATH), "append creates a missing file");
	CHECK(ff_write_kv_str(&ff, "Filetype", "Flipper Test"), "append writes");
	ff_close(&ff);
	CHECK(ff_open(&ff, MISSING_PATH), "created file is readable");
	CHECK(ff_read_line(&ff) && ff_parse_kv(&ff), "created file has the line");
	CHECK_EQ_STR(ff_get_value(&ff), "Flipper Test", "appended value survives");
	ff_close(&ff);

	/* Appending again preserves what was already there. */
	CHECK(ff_open_append(&ff, MISSING_PATH), "reopen for append");
	CHECK(ff_write_kv_uint32(&ff, "Version", 1), "second append writes");
	ff_close(&ff);
	CHECK(ff_open(&ff, MISSING_PATH), "reopen after second append");
	CHECK(ff_read_line(&ff) && ff_parse_kv(&ff), "first line still present");
	CHECK_EQ_STR(ff_get_key(&ff), "Filetype", "append did not truncate");
	CHECK(ff_read_line(&ff) && ff_parse_kv(&ff), "second line present");
	CHECK_EQ_STR(ff_get_key(&ff), "Version", "second append landed at the end");
	ff_close(&ff);

	f_unlink(MISSING_PATH);
}

int main(void)
{
	printf("== Flipper key-value file layer host tests ==\n");

	test_read_line_filtering();
	test_parse_kv_contract();
	test_long_line_clamping();
	test_validate_header();
	test_write_helpers_roundtrip();
	test_parse_hex_bytes();
	test_parse_int32_array();
	test_open_guards();

	printf("== %d checks, %d failures ==\n", g_checks, g_failures);
	return (g_failures == 0) ? 0 : 1;
}
