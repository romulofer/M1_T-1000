#!/bin/sh
# Host known-answer test for the software Crypto-1 forward cipher.
#
# Compiles the REAL NFC/NFC_drv/legacy/mfc_crypto1.c against minimal RFAL stubs
# (tools/host_test/stubs) and runs a published mfkey32v2 known-answer vector
# through the cipher core. Proves crypto1_init / crypto1_word /
# mfc_prng_successor are correct, isolating the on-card MIFARE Classic dump
# failure to the RF layer (encrypted-parity framing). Does NOT touch the
# firmware build.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CC="${CC:-cc}"
BIN="$DIR/test_mfc_crypto1"

"$CC" -std=c11 -Wall -Wextra -O0 -g \
	-I"$DIR/stubs" \
	-I"$ROOT/NFC/NFC_drv/legacy" \
	"$DIR/test_mfc_crypto1.c" \
	"$ROOT/NFC/NFC_drv/legacy/mfc_crypto1.c" \
	-o "$BIN"
"$BIN"
