#!/bin/sh
# Host regression sweep for the IR file layer + shipped .ir database.
#
# Three gates, all host-side (no firmware build, no hardware):
#   1. The host unit suites (file layer, .ir, .sub, .nfc, RSSI geometry).
#   2. Every shipped ir_database/**/*.ir parses (valid header + >= 1 signal) via
#      the standalone validator — the same check applied to M1-authored output.
#   3. The integration contract between the firmware's own constants, the
#      shipped database and the compiled IRSND encoder: dashboard power-blast
#      paths resolve to real files, no remote exceeds the 64-command panel
#      array, and every database protocol has an encoder that can transmit it.
#
# Run from anywhere; paths resolve against the repo root.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CC="${CC:-cc}"

echo "== 1/3: host unit suites =="
sh "$DIR/run_tests.sh"

echo
echo "== 2/3: ir_database .ir validation sweep =="
VAL="$DIR/validate_ir"
"$CC" -std=c11 -Wall -Wextra -O0 \
	-I"$DIR" -I"$ROOT/m1_csrc" \
	"$DIR/validate_ir.c" \
	"$DIR/ff_shim.c" \
	"$ROOT/m1_csrc/flipper_file.c" \
	"$ROOT/m1_csrc/flipper_ir.c" \
	-o "$VAL"

# Collect every shipped .ir file and validate in one pass.
find "$ROOT/ir_database" -name '*.ir' -print0 | xargs -0 "$VAL"

echo
echo "== 3/3: IR database <-> firmware contract =="
# -I"$ROOT/Infrared" brings in the real irsnd.h / irsndconfig.h, so the encoder
# support flags under test are the ones the firmware is compiled with. The host
# shims (main.h, app_freertos.h, queue.h, cmsis_os.h, stm32h5xx_hal.h) stand in
# for the board headers that chain pulls along.
CONTRACT="$DIR/test_ir_db_contract"
"$CC" -std=c11 -Wall -Wextra -O0 -g \
	-I"$DIR" -I"$ROOT/m1_csrc" -I"$ROOT/Infrared" \
	"$DIR/test_ir_db_contract.c" \
	"$DIR/ff_shim.c" \
	"$ROOT/m1_csrc/flipper_file.c" \
	"$ROOT/m1_csrc/flipper_ir.c" \
	-o "$CONTRACT"

find "$ROOT/ir_database" -name '*.ir' -print0 | xargs -0 "$CONTRACT" "$ROOT/ir_database"

echo
echo "== regression sweep passed =="
