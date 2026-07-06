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
#   5. Universal Remotes: record-name matcher + 2-col grid geometry
#      (test_uremote_panel).
#   6. build_universal_ir.py generator unit test (parse / normalize / dedup /
#      emit), when python3 is available.
#   7. IR TX on-air oracle harness (test_ir_tx_frames): compiles the REAL
#      Infrared/irsnd.c on host against thin HAL / m1_infrared shadows and
#      asserts the packed ir_tx_buffer[] frames (Samsung E0 E0 40 BF; B1+ pin
#      the canonical-input and per-protocol oracles here).
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

# Universal Remotes: record-name matcher + 2-col grid geometry (compiles the
# real m1_uremote_match.c + m1_uremote_layout.c with the host shim).
echo
echo "== Universal Remotes matcher + panel geometry test =="
sh "$DIR/test_uremote_panel/run.sh"

# build_universal_ir.py generator unit test (pure parse/normalize/dedup/emit
# core). Skipped with a note when python3 is unavailable on the bench.
echo
echo "== build_universal_ir generator unit test =="
if command -v python3 >/dev/null 2>&1; then
	python3 "$ROOT/tools/test_build_universal_ir.py"
else
	echo "  (skipped: python3 not found)"
fi

# IR TX on-air oracle harness (Task B0). Compiles the REAL Infrared/irsnd.c on
# host and asserts the frame it packs into ir_tx_buffer[]. -I stubs comes first
# so main.h + m1_infrared.h resolve to the thin host shadows; -I Infrared then
# supplies the real irsnd.h / irmp headers. irsnd.c is vendor code, so its
# wrapper compiles -w while the oracle itself stays -Wall -Wextra.
echo
echo "== IR TX on-air oracle harness (real irsnd.c) =="
IRTX_BIN="$DIR/test_ir_tx_frames"
IRTX_HOST_OBJ="$DIR/irsnd_host.o"
"$CC" -std=c11 -w -O0 -g -DIRSND_HOST_TEST=1 \
	-I"$DIR/stubs" -I"$ROOT/Infrared" \
	-c "$DIR/irsnd_host.c" -o "$IRTX_HOST_OBJ"
"$CC" -std=c11 -Wall -Wextra -O0 -g -DIRSND_HOST_TEST=1 \
	-I"$DIR/stubs" -I"$ROOT/Infrared" \
	"$DIR/test_ir_tx_frames.c" \
	"$IRTX_HOST_OBJ" \
	"$DIR/stubs/hal_stub.c" \
	-o "$IRTX_BIN"
"$IRTX_BIN"
rm -f "$IRTX_HOST_OBJ"
