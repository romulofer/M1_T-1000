#!/bin/sh
# Host unit tests for the message-box layout core.
#
# Compiles the REAL m1_csrc/m1_msgbox_layout.c against a len*6 stub measure
# callback and runs the layout assertions (SPEC.md acceptance criteria 1-3).
# The core is pure — no u8g2 / FreeRTOS / FatFs — so no shims are needed. This
# does NOT touch the firmware build.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CC="${CC:-cc}"
BIN="$DIR/test_msgbox"

"$CC" -std=c11 -Wall -Wextra -O0 -g \
	-I"$ROOT/m1_csrc" \
	"$DIR/test_msgbox.c" \
	"$ROOT/m1_csrc/m1_msgbox_layout.c" \
	-o "$BIN"

"$BIN"
