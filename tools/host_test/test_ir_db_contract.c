/* See COPYING.txt for license details. */

/*
 * test_ir_db_contract.c  (HOST INTEGRATION TEST)
 *
 * Checks the contract between three things that only meet on a real device:
 *
 *   1. the firmware's own constants  (m1_ir_universal.h: IRDB root, the three
 *      dashboard power-database paths, the 64-command panel limit),
 *   2. the shipped ir_database/ tree that gets copied to the SD card,
 *   3. the real parser and protocol mapper (flipper_ir.c) plus the real IRSND
 *      encoder configuration (Infrared/irsndconfig.h).
 *
 * Unit tests cannot catch this class of defect: every layer is individually
 * correct while the combination silently fails on the device — a dashboard
 * action pointing at a file that is not on the card ("signal not located"), a
 * remote whose buttons scroll past the 64-command array, or a database entry
 * whose protocol the compiled IRSND encoder cannot transmit at all.
 *
 * Usage:  test_ir_db_contract <ir_database_root> <file.ir>...
 *
 * The .ir file list is supplied by the caller (validate.sh sweeps the tree), so
 * this program never walks directories itself.
 *
 * M1 Project — host test harness
 */

#include "m1_ir_universal.h"
#include "flipper_ir.h"
#include "flipper_file.h"
#include "irsnd.h"

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

#define CHECK_CTX(cond, msg, ctx)                                              \
	do {                                                                       \
		g_checks++;                                                            \
		if (!(cond)) {                                                         \
			g_failures++;                                                      \
			printf("  FAIL: %s [%s]  (%s:%d)\n", (msg), (ctx), __FILE__, __LINE__); \
		}                                                                      \
	} while (0)

/*************************** F I X T U R E S *********************************/

#define MAX_SIGNALS_PER_FILE   256

/* Protocols the firmware maps a database name onto but the compiled IRSND
 * encoder cannot transmit. Each entry is a KNOWN gap: the affected codes are
 * silently skipped on the device. A protocol that turns up unsupported and is
 * NOT listed here fails the run — that is a new regression, not a known gap.
 * Remove an entry once its IRSND_SUPPORT_* flag is switched on. */
typedef struct {
	uint8_t     irmp_id;
	const char *label;
	const char *note;
} known_gap_t;

static const known_gap_t k_known_gaps[] = {
	{ IRMP_RCCAR_PROTOCOL, "RCA / RCCAR",
	  "IRSND_SUPPORT_RCCAR_PROTOCOL is 0 — RCA codes cannot be transmitted" },
};
#define K_KNOWN_GAP_COUNT ((int)(sizeof(k_known_gaps) / sizeof(k_known_gaps[0])))

static int g_gap_hits[K_KNOWN_GAP_COUNT];

/* Does the compiled IRSND encoder support this IRMP protocol id?
 * Mirrors the #if guards in Infrared/irsnd.c, reading the REAL flags from
 * irsndconfig.h so flipping one there updates this test automatically. */
static bool irsnd_supports(uint8_t irmp_id)
{
	switch (irmp_id) {
	case IRMP_SIRCS_PROTOCOL:     return IRSND_SUPPORT_SIRCS_PROTOCOL != 0;
	case IRMP_NEC_PROTOCOL:       return IRSND_SUPPORT_NEC_PROTOCOL != 0;
	case IRMP_APPLE_PROTOCOL:     return IRSND_SUPPORT_NEC_PROTOCOL != 0; /* Apple rides the NEC encoder */
	case IRMP_SAMSUNG32_PROTOCOL: return IRSND_SUPPORT_SAMSUNG_PROTOCOL != 0;
	case IRMP_SAMSUNG48_PROTOCOL: return IRSND_SUPPORT_SAMSUNG48_PROTOCOL != 0;
	case IRMP_KASEIKYO_PROTOCOL:  return IRSND_SUPPORT_KASEIKYO_PROTOCOL != 0;
	case IRMP_DENON_PROTOCOL:     return IRSND_SUPPORT_DENON_PROTOCOL != 0;
	case IRMP_RC5_PROTOCOL:       return IRSND_SUPPORT_RC5_PROTOCOL != 0;
	case IRMP_RC6_PROTOCOL:       return IRSND_SUPPORT_RC6_PROTOCOL != 0;
	case IRMP_JVC_PROTOCOL:       return IRSND_SUPPORT_JVC_PROTOCOL != 0;
	case IRMP_NEC16_PROTOCOL:     return IRSND_SUPPORT_NEC16_PROTOCOL != 0;
	case IRMP_NEC42_PROTOCOL:     return IRSND_SUPPORT_NEC42_PROTOCOL != 0;
	case IRMP_NOKIA_PROTOCOL:     return IRSND_SUPPORT_NOKIA_PROTOCOL != 0;
	case IRMP_BOSE_PROTOCOL:      return IRSND_SUPPORT_BOSE_PROTOCOL != 0;
	case IRMP_RCCAR_PROTOCOL:     return IRSND_SUPPORT_RCCAR_PROTOCOL != 0;
	case IRMP_LGAIR_PROTOCOL:     return IRSND_SUPPORT_LGAIR_PROTOCOL != 0;
	case IRMP_RCMM32_PROTOCOL:    return IRSND_SUPPORT_RCMM_PROTOCOL != 0;
	default:                      return false;
	}
}

/* Record an unsupported protocol: known gap -> tallied, anything else -> fail. */
static void note_unsupported(uint8_t irmp_id, const char *where, const char *signal_name)
{
	int i;

	for (i = 0; i < K_KNOWN_GAP_COUNT; i++) {
		if (k_known_gaps[i].irmp_id == irmp_id) {
			g_gap_hits[i]++;
			return;
		}
	}

	g_failures++;
	g_checks++;
	printf("  FAIL: protocol id %u has no IRSND encoder [%s: %s]\n",
	       (unsigned)irmp_id, where, signal_name);
}

/* Translate a firmware SD path ("0:/IR/TV/x.ir") into a path under the repo's
 * ir_database root. Returns false if the path does not start at the IRDB root,
 * which is itself a contract violation. */
static bool sd_path_to_host(const char *sd_path, const char *db_root,
                            char *out, size_t out_len)
{
	const size_t root_len = strlen(IR_UNIVERSAL_IRDB_ROOT);

	if (strncmp(sd_path, IR_UNIVERSAL_IRDB_ROOT "/", root_len + 1) != 0)
		return false;

	if (snprintf(out, out_len, "%s%s", db_root, sd_path + root_len) >= (int)out_len)
		return false;

	return true;
}

/* Read every signal of a .ir file. Returns the count, or -1 if it will not open. */
static int read_signals(const char *host_path, flipper_ir_signal_t *out, int max)
{
	flipper_file_t ff;
	int n = 0;

	if (!flipper_ir_open(&ff, host_path))
		return -1;

	while (n < max && flipper_ir_read_signal(&ff, &out[n]))
		n++;

	ff_close(&ff);
	return n;
}

/*************************** T E S T S ***************************************/

/* Every dashboard power-blast database must be present, parseable, and made
 * entirely of codes the device can actually transmit. */
static void test_dashboard_databases(const char *db_root)
{
	static const struct {
		const char *label;
		const char *sd_path;
	} k_dashboard_dbs[] = {
		{ "Power Off TVs",  IR_POWER_DB_PATH },
		{ "Power Off A/V",  IR_AUDIO_DB_PATH },
		{ "Power Off A/V",  IR_PROJ_DB_PATH },
	};
	static flipper_ir_signal_t sigs[MAX_SIGNALS_PER_FILE];

	char host_path[512];
	int i;
	int n;
	int s;

	printf("test_dashboard_databases\n");

	for (i = 0; i < (int)(sizeof(k_dashboard_dbs) / sizeof(k_dashboard_dbs[0])); i++) {
		const char *sd_path = k_dashboard_dbs[i].sd_path;

		CHECK_CTX(sd_path_to_host(sd_path, db_root, host_path, sizeof(host_path)),
		          "dashboard path sits under the IRDB root", sd_path);

		n = read_signals(host_path, sigs, MAX_SIGNALS_PER_FILE);
		if (n < 0) {
			g_checks++;
			g_failures++;
			printf("  FAIL: %s -> %s is missing from ir_database (device would show \"not located\")\n",
			       k_dashboard_dbs[i].label, sd_path);
			continue;
		}

		CHECK_CTX(n > 0, "dashboard database carries at least one code", sd_path);
		CHECK_CTX(n <= IR_UNIVERSAL_MAX_CMDS,
		          "dashboard database fits the 64-command panel array", sd_path);

		for (s = 0; s < n; s++) {
			CHECK_CTX(sigs[s].valid, "dashboard code parsed as valid", sd_path);
			CHECK_CTX(sigs[s].type == FLIPPER_IR_SIGNAL_PARSED,
			          "dashboard code is a parsed protocol, not raw", sd_path);

			if (sigs[s].type != FLIPPER_IR_SIGNAL_PARSED)
				continue;

			CHECK_CTX(sigs[s].parsed.protocol != IRMP_UNKNOWN_PROTOCOL,
			          "dashboard code protocol is known to the firmware mapper", sd_path);

			if (!irsnd_supports(sigs[s].parsed.protocol))
				note_unsupported(sigs[s].parsed.protocol, sd_path, sigs[s].name);
		}

		printf("  OK: %-40s %2d codes  (%s)\n", sd_path, n, k_dashboard_dbs[i].label);
	}
}

/* Sweep every shipped .ir file against the limits the universal remote screen
 * imposes and against the encoder that has to send the codes. */
static void test_shipped_database(int file_count, char *const *files)
{
	static flipper_ir_signal_t sigs[MAX_SIGNALS_PER_FILE];

	int f;
	int total_signals = 0;
	int raw_signals = 0;

	printf("test_shipped_database (%d files)\n", file_count);

	/* This sweep visits every shipped file, including the three dashboard
	 * databases checked above, so restart the tally here to count each
	 * affected code once. */
	memset(g_gap_hits, 0, sizeof(g_gap_hits));

	for (f = 0; f < file_count; f++) {
		const char *path = files[f];
		int n = read_signals(path, sigs, MAX_SIGNALS_PER_FILE);
		int s;
		int t;

		if (n < 0) {
			g_checks++;
			g_failures++;
			printf("  FAIL: %s does not open as a Flipper .ir file\n", path);
			continue;
		}

		CHECK_CTX(n > 0, "file carries at least one signal", path);
		CHECK_CTX(n <= IR_UNIVERSAL_MAX_CMDS,
		          "file fits the 64-command panel array (extra buttons are dropped on device)",
		          path);

		for (s = 0; s < n; s++) {
			size_t name_len = strlen(sigs[s].name);

			CHECK_CTX(sigs[s].valid, "signal parsed as valid", path);
			CHECK_CTX(name_len > 0, "signal has a name", path);
			CHECK_CTX(name_len < FLIPPER_IR_NAME_MAX_LEN - 1,
			          "signal name is not truncated by the 32-byte name field", path);

			/* Duplicate names make the panel ambiguous: two buttons, same label. */
			for (t = 0; t < s; t++) {
				if (strcmp(sigs[s].name, sigs[t].name) == 0) {
					g_checks++;
					g_failures++;
					printf("  FAIL: duplicate signal name \"%s\" [%s]\n", sigs[s].name, path);
					break;
				}
			}

			if (sigs[s].type == FLIPPER_IR_SIGNAL_PARSED) {
				CHECK_CTX(sigs[s].parsed.protocol != IRMP_UNKNOWN_PROTOCOL,
				          "protocol name maps to an IRMP id", path);

				if (sigs[s].parsed.protocol != IRMP_UNKNOWN_PROTOCOL &&
				    !irsnd_supports(sigs[s].parsed.protocol))
					note_unsupported(sigs[s].parsed.protocol, path, sigs[s].name);
			} else {
				raw_signals++;
				CHECK_CTX(sigs[s].raw.sample_count > 0, "raw signal has samples", path);
				CHECK_CTX(sigs[s].raw.frequency >= 30000 && sigs[s].raw.frequency <= 60000,
				          "raw carrier frequency is in the IR band", path);
			}
		}

		total_signals += n;
	}

	printf("  swept %d signals (%d raw) across %d files\n",
	       total_signals, raw_signals, file_count);
}

/* The protocol mapper must be self-consistent: a name maps to an id, and the
 * reverse lookup of that id maps back to the same id (aliases collapse, but
 * never onto a different protocol). */
static void test_protocol_mapper_roundtrip(void)
{
	static const char *const k_db_protocols[] = {
		"NEC", "NECext", "NEC42", "NEC16", "Samsung32", "RC5", "RC5X", "RC6",
		"SIRC", "SIRC15", "SIRC20", "Kaseikyo", "RCA", "Pioneer", "Denon",
		"JVC", "Sharp", "Panasonic", "LG", "Samsung", "Apple", "Nokia",
		"Bose", "Samsung48", "RCMM"
	};
	int i;

	printf("test_protocol_mapper_roundtrip\n");

	for (i = 0; i < (int)(sizeof(k_db_protocols) / sizeof(k_db_protocols[0])); i++) {
		uint8_t id = flipper_ir_proto_to_irmp(k_db_protocols[i]);
		const char *back;

		CHECK_CTX(id != IRMP_UNKNOWN_PROTOCOL, "protocol name is mapped", k_db_protocols[i]);
		if (id == IRMP_UNKNOWN_PROTOCOL)
			continue;

		back = flipper_ir_irmp_to_proto(id);
		CHECK_CTX(back != NULL && back[0] != '\0',
		          "IRMP id maps back to a Flipper name", k_db_protocols[i]);
		CHECK_CTX(flipper_ir_proto_to_irmp(back) == id,
		          "reverse-mapped name resolves to the same protocol id", k_db_protocols[i]);
	}

	/* Case-insensitivity: the database is hand-edited and Flipper dumps vary. */
	CHECK(flipper_ir_proto_to_irmp("nec") == IRMP_NEC_PROTOCOL, "lower-case protocol name maps");
	CHECK(flipper_ir_proto_to_irmp("SAMSUNG32") == IRMP_SAMSUNG32_PROTOCOL, "upper-case protocol name maps");
	CHECK(flipper_ir_proto_to_irmp("Frobnicator") == IRMP_UNKNOWN_PROTOCOL, "unknown protocol stays unknown");
}

int main(int argc, char *argv[])
{
	int i;

	if (argc < 3) {
		printf("usage: %s <ir_database_root> <file.ir>...\n", argv[0]);
		return 2;
	}

	printf("== IR database <-> firmware contract (integration) ==\n");
	printf("   IRDB root on device: %s\n", IR_UNIVERSAL_IRDB_ROOT);
	printf("   ir_database on host: %s\n", argv[1]);

	test_dashboard_databases(argv[1]);
	test_shipped_database(argc - 2, &argv[2]);
	test_protocol_mapper_roundtrip();

	for (i = 0; i < K_KNOWN_GAP_COUNT; i++) {
		if (g_gap_hits[i] > 0)
			printf("  KNOWN GAP: %s — %d code(s): %s\n",
			       k_known_gaps[i].label, g_gap_hits[i], k_known_gaps[i].note);
		else
			printf("  NOTE: known gap %s no longer occurs in the database — "
			       "drop it from k_known_gaps[]\n", k_known_gaps[i].label);
	}

	printf("== %d checks, %d failures ==\n", g_checks, g_failures);
	return (g_failures == 0) ? 0 : 1;
}
