#!/bin/sh
# Host unit test for the Universal Remotes pure helpers.
#
# T1: compiles the REAL m1_csrc/m1_uremote_match.c (case-insensitive record
# matcher) and runs the assertions. Later tasks add m1_uremote_layout.c
# (compiled with -DM1_UREMOTE_HOST_TEST so u8g2_uint_t is shimmed). Does NOT
# touch the firmware build.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
CC="${CC:-cc}"
BIN="$DIR/test_uremote_panel"

"$CC" -std=c11 -Wall -Wextra -O0 -g -DM1_UREMOTE_HOST_TEST \
	-I"$DIR" -I"$ROOT/m1_csrc" \
	"$DIR/test_uremote_panel.c" \
	"$ROOT/m1_csrc/m1_uremote_match.c" \
	-o "$BIN"

"$BIN"
