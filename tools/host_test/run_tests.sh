#!/bin/sh
# Host unit/round-trip test suite (no firmware build, no hardware).
#
#   1. Centered "Please wait..." box pure-geometry test (test_please_wait_box).
#   2. Custom Remotes UI geometry test (test_remotes_ui): card-list scroll
#      window / row / scrollbar math + tx-status card centering/stacking.
#   3. Flipper .ir file-layer round-trip test: compiles the REAL
#      m1_csrc/flipper_file.c + flipper_ir.c against the FatFs/IRMP host shims
#      and runs the round-trip assertions, per SPEC.md ("parse the written .ir
#      with the same flipper_ir logic").
#   4. SubGHz live-RSSI-bar geometry helpers.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CC="${CC:-cc}"

# pure-geometry unit test for the centered "Please wait..." box.
echo "== please-wait box geometry test =="
sh "$DIR/test_please_wait_box/run.sh"
echo

# Custom Remotes UI rework: card-list + tx-status card pure geometry helpers.
echo "== Custom Remotes UI geometry test =="
sh "$DIR/test_remotes_ui/run.sh"
echo

echo "== Flipper .ir round-trip suite =="

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
exit $?
