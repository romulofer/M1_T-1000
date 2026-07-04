#!/bin/sh
# Host data-linter for the shipped MIFARE Classic key dictionary.
#
# Compiles test_mfc_dict.c (a faithful mirror of the firmware's 32-byte
# truncating line reader + key parser) and runs it against the shipped
# sdcard/NFC/system/mf_classic_dict.nfc. Proves the file is parseable
# on-device with no truncation drift and that well-known keys are present.
# Does NOT touch the firmware build.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CC="${CC:-cc}"
BIN="$DIR/test_mfc_dict"
DICT="${1:-$ROOT/sdcard/NFC/system/mf_classic_dict.nfc}"

"$CC" -std=c11 -Wall -Wextra -O0 -g "$DIR/test_mfc_dict.c" -o "$BIN"
"$BIN" "$DICT"
