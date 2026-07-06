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

/* Byte i of ir_tx_buffer[] is the logical protocol byte bit-reversed (verified
   against Samsung: 0xE0 == bitrev8(0x07)). Used to build NEC oracles from the
   protocol frame definition, independent of the expander under test. */
static uint8_t bitrev8(uint8_t b)
{
	uint8_t r = 0;
	for (int i = 0; i < 8; i++) { r = (uint8_t)((r << 1) | (b & 1u)); b = (uint8_t)(b >> 1); }
	return r;
}

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
	snprintf(label, sizeof label, "shipped \"%s\" (%s)", record, path);

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

/* On-air standard NEC is {addr, ~addr, cmd, ~cmd}; the buffer holds each byte
   bit-reversed. Reference is built from the protocol definition, not the
   expander, then driven through expand + real encode. */
static void expect_nec(const char *name, uint16_t addr_lo, uint16_t cmd_lo)
{
	uint8_t a = (uint8_t)addr_lo, c = (uint8_t)cmd_lo;
	uint8_t want[4] = { bitrev8(a), bitrev8((uint8_t)~a), bitrev8(c), bitrev8((uint8_t)~c) };
	expect_expanded(name, IRMP_NEC_PROTOCOL, addr_lo, cmd_lo, want, 4);
}

/* Assert the expander leaves an already-complete code untouched (idempotency /
   extended-address protection): re-expansion and 16-bit addresses are no-ops. */
static void expect_noop(const char *name, uint8_t proto, uint16_t addr, uint16_t cmd)
{
	uint16_t a = addr, c = cmd;
	ir_expand_parsed_code(proto, &a, &c);
	if (a != addr || c != cmd) {
		failures++;
		printf("  FAIL %s: expander mutated 0x%04X/0x%04X -> 0x%04X/0x%04X\n",
		       name, addr, cmd, a, c);
	} else {
		printf("  ok   %s: 0x%04X/0x%04X unchanged\n", name, addr, cmd);
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
		printf("  (skip Samsung shipped oracle: no .ir path argument)\n");

	/* B2 — standard NEC: the expander synthesizes ~addr into the address high
	   byte; IRSND derives the ~command byte itself, so the command is untouched. */
	expect_nec("B2  NEC 0x04/0x08 (expand ~addr)", 0x0004, 0x0008);

	/* B2 — idempotency: an already-16-bit address (an expanded standard-NEC
	   clone, or a genuine extended-NEC address) is left untouched, so a real
	   16-bit address is never corrupted. */
	expect_noop("B2  NEC 0xFB04/0x0008 expanded (idempotent)", IRMP_NEC_PROTOCOL, 0xFB04, 0x0008);
	expect_noop("B2  NECext 0xABCD/0x0012 (16-bit addr untouched)", IRMP_NEC_PROTOCOL, 0xABCD, 0x0012);

	/* B2 — shipped NEC record "On" (address 0x00, command 0x40) end to end. */
	if (argc > 2) {
		uint8_t nec_on[4] = { bitrev8(0x00), bitrev8(0xFF), bitrev8(0x40), bitrev8(0xBF) };
		expect_shipped(argv[2], "On", nec_on, 4);
	} else {
		printf("  (skip NEC shipped oracle: no second .ir path argument)\n");
	}

	if (failures) {
		printf("ir_tx_frames: %d FAILURE(S)\n", failures);
		return 1;
	}
	printf("ir_tx_frames: all tests passed\n");
	return 0;
}
