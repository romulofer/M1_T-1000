#!/bin/sh
# Host round-trip tests for the Flipper .ir file layer.
#
# Compiles the REAL m1_csrc/flipper_file.c + flipper_ir.c against the FatFs/IRMP
# host shims in this directory and runs the round-trip assertions. This is the
# host-side verification called for by SPEC.md ("parse the written .ir with the
# same flipper_ir logic"). It does NOT touch the firmware build.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CC="${CC:-cc}"
BIN="$DIR/test_flipper_ir"

# Shim headers (ff.h, irmp.h) must resolve BEFORE anything else, so -I"$DIR"
# comes first; m1_csrc supplies flipper_file.h / flipper_ir.h.
"$CC" -std=c11 -Wall -Wextra -O0 -g \
	-I"$DIR" -I"$ROOT/m1_csrc" \
	"$DIR/test_flipper_ir.c" \
	"$DIR/ff_shim.c" \
	"$ROOT/m1_csrc/flipper_file.c" \
	"$ROOT/m1_csrc/flipper_ir.c" \
	-o "$BIN"

# Run in a throwaway directory so scratch .ir files never touch the repo.
SCRATCH="$(mktemp -d)"
( cd "$SCRATCH" && "$BIN" )
rc=$?
rm -rf "$SCRATCH"
[ $rc -eq 0 ] || exit $rc

# SubGHz live-RSSI-bar geometry helpers (Task 1). Pure math, header-free
# snippet shared with m1_csrc/m1_sub_ghz.c — no firmware sources needed.
SUBGHZ_BIN="$DIR/test_subghz_rssi"
"$CC" -std=c11 -Wall -Wextra -O0 -g \
	-I"$ROOT/m1_csrc" \
	"$DIR/test_subghz_rssi.c" \
	-o "$SUBGHZ_BIN"
"$SUBGHZ_BIN"
rc=$?
[ $rc -eq 0 ] || exit $rc

# U2F/CTAP1 core: HMAC-SHA256 + SHA-256 known-answer vectors, and a
# REGISTER/AUTHENTICATE round trip verified against uECC_verify. Pure
# logic, no HAL/FreeRTOS/USB needed.
U2F_BIN="$DIR/test_u2f_core"
"$CC" -std=c11 -Wall -Wextra -O0 -g \
	-DuECC_PLATFORM=0 -DuECC_SUPPORTS_secp160r1=0 -DuECC_SUPPORTS_secp192r1=0 \
	-DuECC_SUPPORTS_secp224r1=0 -DuECC_SUPPORTS_secp256k1=0 -DuECC_SUPPORT_COMPRESSED_POINT=0 \
	-I"$ROOT/m1_csrc" -I"$ROOT/Middlewares/uECC" -I"$ROOT/Middlewares/sha256" \
	"$DIR/test_u2f_core.c" \
	"$ROOT/m1_csrc/m1_u2f_core.c" \
	"$ROOT/Middlewares/uECC/uECC.c" \
	"$ROOT/Middlewares/sha256/sha256.c" \
	-o "$U2F_BIN"
"$U2F_BIN"
exit $?
