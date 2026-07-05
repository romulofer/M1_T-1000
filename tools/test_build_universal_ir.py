#!/usr/bin/env python3
"""Unit tests for the pure core of build_universal_ir.py.

Exercises record parsing, canonical-name normalization, de-duplication, and
Flipper .ir emission. No filesystem sweep, no hardware. Run:

    python3 tools/test_build_universal_ir.py
"""

import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_universal_ir as g  # noqa: E402

FAIL = 0


def check(cond, msg):
    global FAIL
    if not cond:
        print(f"FAIL: {msg}")
        FAIL += 1


SAMPLE = """Filetype: IR signals file
Version: 1
#
name: Power
type: parsed
protocol: NEC
address: 40 00 00 00
command: 0C 00 00 00
#
name: Vol_Up
type: parsed
protocol: NEC
address: 40 00 00 00
command: 02 00 00 00
#
name: ViewSonic_Power
type: raw
frequency: 38000
duty_cycle: 0.330000
data: 100 200 300 400
"""


def test_parse_records():
    recs = g.parse_ir_records(SAMPLE)
    check(len(recs) == 3, f"parsed 3 records, got {len(recs)}")

    check(recs[0].name == "Power", "first record name")
    check(recs[0].type == "parsed", "first record type")
    check(recs[0].protocol == "NEC", "first record protocol")
    check(recs[0].address == "40 00 00 00", "first record address")
    check(recs[0].command == "0C 00 00 00", "first record command")

    check(recs[2].type == "raw", "raw record type")
    check(recs[2].data == "100 200 300 400", "raw record data")


def test_canonicalize():
    functions = [
        g.Function("Power", ["Power"]),
        g.Function("Source", ["Source", "Input"]),
    ]
    # exact, case-insensitive
    check(g.canonicalize("power", functions) == "Power", "power -> Power (ci)")
    check(g.canonicalize("POWER", functions) == "Power", "POWER -> Power")
    # alias
    check(g.canonicalize("input", functions) == "Source", "input -> Source (alias)")
    # unknown
    check(g.canonicalize("Netflix", functions) is None, "unknown -> None")
    # no substring false positive
    check(g.canonicalize("Power_On", functions) is None, "Power_On not matched")


def test_dedup():
    r1 = g.Record("Power", "parsed", protocol="NEC",
                  address="40 00 00 00", command="0C 00 00 00")
    r2 = g.Record("Power", "parsed", protocol="NEC",   # identical tuple
                  address="40 00 00 00", command="0C 00 00 00")
    r3 = g.Record("Power", "parsed", protocol="NEC",   # different command
                  address="40 00 00 00", command="0D 00 00 00")
    r4 = g.Record("Power", "raw", data="100 200 300")
    r5 = g.Record("Power", "raw", data="100 200 300")  # identical raw

    out = g.dedup([r1, r2, r3, r4, r5])
    check(len(out) == 3, f"dedup keeps 3 unique, got {len(out)}")


def test_emit_reparseable_shape():
    recs = [
        g.Record("Power", "parsed", protocol="NEC",
                 address="40 00 00 00", command="0C 00 00 00", source="Samsung"),
        g.Record("Power", "raw", data="100 200 300", source="ViewSonic"),
    ]
    text = g.emit_ir(recs)
    check(text.startswith("Filetype: IR signals file\nVersion: 1\n"),
          "emit has Flipper header")
    # round-trips through our own parser
    reparsed = g.parse_ir_records(text)
    check(len(reparsed) == 2, "emitted text re-parses to 2 records")
    check(reparsed[0].name == "Power", "emitted record keeps canonical name")
    check("# Samsung" in text or "#Samsung" in text or "Samsung" in text,
          "emit includes brand comment")


def main():
    test_parse_records()
    test_canonicalize()
    test_dedup()
    test_emit_reparseable_shape()
    if FAIL:
        print(f"build_universal_ir: {FAIL} check(s) FAILED")
        return 1
    print("build_universal_ir: all tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
