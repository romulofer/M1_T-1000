#!/bin/sh
# Host unit tests for the M1 file-format and pure-logic layers.
#
# Compiles the REAL m1_csrc sources against the FatFs/IRMP/HAL host shims in
# this directory and runs the assertion suites. This is the host-side
# verification called for by SPEC.md ("parse the written .ir with the same
# flipper_ir logic"). It does NOT touch the firmware build.
#
# Suites:
#   flipper_file    key-value line parser + write helpers (shared by all formats)
#   flipper_ir      .ir append / rewrite / rename / delete round-trips
#   flipper_subghz  .sub RAW + Key round-trips, preset/band mapping
#   flipper_nfc     .nfc card header round-trip + Page/Block dump reader
#   subghz_rssi     RSSI-bar geometry helpers (pure math, no file I/O)
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CC="${CC:-cc}"

# Shim headers (ff.h, irmp.h, stm32h5xx_hal.h) must resolve BEFORE anything
# else, so -I"$DIR" comes first; m1_csrc supplies the module headers.
CFLAGS="-std=c11 -Wall -Wextra -O0 -g"
INCS="-I$DIR -I$ROOT/m1_csrc"

# Scratch directory shared by the file-writing suites, so no test artifact ever
# lands in the repo. Removed on exit, including on failure.
SCRATCH="$(mktemp -d)"
trap 'rm -rf "$SCRATCH"' EXIT

# build_and_run <test-name> <module source>...
#   Builds $DIR/<test-name> from $DIR/<test-name>.c plus the given m1_csrc
#   modules and the FatFs shim, then runs it inside the scratch directory.
build_and_run() {
	name="$1"
	shift
	# shellcheck disable=SC2086  # CFLAGS/INCS are intentionally word-split
	"$CC" $CFLAGS $INCS "$DIR/$name.c" "$DIR/ff_shim.c" "$@" -o "$DIR/$name"
	( cd "$SCRATCH" && "$DIR/$name" )
}

build_and_run test_flipper_file \
	"$ROOT/m1_csrc/flipper_file.c"

build_and_run test_flipper_ir \
	"$ROOT/m1_csrc/flipper_file.c" \
	"$ROOT/m1_csrc/flipper_ir.c"

build_and_run test_flipper_subghz \
	"$ROOT/m1_csrc/flipper_file.c" \
	"$ROOT/m1_csrc/flipper_subghz.c"

build_and_run test_flipper_nfc \
	"$ROOT/m1_csrc/flipper_file.c" \
	"$ROOT/m1_csrc/flipper_nfc.c"

# SubGHz live-RSSI-bar geometry helpers (Task 1). Pure math, header-free
# snippet shared with m1_csrc/m1_sub_ghz.c — no firmware sources, no file I/O.
SUBGHZ_BIN="$DIR/test_subghz_rssi"
# shellcheck disable=SC2086
"$CC" $CFLAGS -I"$ROOT/m1_csrc" "$DIR/test_subghz_rssi.c" -o "$SUBGHZ_BIN"
"$SUBGHZ_BIN"
