/*
 * B0 — on-air oracle harness (RISK GATE).
 *
 * Compiles/links the REAL Infrared/irsnd.c (via irsnd_host.c) against thin
 * HAL / main.h / m1_infrared.h shadows, calls the genuine
 * irsnd_generate_tx_data(), and asserts the packed on-air bytes it produces in
 * ir_tx_buffer[]. This proves the actual firmware encoder runs on host, so the
 * per-protocol byte-exact oracles in B1..B3 can be pinned against it instead of
 * a hand-copied encoder.
 *
 * B0 scope is a single known-good assertion: Samsung32 fed the ALREADY-EXPANDED
 * 2010-Samsung power vector (0x0707 / 0xFD02) must pack to E0 E0 40 BF — the
 * exact frame a real Samsung remote sends. The shared expander and the
 * canonical-input oracles (0x0007/0x0002 -> same frame) arrive in B1.
 *
 * Host-only (IRSND_HOST_TEST). The firmware build never sees this file.
 */
#include <stdio.h>
#include <string.h>

#include "irsnd.h"   /* IRMP_DATA, irsnd_generate_tx_data, irsnd_init, IRMP_*_PROTOCOL */

/* Exposed by irsnd_host.c: pointer to the real encoder's packed frame buffer. */
extern const uint8_t *irsnd_host_tx_buffer(void);

static int failures;

/*
 * Encode one IRMP_DATA vector. irsnd_generate_tx_data() latches ir_tx_active
 * and refuses a second frame until re-init, so re-init before every vector.
 * irsnd_init() only resets state — it touches no HAL.
 */
static void encode(IRMP_DATA d)
{
	static TIM_HandleTypeDef dummy;
	memset(&dummy, 0, sizeof dummy);
	irsnd_init(&dummy, 0);
	irsnd_generate_tx_data(d);
}

static void expect(const char *name, IRMP_DATA d, const uint8_t *want, int n)
{
	encode(d);
	const uint8_t *got = irsnd_host_tx_buffer();
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

int main(void)
{
	printf("== IR TX on-air oracle harness (real irsnd.c) ==\n");

	/* Samsung32 fed the already-expanded device/command bytes must produce the
	   real 2010-Samsung power frame. This is the go/no-go proof that the vendor
	   encoder links and runs on host. */
	IRMP_DATA samsung_expanded = {
		.protocol = IRMP_SAMSUNG32_PROTOCOL,
		.address  = 0x0707,   /* device byte duplicated (0x07,0x07) */
		.command  = 0xFD02,   /* ~cmd check byte + cmd (0xFD,0x02)  */
		.flags    = 0,
	};
	static const uint8_t samsung_power[4] = { 0xE0, 0xE0, 0x40, 0xBF };
	expect("Samsung32 0x0707/0xFD02 (2010 power)", samsung_expanded, samsung_power, 4);

	if (failures) {
		printf("ir_tx_frames: %d FAILURE(S)\n", failures);
		return 1;
	}
	printf("ir_tx_frames: all tests passed\n");
	return 0;
}
