#!/bin/sh
# Host geometry test for the Custom Remotes UI rework helpers.
#
# Compiles the REAL m1_csrc/m1_card_list_layout.c with -DM1_CARD_HOST_TEST so
# u8g2_uint_t is shimmed (no u8g2 renderer needed) and runs the pure-geometry
# assertions. Does NOT touch the firmware build.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../../.." && pwd)"
CC="${CC:-cc}"
BIN="$DIR/test_remotes_ui"

"$CC" -std=c11 -Wall -Wextra -O0 -g -DM1_CARD_HOST_TEST -DM1_TX_HOST_TEST \
	-I"$DIR" -I"$ROOT/m1_csrc" \
	"$DIR/test_remotes_ui.c" \
	"$ROOT/m1_csrc/m1_card_list_layout.c" \
	"$ROOT/m1_csrc/m1_tx_status_layout.c" \
	-o "$BIN"

"$BIN"
