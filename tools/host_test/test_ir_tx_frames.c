/*
 * IR TX on-air oracle harness.
 *
 * Compiles/links the REAL Infrared/irsnd.c (via irsnd_host.c) and the REAL
 * expander + .ir parser (m1_csrc/flipper_ir.c) against thin HAL / main.h /
 * m1_infrared.h shadows and the FatFs host shim, then asserts the exact on-air
 * bytes the genuine firmware path produces in ir_tx_buffer[]. No hand-copied
 * encoder: every oracle runs the same code the device runs.
 *
 * B0 (RISK GATE): the vendor encoder runs on host at all — Samsung32 fed the
 *   already-expanded 0x0707/0xFD02 packs to E0 E0 40 BF.
 * B1 (Samsung32): ir_expand_parsed_code() turns the DB-canonical 0x0007/0x0002
 *   into that same real 2010-Samsung power frame; is idempotent on an already-
 *   expanded value; and drives the shipped ir_database/TV/Samsung.ir "Power"
 *   record end to end through parse -> expand -> encode.
 *
 * Host-only (IRSND_HOST_TEST); the firmware build never sees this file.
 */
#include <stdio.h>
#include <string.h>

#include "irsnd.h"        /* IRMP_DATA, irsnd_generate_tx_data, irsnd_init, IRMP_*_PROTOCOL */
#include "flipper_ir.h"   /* ir_expand_parsed_code, flipper_ir_open/read_signal, ff_close */

/* Exposed by irsnd_host.c: pointer to the real encoder's packed frame buffer. */
extern const uint8_t *irsnd_host_tx_buffer(void);

static int failures;

/*
 * Encode one IRMP_DATA vector through the real irsnd.c and return the packed
 * buffer. irsnd_generate_tx_data() latches ir_tx_active and refuses a second
 * frame until re-init, so re-init before every vector. irsnd_init() only resets
 * state — it touches no HAL.
 */
static const uint8_t *encode(uint8_t proto, uint16_t addr, uint16_t cmd)
{
	static TIM_HandleTypeDef dummy;
	memset(&dummy, 0, sizeof dummy);
	irsnd_init(&dummy, 0);

	IRMP_DATA d;
	d.protocol = proto;
	d.address  = addr;
	d.command  = cmd;
	d.flags    = 0;
	irsnd_generate_tx_data(d);
	return irsnd_host_tx_buffer();
}

static void check(const char *name, const uint8_t *got, const uint8_t *want, int n)
{
	if (memcmp(got, want, (size_t)n) != 0) {
		failures++;
		printf("  FAIL %s: got", name);
		for (int i = 0; i < n; i++) printf(" %02X", got[i]);
		printf(" | want");
		for (int i = 0; i < n; i++) printf(" %02X", want[i]);
		printf("\n");
	} else {
		printf("  ok   %s:", name);
		for (int i = 0; i < n; i++) printf(" %02X", got[i]);
		printf("\n");
	}
}

/* Encode verbatim (no expander) — proves the vendor encoder runs on host. */
static void expect_raw(const char *name, uint8_t proto, uint16_t addr, uint16_t cmd,
                       const uint8_t *want, int n)
{
	check(name, encode(proto, addr, cmd), want, n);
}

/* Expand exactly as the firmware does at TX, then encode — the on-air oracle. */
static void expect_expanded(const char *name, uint8_t proto, uint16_t addr, uint16_t cmd,
                            const uint8_t *want, int n)
{
	ir_expand_parsed_code(proto, &addr, &cmd);
	check(name, encode(proto, addr, cmd), want, n);
}

/* Parse a named record from a shipped .ir file, run it through expand + encode,
   and assert — the real parser feeding the real encoder. */
static void expect_shipped(const char *path, const char *record,
                           const uint8_t *want, int n)
{
	char label[112];
	snprintf(label, sizeof label, "B1  shipped \"%s\" (%s)", record, path);

	flipper_file_t ctx;
	if (!flipper_ir_open(&ctx, path)) {
		failures++;
		printf("  FAIL %s: cannot open file\n", label);
		return;
	}

	flipper_ir_signal_t sig;
	int found = 0;
	while (flipper_ir_read_signal(&ctx, &sig)) {
		if (sig.type == FLIPPER_IR_SIGNAL_PARSED && strcmp(sig.name, record) == 0) {
			found = 1;
			expect_expanded(label, sig.parsed.protocol,
			                sig.parsed.address, sig.parsed.command, want, n);
			break;
		}
	}
	ff_close(&ctx);

	if (!found) {
		failures++;
		printf("  FAIL %s: record not found\n", label);
	}
}

int main(int argc, char **argv)
{
	printf("== IR TX on-air oracle harness (real irsnd.c + expander) ==\n");

	static const uint8_t samsung_power[4] = { 0xE0, 0xE0, 0x40, 0xBF };

	/* B0 — vendor encoder runs on host (already-expanded vector, no expander). */
	expect_raw("B0  Samsung32 0x0707/0xFD02 (raw encode)",
	           IRMP_SAMSUNG32_PROTOCOL, 0x0707, 0xFD02, samsung_power, 4);

	/* B1 — DB-canonical device/command expands to the real 2010 power frame. */
	expect_expanded("B1  Samsung32 0x0007/0x0002 (expand)",
	                IRMP_SAMSUNG32_PROTOCOL, 0x0007, 0x0002, samsung_power, 4);

	/* B1 — expanding an already-expanded value is a no-op (clones/replays safe). */
	expect_expanded("B1  Samsung32 0x0707/0xFD02 (idempotent)",
	                IRMP_SAMSUNG32_PROTOCOL, 0x0707, 0xFD02, samsung_power, 4);

	/* B1 — shipped Samsung.ir "Power" through the real parser + expander. */
	if (argc > 1)
		expect_shipped(argv[1], "Power", samsung_power, 4);
	else
		printf("  (skip shipped oracle: no .ir path argument)\n");

	if (failures) {
		printf("ir_tx_frames: %d FAILURE(S)\n", failures);
		return 1;
	}
	printf("ir_tx_frames: all tests passed\n");
	return 0;
}
