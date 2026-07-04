/* See COPYING.txt for license details. */

/*
 * test_mfc_dict.c  (HOST TEST)
 *
 * Data-linter for the shipped MIFARE Classic key dictionary
 * (sdcard/NFC/system/mf_classic_dict.nfc). It re-implements the firmware's
 * exact line-reading contract so the shipped file is proven parseable by the
 * on-device iterator BEFORE it ever touches hardware:
 *
 *   - nfcfio_getline() uses a fixed char line[32] buffer and TRUNCATES any
 *     physical line longer than 31 chars, returning the fragment without
 *     consuming the rest (NFC/NFC_drv/common/nfc_fileio.c:81). The next call
 *     resumes mid-line — so an overlong comment can fragment into pieces that
 *     the key parser then mis-reads.
 *   - mfc_key_iter_next() skips leading whitespace, skips '#' comments and
 *     blank lines, then parses exactly 12 hex chars -> 6 bytes
 *     (nfc_poller.c:1252). MFC line buffer size and key length below mirror
 *     the firmware constants.
 *
 * The test parses the file TWICE: once logically (split on '\n' only, i.e. the
 * author's intent) and once through the firmware's 32-byte truncating reader.
 * If the two key sets are identical, the file is safe to ship as-is with no
 * parser change. It also asserts the well-known default keys are present.
 *
 * Usage: test_mfc_dict <path-to-dict>   (path required)
 *
 * M1 Project — host test harness
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Mirror the firmware constants. */
#define MFC_LINE_BUF   32   /* nfc_poller.c: char line[32] */
#define MFC_KEY_LEN     6

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

/*************************** P A R S E R   M I R R O R ************************/

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
	return -1;
}

/* mfc_key_iter_next() line->key logic. Returns true and fills key_out[6] when
 * the fragment is a valid 12-hex key line; false for comment/blank/garbage. */
static bool parse_key_line(const char *line, uint8_t key_out[MFC_KEY_LEN])
{
	const char *p = line;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	if (*p == '\0') return false;
	if (*p == '#')  return false;

	for (int i = 0; i < MFC_KEY_LEN; i++) {
		int hi = hex_nibble(*p++);
		int lo = hex_nibble(*p++);
		if (hi < 0 || lo < 0) return false;
		key_out[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}

/* Collect keys as uppercase 12-char hex strings for set comparison. */
typedef struct { char (*keys)[13]; size_t count; size_t cap; } keyset_t;

static void keyset_add(keyset_t *ks, const uint8_t k[MFC_KEY_LEN])
{
	if (ks->count == ks->cap) {
		ks->cap = ks->cap ? ks->cap * 2 : 256;
		ks->keys = realloc(ks->keys, ks->cap * sizeof(ks->keys[0]));
	}
	snprintf(ks->keys[ks->count], 13, "%02X%02X%02X%02X%02X%02X",
	         k[0], k[1], k[2], k[3], k[4], k[5]);
	ks->count++;
}

static bool keyset_has(const keyset_t *ks, const char *hex12)
{
	for (size_t i = 0; i < ks->count; i++)
		if (strcmp(ks->keys[i], hex12) == 0) return true;
	return false;
}

/* Logical parse: split on '\n' only (author intent, unlimited line length). */
static void parse_logical(const char *buf, size_t len, keyset_t *out)
{
	char line[4096];
	size_t li = 0;
	for (size_t i = 0; i <= len; i++) {
		char c = (i < len) ? buf[i] : '\n';
		if (c == '\n' || li + 1 >= sizeof(line)) {
			line[li] = '\0';
			uint8_t key[MFC_KEY_LEN];
			if (parse_key_line(line, key)) keyset_add(out, key);
			li = 0;
			if (c != '\n') { /* overlong logical line: swallow to newline */
				while (i < len && buf[i] != '\n') i++;
			}
		} else {
			line[li++] = c;
		}
	}
}

/* Firmware parse: emulate nfcfio_getline() with a MFC_LINE_BUF buffer that
 * TRUNCATES at buffer-full, then feed each fragment to the key parser. */
static void parse_firmware(const char *buf, size_t len, keyset_t *out)
{
	size_t i = 0;
	while (i < len) {
		char line[MFC_LINE_BUF];
		size_t w = 0;
		while (i < len && w + 1 < MFC_LINE_BUF) {
			char c = buf[i++];
			line[w++] = c;
			if (c == '\n') break;
		}
		line[w] = '\0';
		uint8_t key[MFC_KEY_LEN];
		if (parse_key_line(line, key)) keyset_add(out, key);
	}
}

/*************************** T E S T S ***************************************/

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <dict-file>\n", argv[0]);
		return 2;
	}
	const char *path = argv[1];

	FILE *f = fopen(path, "rb");
	if (!f) { fprintf(stderr, "cannot open %s\n", path); return 2; }
	fseek(f, 0, SEEK_END);
	long fsz = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = malloc((size_t)fsz + 1);
	size_t rd = fread(buf, 1, (size_t)fsz, f);
	buf[rd] = '\0';
	fclose(f);

	printf("Testing dict: %s (%zu bytes)\n", path, rd);

	keyset_t logical = {0}, firmware = {0};
	parse_logical(buf, rd, &logical);
	parse_firmware(buf, rd, &firmware);

	/* 1. The firmware iterator must recover at least one key. */
	CHECK(firmware.count > 0, "firmware parser yields > 0 keys");

	/* 2. Truncation safety: firmware-visible keys == logical keys, both ways.
	 *    A mismatch means an overlong line fragmented into spurious/lost keys. */
	CHECK_EQ_INT(firmware.count, logical.count,
	             "firmware key count == logical key count (no truncation drift)");
	{
		int missing = 0, spurious = 0;
		for (size_t i = 0; i < logical.count; i++)
			if (!keyset_has(&firmware, logical.keys[i])) missing++;
		for (size_t i = 0; i < firmware.count; i++)
			if (!keyset_has(&logical, firmware.keys[i])) spurious++;
		CHECK_EQ_INT(missing, 0, "no logical key lost by firmware reader");
		CHECK_EQ_INT(spurious, 0, "no spurious key from truncated fragment");
	}

	/* 3. Well-known default keys must be present. */
	CHECK(keyset_has(&firmware, "FFFFFFFFFFFF"), "default key FFFFFFFFFFFF present");
	CHECK(keyset_has(&firmware, "000000000000"), "blank key 000000000000 present");
	CHECK(keyset_has(&firmware, "A0A1A2A3A4A5"), "NFC Forum MAD key present");

	printf("keys: firmware=%zu logical=%zu\n", firmware.count, logical.count);
	printf("%d checks, %d failures\n", g_checks, g_failures);

	free(logical.keys);
	free(firmware.keys);
	free(buf);
	return g_failures ? 1 : 0;
}
