/*
 * Host-only wrapper that compiles the REAL Infrared/irsnd.c and exposes its
 * private packed transmit buffer to the on-air oracle (test_ir_tx_frames.c).
 *
 * Compiled with -w because irsnd.c is vendor code that warns under -Wall; the
 * oracle itself stays -Wall -Wextra. Built against the tools/host_test/stubs
 * shadows (main.h, m1_infrared.h, hal_stub.h). Never part of the firmware.
 */
#include <stdint.h>

#include "irsnd.c"   /* the real encoder; brings its static ir_tx_buffer[] into this TU */

/* The real encoder packs the on-air frame into the file-static ir_tx_buffer[].
   Hand it to the oracle so assertions run against the genuine bytes. */
const uint8_t *irsnd_host_tx_buffer(void)
{
	return ir_tx_buffer;
}
