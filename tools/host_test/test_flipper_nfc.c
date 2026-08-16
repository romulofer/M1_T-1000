/* See COPYING.txt for license details. */

/*
 * test_flipper_nfc.c  (HOST TEST)
 *
 * Unit tests for the Flipper .nfc file layer (m1_csrc/flipper_nfc.c): card
 * header save/load, the device-type table, and the Page/Block dump reader.
 * The real module is compiled against the host FatFs shim.
 *
 * Verifies:
 *   - save -> load round-trip of UID / ATQA / SAK / device type
 *   - a hand-written Flipper NTAG215 file parses field for field
 *   - header gate: wrong filetype, version below 2, UID-less files are refused
 *   - UID longer than FLIPPER_NFC_UID_MAX_LEN is capped, not overflowed
 *   - flipper_nfc_parse_type() over the whole table, case-insensitively
 *   - flipper_nfc_load_dump(): 4-byte pages vs 16-byte blocks, sparse indices,
 *     short lines skipped, "Pages total:" not mistaken for a page, and the
 *     dump_buf_size ceiling honoured
 *
 * M1 Project — host test harness
 */

#include "flipper_nfc.h"

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

#define TEST_PATH   "m1_flipper_roundtrip.nfc"

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

/* Count the bits set in a valid_bits bitmap. */
static int count_valid(const uint8_t *bits, int n_bytes)
{
	int i;
	int n = 0;
	for (i = 0; i < n_bytes * 8; i++) {
		if (bits[i >> 3] & (uint8_t)(1u << (i & 7)))
			n++;
	}
	return n;
}

/*************************** T E S T S ***************************************/

/* A saved card comes back with every header field intact. */
static void test_card_roundtrip(void)
{
	flipper_nfc_card_t card;
	flipper_nfc_card_t got;

	printf("test_card_roundtrip\n");
	f_unlink(TEST_PATH);

	memset(&card, 0, sizeof(card));
	card.type = FLIPPER_NFC_TYPE_NTAG;
	strcpy(card.device_type, "NTAG215");
	card.uid_len = 7;
	card.uid[0] = 0x04; card.uid[1] = 0x68; card.uid[2] = 0x95; card.uid[3] = 0x71;
	card.uid[4] = 0xFA; card.uid[5] = 0x5C; card.uid[6] = 0x64;
	card.atqa[0] = 0x44; card.atqa[1] = 0x00;
	card.sak = 0x00;

	CHECK(flipper_nfc_save(TEST_PATH, &card), "card: save");

	memset(&got, 0xEE, sizeof(got));
	CHECK(flipper_nfc_load(TEST_PATH, &got), "card: load");
	CHECK_EQ_STR(got.device_type, "NTAG215", "card: device type");
	CHECK_EQ_INT(got.type, FLIPPER_NFC_TYPE_NTAG, "card: mapped type");
	CHECK_EQ_INT(got.uid_len, 7, "card: UID length");
	CHECK(memcmp(got.uid, card.uid, 7) == 0, "card: UID bytes");
	CHECK_EQ_INT(got.atqa[0], 0x44, "card: ATQA byte 0");
	CHECK_EQ_INT(got.atqa[1], 0x00, "card: ATQA byte 1");
	CHECK_EQ_INT(got.sak, 0x00, "card: SAK");

	/* A 4-byte UID card with a non-zero SAK (Mifare Classic 1K shape). */
	memset(&card, 0, sizeof(card));
	strcpy(card.device_type, "Mifare Classic 1K");
	card.uid_len = 4;
	card.uid[0] = 0xDE; card.uid[1] = 0xAD; card.uid[2] = 0xBE; card.uid[3] = 0xEF;
	card.atqa[0] = 0x00; card.atqa[1] = 0x04;
	card.sak = 0x08;

	CHECK(flipper_nfc_save(TEST_PATH, &card), "classic: save");
	memset(&got, 0, sizeof(got));
	CHECK(flipper_nfc_load(TEST_PATH, &got), "classic: load");
	CHECK_EQ_INT(got.type, FLIPPER_NFC_TYPE_MIFARE_CLASSIC, "classic: mapped type");
	CHECK_EQ_INT(got.uid_len, 4, "classic: UID length");
	CHECK_EQ_INT(got.uid[3], 0xEF, "classic: last UID byte");
	CHECK_EQ_INT(got.sak, 0x08, "classic: SAK");

	f_unlink(TEST_PATH);
}

/* A file written by a real Flipper parses field for field. */
static void test_load_flipper_file(void)
{
	flipper_nfc_card_t got;

	printf("test_load_flipper_file\n");
	f_unlink(TEST_PATH);

	write_file(TEST_PATH,
	           "Filetype: Flipper NFC device\n"
	           "Version: 4\n"
	           "# Nfc device type can be UID, Mifare Ultralight, Mifare Classic\n"
	           "Device type: NTAG215\n"
	           "# UID is common for all formats\n"
	           "UID: 04 68 95 71 FA 5C 64\n"
	           "ATQA: 44 00\n"
	           "SAK: 00\n"
	           "Signature: 00 00 00 00\n");

	memset(&got, 0, sizeof(got));
	CHECK(flipper_nfc_load(TEST_PATH, &got), "flipper file: load");
	CHECK_EQ_INT(got.uid_len, 7, "flipper file: UID length");
	CHECK_EQ_INT(got.uid[0], 0x04, "flipper file: UID byte 0");
	CHECK_EQ_INT(got.uid[6], 0x64, "flipper file: UID byte 6");
	CHECK_EQ_INT(got.atqa[0], 0x44, "flipper file: ATQA byte 0");
	CHECK_EQ_INT(got.sak, 0x00, "flipper file: SAK");
	CHECK_EQ_INT(got.type, FLIPPER_NFC_TYPE_NTAG, "flipper file: type");
	CHECK_EQ_STR(got.device_type, "NTAG215", "flipper file: device type text");

	f_unlink(TEST_PATH);
}

/* Files that fail the header gate or carry no UID are refused. */
static void test_load_rejects(void)
{
	flipper_nfc_card_t got;

	printf("test_load_rejects\n");
	f_unlink(TEST_PATH);

	CHECK(!flipper_nfc_load(TEST_PATH, &got), "reject: missing file");
	CHECK(!flipper_nfc_load(NULL, &got), "reject: NULL path");
	CHECK(!flipper_nfc_load(TEST_PATH, NULL), "reject: NULL out");
	CHECK(!flipper_nfc_save(NULL, &got), "reject: save NULL path");
	CHECK(!flipper_nfc_save(TEST_PATH, NULL), "reject: save NULL card");

	write_file(TEST_PATH,
	           "Filetype: Flipper SubGhz Key File\nVersion: 4\nUID: 04 68 95\n");
	CHECK(!flipper_nfc_load(TEST_PATH, &got), "reject: wrong filetype");

	write_file(TEST_PATH,
	           "Filetype: Flipper NFC device\nVersion: 1\nUID: 04 68 95\n");
	CHECK(!flipper_nfc_load(TEST_PATH, &got), "reject: version below 2");

	write_file(TEST_PATH,
	           "Filetype: Flipper NFC device\nVersion: 2\nUID: 04 68 95\n");
	CHECK(flipper_nfc_load(TEST_PATH, &got), "accept: version equal to 2");

	write_file(TEST_PATH,
	           "Filetype: Flipper NFC device\n"
	           "Version: 4\n"
	           "Device type: NTAG215\n"
	           "ATQA: 44 00\n"
	           "SAK: 00\n");
	CHECK(!flipper_nfc_load(TEST_PATH, &got), "reject: no UID line");

	/* An over-long UID is truncated to the struct capacity, never past it. */
	write_file(TEST_PATH,
	           "Filetype: Flipper NFC device\n"
	           "Version: 4\n"
	           "UID: 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E\n");
	memset(&got, 0, sizeof(got));
	CHECK(flipper_nfc_load(TEST_PATH, &got), "long UID: load");
	CHECK_EQ_INT(got.uid_len, FLIPPER_NFC_UID_MAX_LEN, "long UID: capped at the buffer size");
	CHECK_EQ_INT(got.uid[FLIPPER_NFC_UID_MAX_LEN - 1], 0x0A, "long UID: last kept byte");

	f_unlink(TEST_PATH);
}

/* Device-type string -> enum, over the whole table. */
static void test_parse_type(void)
{
	printf("test_parse_type\n");

	CHECK_EQ_INT(flipper_nfc_parse_type("ISO14443-3A"), FLIPPER_NFC_TYPE_ISO14443_3A, "type: ISO14443-3A");
	CHECK_EQ_INT(flipper_nfc_parse_type("ISO14443-3B"), FLIPPER_NFC_TYPE_ISO14443_3B, "type: ISO14443-3B");
	CHECK_EQ_INT(flipper_nfc_parse_type("ISO14443-4A"), FLIPPER_NFC_TYPE_ISO14443_4A, "type: ISO14443-4A");
	CHECK_EQ_INT(flipper_nfc_parse_type("NTAG"), FLIPPER_NFC_TYPE_NTAG, "type: NTAG");
	CHECK_EQ_INT(flipper_nfc_parse_type("NTAG203"), FLIPPER_NFC_TYPE_NTAG, "type: NTAG203");
	CHECK_EQ_INT(flipper_nfc_parse_type("NTAG213"), FLIPPER_NFC_TYPE_NTAG, "type: NTAG213");
	CHECK_EQ_INT(flipper_nfc_parse_type("NTAG215"), FLIPPER_NFC_TYPE_NTAG, "type: NTAG215");
	CHECK_EQ_INT(flipper_nfc_parse_type("NTAG216"), FLIPPER_NFC_TYPE_NTAG, "type: NTAG216");
	CHECK_EQ_INT(flipper_nfc_parse_type("NTAGI2C1K"), FLIPPER_NFC_TYPE_NTAG, "type: NTAGI2C1K");
	CHECK_EQ_INT(flipper_nfc_parse_type("NTAGI2C2K"), FLIPPER_NFC_TYPE_NTAG, "type: NTAGI2C2K");
	CHECK_EQ_INT(flipper_nfc_parse_type("Mifare Classic"), FLIPPER_NFC_TYPE_MIFARE_CLASSIC, "type: Mifare Classic");
	CHECK_EQ_INT(flipper_nfc_parse_type("Mifare Classic 1K"), FLIPPER_NFC_TYPE_MIFARE_CLASSIC, "type: Mifare Classic 1K");
	CHECK_EQ_INT(flipper_nfc_parse_type("Mifare Classic 4K"), FLIPPER_NFC_TYPE_MIFARE_CLASSIC, "type: Mifare Classic 4K");
	CHECK_EQ_INT(flipper_nfc_parse_type("Mifare DESFire"), FLIPPER_NFC_TYPE_MIFARE_DESFIRE, "type: Mifare DESFire");

	CHECK_EQ_INT(flipper_nfc_parse_type("ntag215"), FLIPPER_NFC_TYPE_NTAG, "type: lower case matches");
	CHECK_EQ_INT(flipper_nfc_parse_type("MIFARE CLASSIC 1K"), FLIPPER_NFC_TYPE_MIFARE_CLASSIC, "type: upper case matches");

	CHECK_EQ_INT(flipper_nfc_parse_type("NTAG21"), FLIPPER_NFC_TYPE_UNKNOWN, "type: prefix is not a match");
	CHECK_EQ_INT(flipper_nfc_parse_type("Mifare Ultralight"), FLIPPER_NFC_TYPE_UNKNOWN, "type: unlisted card");
	CHECK_EQ_INT(flipper_nfc_parse_type(""), FLIPPER_NFC_TYPE_UNKNOWN, "type: empty string");
	CHECK_EQ_INT(flipper_nfc_parse_type(NULL), FLIPPER_NFC_TYPE_UNKNOWN, "type: NULL");
}

/* 4-byte Page lines: count, unit size, payload and the valid bitmap. */
static void test_load_dump_pages(void)
{
	uint8_t dump[256];
	uint8_t valid[32];
	uint16_t unit_size = 0xFFFF;
	uint16_t count;

	printf("test_load_dump_pages\n");
	f_unlink(TEST_PATH);

	write_file(TEST_PATH,
	           "Filetype: Flipper NFC device\n"
	           "Version: 4\n"
	           "Device type: NTAG215\n"
	           "UID: 04 68 95 71 FA 5C 64\n"
	           "Pages total: 135\n"          /* must NOT be read as "Page " */
	           "Page 0: 04 68 95 71\n"
	           "Page 1: FA 5C 64 80\n"
	           "Page 2: 01 02 03\n"          /* short line -> skipped        */
	           "Page 3: AA BB CC DD\n");

	memset(dump, 0, sizeof(dump));
	memset(valid, 0, sizeof(valid));
	count = flipper_nfc_load_dump(TEST_PATH, dump, sizeof(dump), valid, &unit_size);

	CHECK_EQ_INT(unit_size, 4, "pages: unit size is 4");
	CHECK_EQ_INT(count, 4, "pages: count is highest index + 1");
	CHECK_EQ_INT(count_valid(valid, 32), 3, "pages: only complete lines are marked valid");
	CHECK(valid[0] & 0x01, "pages: page 0 valid");
	CHECK(valid[0] & 0x02, "pages: page 1 valid");
	CHECK(!(valid[0] & 0x04), "pages: short page 2 not valid");
	CHECK(valid[0] & 0x08, "pages: page 3 valid");

	CHECK_EQ_INT(dump[0], 0x04, "pages: page 0 byte 0");
	CHECK_EQ_INT(dump[3], 0x71, "pages: page 0 byte 3");
	CHECK_EQ_INT(dump[4], 0xFA, "pages: page 1 byte 0");
	CHECK_EQ_INT(dump[8], 0x00, "pages: skipped page left untouched");
	CHECK_EQ_INT(dump[12], 0xAA, "pages: page 3 byte 0");
	CHECK_EQ_INT(dump[15], 0xDD, "pages: page 3 byte 3");

	f_unlink(TEST_PATH);
}

/* 16-byte Block lines, with a gap between indices. */
static void test_load_dump_blocks(void)
{
	uint8_t dump[256];
	uint8_t valid[32];
	uint16_t unit_size = 0;
	uint16_t count;

	printf("test_load_dump_blocks\n");
	f_unlink(TEST_PATH);

	write_file(TEST_PATH,
	           "Filetype: Flipper NFC device\n"
	           "Version: 4\n"
	           "Device type: Mifare Classic 1K\n"
	           "UID: DE AD BE EF\n"
	           "Block 0: DE AD BE EF 08 04 00 62 63 64 65 66 67 68 69 6A\n"
	           "Block 4: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n");

	memset(dump, 0, sizeof(dump));
	memset(valid, 0, sizeof(valid));
	count = flipper_nfc_load_dump(TEST_PATH, dump, sizeof(dump), valid, &unit_size);

	CHECK_EQ_INT(unit_size, 16, "blocks: unit size is 16");
	CHECK_EQ_INT(count, 5, "blocks: count spans the gap");
	CHECK_EQ_INT(count_valid(valid, 32), 2, "blocks: only the two present blocks are valid");
	CHECK(valid[0] & 0x01, "blocks: block 0 valid");
	CHECK(valid[0] & 0x10, "blocks: block 4 valid");
	CHECK_EQ_INT(dump[0], 0xDE, "blocks: block 0 byte 0");
	CHECK_EQ_INT(dump[15], 0x6A, "blocks: block 0 byte 15");
	CHECK_EQ_INT(dump[16], 0x00, "blocks: gap left zeroed");
	CHECK_EQ_INT(dump[64], 0x00, "blocks: block 4 byte 0");
	CHECK_EQ_INT(dump[79], 0x0F, "blocks: block 4 byte 15");

	f_unlink(TEST_PATH);
}

/* Units past the caller's buffer are dropped, not written out of bounds. */
static void test_load_dump_limits(void)
{
	uint8_t dump[8];                 /* room for exactly two 4-byte pages */
	uint8_t valid[32];
	uint16_t unit_size = 0xFFFF;
	uint16_t count;

	printf("test_load_dump_limits\n");
	f_unlink(TEST_PATH);

	write_file(TEST_PATH,
	           "Filetype: Flipper NFC device\n"
	           "Version: 4\n"
	           "UID: 04 68 95 71\n"
	           "Page 0: 04 68 95 71\n"
	           "Page 1: FA 5C 64 80\n"
	           "Page 2: AA BB CC DD\n"
	           "Page 3: EE FF 00 11\n");

	memset(dump, 0, sizeof(dump));
	memset(valid, 0, sizeof(valid));
	count = flipper_nfc_load_dump(TEST_PATH, dump, sizeof(dump), valid, &unit_size);

	CHECK_EQ_INT(unit_size, 4, "limits: unit size is 4");
	CHECK_EQ_INT(count, 2, "limits: count stops at what fits");
	CHECK_EQ_INT(count_valid(valid, 32), 2, "limits: only stored pages marked valid");
	CHECK_EQ_INT(dump[7], 0x80, "limits: last in-bounds byte written");

	/* A header-only file yields nothing and reports no unit size. */
	write_file(TEST_PATH,
	           "Filetype: Flipper NFC device\nVersion: 4\nUID: 04 68 95 71\n");
	unit_size = 0xFFFF;
	memset(valid, 0, sizeof(valid));
	count = flipper_nfc_load_dump(TEST_PATH, dump, sizeof(dump), valid, &unit_size);
	CHECK_EQ_INT(count, 0, "limits: no dump lines -> zero units");
	CHECK_EQ_INT(unit_size, 0, "limits: no dump lines -> unit size zero");
	CHECK_EQ_INT(count_valid(valid, 32), 0, "limits: no dump lines -> nothing valid");

	/* Guards. */
	CHECK_EQ_INT(flipper_nfc_load_dump(NULL, dump, sizeof(dump), valid, &unit_size), 0,
	             "limits: NULL path");
	CHECK_EQ_INT(flipper_nfc_load_dump(TEST_PATH, NULL, sizeof(dump), valid, &unit_size), 0,
	             "limits: NULL dump buffer");
	CHECK_EQ_INT(flipper_nfc_load_dump(TEST_PATH, dump, sizeof(dump), NULL, &unit_size), 0,
	             "limits: NULL valid bitmap");
	CHECK_EQ_INT(flipper_nfc_load_dump(TEST_PATH, dump, sizeof(dump), valid, NULL), 0,
	             "limits: NULL unit size");

	f_unlink(TEST_PATH);
	CHECK_EQ_INT(flipper_nfc_load_dump(TEST_PATH, dump, sizeof(dump), valid, &unit_size), 0,
	             "limits: missing file");
}

int main(void)
{
	printf("== Flipper .nfc host tests ==\n");

	test_card_roundtrip();
	test_load_flipper_file();
	test_load_rejects();
	test_parse_type();
	test_load_dump_pages();
	test_load_dump_blocks();
	test_load_dump_limits();

	printf("== %d checks, %d failures ==\n", g_checks, g_failures);
	return (g_failures == 0) ? 0 : 1;
}
