#!/bin/sh
# Host unit test for the MIFARE Classic SAK -> layout mapping.
#
# Compiles test_mfc_sak_layout.c against the REAL dependency-free header
# NFC/NFC_drv/legacy/nfc_read_progress.h and runs the SAK classification /
# sector-block layout assertions. Does NOT touch the firmware build.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CC="${CC:-cc}"
BIN="$DIR/test_mfc_sak_layout"

"$CC" -std=c11 -Wall -Wextra -O0 -g \
	-I"$ROOT/NFC/NFC_drv/legacy" \
	"$DIR/test_mfc_sak_layout.c" \
	-o "$BIN"
"$BIN"
