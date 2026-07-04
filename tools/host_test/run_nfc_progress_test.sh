#!/bin/sh
# Host unit test for the NFC read-progress result classifier.
#
# Compiles test_nfc_read_progress.c against the REAL dependency-free header
# NFC/NFC_drv/legacy/nfc_read_progress.h and runs the boundary assertions for
# mfc_classify_result(). Does NOT touch the firmware build.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CC="${CC:-cc}"
BIN="$DIR/test_nfc_read_progress"

"$CC" -std=c11 -Wall -Wextra -O0 -g \
	-I"$ROOT/NFC/NFC_drv/legacy" \
	"$DIR/test_nfc_read_progress.c" \
	-o "$BIN"
"$BIN"
