#!/bin/sh
# Host geometry test for the centered "Please wait..." box (hourglass rework).
#
# Compiles the REAL m1_csrc/m1_please_wait_layout.c with -DM1_PW_HOST_TEST so
# u8g2_uint_t is shimmed (no u8g2 renderer needed) and runs the pure-geometry
# assertions. Does NOT touch the firmware build.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
CC="${CC:-cc}"
BIN="$DIR/test_please_wait_box"

"$CC" -std=c11 -Wall -Wextra -O0 -g -DM1_PW_HOST_TEST \
	-I"$DIR" -I"$ROOT/m1_csrc" \
	"$DIR/test_please_wait_box.c" \
	"$ROOT/m1_csrc/m1_please_wait_layout.c" \
	-o "$BIN"

"$BIN"
