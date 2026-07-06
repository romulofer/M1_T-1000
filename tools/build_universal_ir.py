#!/usr/bin/env python3
"""Build aggregated Universal Remotes .ir files from the per-brand database.

For each configured category (TV, Audio, Projector, ...) this walks
``ir_database/<Cat>/*.ir``, collects the records whose Flipper ``name:`` maps
to one of the category's canonical functions (Power, Vol_Up, ...), normalizes
the emitted name to that canonical function, de-duplicates identical
protocol/address/command (or raw-timing) codes across brands, and writes one
aggregated Flipper .ir file to ``ir_database/Universal/<cat>.ir``.

The panel's single-function blast (``ir_ir_blast`` + ``uremote_name_matches``)
then filters this file by canonical name: press "Power" -> transmit every
brand's Power code in turn until the appliance reacts.

Adding a category is data only: add a CATEGORIES entry (label + source dir +
function/alias list) and re-run. Runtime scene code does not change.

Design notes:
  * Per-brand files (Samsung.ir, LG.ir, ...) already use canonical function
    names, so alias matching is exact + case-insensitive (no substring hits).
  * "Power dump" files (Universal_Power.ir, Universal_Projector.ir) are ENTIRELY
    power codes named per brand; every record in them is force-mapped to Power.
  * Output is validated by re-parsing through the real firmware parser
    (tools/host_test/validate.sh sweeps ir_database/**/*.ir via flipper_ir.c).

Usage:
    python3 tools/build_universal_ir.py [--root <repo>] [--verbose]
"""

import argparse
import os
import re
import sys


# ---------------------------------------------------------------------------
# Data model
# ---------------------------------------------------------------------------
class Record:
    """One Flipper .ir signal block."""

    __slots__ = ("name", "type", "protocol", "address", "command",
                 "frequency", "duty_cycle", "data", "source")

    def __init__(self, name, rtype, protocol=None, address=None, command=None,
                 frequency=None, duty_cycle=None, data=None, source=None):
        self.name = name
        self.type = rtype
        self.protocol = protocol
        self.address = address
        self.command = command
        self.frequency = frequency
        self.duty_cycle = duty_cycle
        self.data = data
        self.source = source  # originating brand file stem (for # comments)

    def dedup_key(self):
        """Identity for de-duplication (ignores name/source)."""
        if self.type == "raw":
            return ("raw", self.data)
        return ("parsed", self.protocol, self.address, self.command)


class Function:
    """A canonical panel function and the source names that map to it."""

    __slots__ = ("name", "aliases")

    def __init__(self, name, aliases):
        self.name = name
        # match against canonical name too, all case-insensitive
        self.aliases = [name] + [a for a in aliases if a != name]


# ---------------------------------------------------------------------------
# Pure core (unit-tested by tools/test_build_universal_ir.py)
# ---------------------------------------------------------------------------
def parse_ir_records(text):
    """Parse Flipper .ir text into a list of Record. Header lines and blank
    lines are ignored; ``#`` starts a new signal block."""
    records = []
    cur = None

    def flush():
        nonlocal cur
        if cur is not None and cur.get("name") and cur.get("type"):
            records.append(Record(
                cur["name"], cur["type"],
                protocol=cur.get("protocol"),
                address=cur.get("address"),
                command=cur.get("command"),
                frequency=cur.get("frequency"),
                duty_cycle=cur.get("duty_cycle"),
                data=cur.get("data"),
            ))
        cur = None

    for raw_line in text.splitlines():
        line = raw_line.strip()
        if line.startswith("#"):
            flush()
            cur = {}
            continue
        if ":" not in line:
            continue
        key, _, val = line.partition(":")
        key = key.strip().lower()
        val = val.strip()
        if key == "name":
            # a bare "name:" without a preceding "#" still starts a record
            if cur is None:
                cur = {}
            cur["name"] = val
        elif key in ("type", "protocol", "address", "command",
                     "frequency", "duty_cycle", "data"):
            if cur is None:
                cur = {}
            cur[key] = val
        # Filetype/Version and anything else: ignored
    flush()
    return records


def canonicalize(name, functions):
    """Return the canonical function name for a source record ``name`` (exact,
    case-insensitive match against each function's aliases), or None."""
    low = name.strip().lower()
    for fn in functions:
        for alias in fn.aliases:
            if low == alias.lower():
                return fn.name
    return None


def dedup(records):
    """Drop later records whose dedup_key was already seen. Stable order."""
    seen = set()
    out = []
    for r in records:
        k = r.dedup_key()
        if k in seen:
            continue
        seen.add(k)
        out.append(r)
    return out


_HEX_BYTES_RE = re.compile(r"^[0-9A-Fa-f]{2}( [0-9A-Fa-f]{2})*$")


def lint_record(r):
    """Return a list of human-readable problems with a Record (empty if clean).

    Guards the aggregate against malformed or ambiguous rows: a parsed row must
    carry a protocol plus space-separated hex-byte address/command and no raw
    ``data``; a raw row must carry ``data``. This only *flags* bad input -- it
    never rewrites values, so the generator's canonical single-byte output shape
    is unchanged."""
    problems = []
    if r.type == "parsed":
        if not r.protocol:
            problems.append("parsed record missing protocol")
        for field in ("address", "command"):
            val = getattr(r, field)
            if not val:
                problems.append(f"parsed record missing {field}")
            elif not _HEX_BYTES_RE.match(val.strip()):
                problems.append(f"parsed {field} is not space-separated hex bytes: {val!r}")
        if r.data is not None:
            problems.append("parsed record also carries raw 'data' (ambiguous type)")
    elif r.type == "raw":
        if not r.data:
            problems.append("raw record missing data")
    else:
        problems.append(f"unknown record type: {r.type!r}")
    return problems


def emit_ir(records):
    """Render records as Flipper .ir text (with a per-record # brand comment)."""
    lines = ["Filetype: IR signals file", "Version: 1"]
    for r in records:
        if r.source:
            lines.append("#")
            lines.append(f"# {r.source}")
        else:
            lines.append("#")
        lines.append(f"name: {r.name}")
        lines.append(f"type: {r.type}")
        if r.type == "raw":
            if r.frequency is not None:
                lines.append(f"frequency: {r.frequency}")
            if r.duty_cycle is not None:
                lines.append(f"duty_cycle: {r.duty_cycle}")
            lines.append(f"data: {r.data}")
        else:
            lines.append(f"protocol: {r.protocol}")
            lines.append(f"address: {r.address}")
            lines.append(f"command: {r.command}")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# Category configuration (v1: TV, Audio, Projector)
# ---------------------------------------------------------------------------
CATEGORIES = [
    {
        "output": "tv.ir",
        "src_dir": "TV",
        "functions": [
            Function("Power", ["Power"]),
            Function("Vol_Up", ["Vol_Up"]),
            Function("Vol_Down", ["Vol_Down"]),
            Function("Ch_Up", ["Ch_Up"]),
            Function("Ch_Down", ["Ch_Down"]),
            Function("Mute", ["Mute"]),
            Function("Source", ["Source", "Input"]),
            Function("Menu", ["Menu"]),
        ],
        # Files that are entirely power codes named per-brand -> all -> Power.
        "power_dump_files": ["Universal_Power.ir"],
    },
    {
        "output": "audio.ir",
        "src_dir": "Audio",
        "functions": [
            Function("Power", ["Power"]),
            Function("Vol_Up", ["Vol_Up"]),
            Function("Vol_Down", ["Vol_Down"]),
            Function("Mute", ["Mute"]),
            Function("Source", ["Source", "Input"]),
        ],
        "power_dump_files": ["Universal_Power.ir"],
    },
    {
        "output": "projector.ir",
        "src_dir": "Projector",
        "functions": [
            Function("Power", ["Power"]),
        ],
        # The only projector file is a per-brand power dump.
        "power_dump_files": ["Universal_Projector.ir"],
    },
]


def build_category(cat, ir_root):
    """Return aggregated .ir text for one category, grouped by function and
    de-duplicated. Records are ordered by the category's function list so the
    output reads Power-block, Vol_Up-block, ... ."""
    src_dir = os.path.join(ir_root, cat["src_dir"])
    dump_files = set(cat.get("power_dump_files", []))

    # Bucket records per canonical function name.
    buckets = {fn.name: [] for fn in cat["functions"]}

    if not os.path.isdir(src_dir):
        return None

    for fname in sorted(os.listdir(src_dir)):
        if not fname.endswith(".ir"):
            continue
        path = os.path.join(src_dir, fname)
        stem = fname[:-3]
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            recs = parse_ir_records(fh.read())

        # Flag malformed/ambiguous rows in the source (non-fatal; keeps valid
        # output byte-identical while surfacing bad input for a human to fix).
        for r in recs:
            for problem in lint_record(r):
                sys.stderr.write(f"lint: {fname}: {r.name!r}: {problem}\n")

        if fname in dump_files:
            # Every record is a brand power code.
            for r in recs:
                r.name = "Power"
                r.source = r.source or stem
                if "Power" in buckets:
                    buckets["Power"].append(r)
            continue

        for r in recs:
            canon = canonicalize(r.name, cat["functions"])
            if canon is None:
                continue
            r.name = canon
            r.source = stem
            buckets[canon].append(r)

    ordered = []
    total = 0
    for fn in cat["functions"]:
        deduped = dedup(buckets[fn.name])
        total += len(deduped)
        ordered.extend(deduped)

    if total == 0:
        return None
    return emit_ir(ordered)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=None,
                    help="repo root (default: parent of this script's tools/)")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    root = args.root or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    ir_root = os.path.join(root, "ir_database")
    out_dir = os.path.join(ir_root, "Universal")
    os.makedirs(out_dir, exist_ok=True)

    written = 0
    for cat in CATEGORIES:
        text = build_category(cat, ir_root)
        if text is None:
            if args.verbose:
                print(f"  skip {cat['output']} (no source data)")
            continue
        out_path = os.path.join(out_dir, cat["output"])
        with open(out_path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(text)
        written += 1
        if args.verbose:
            n = text.count("\nname:") + (1 if text.startswith("name:") else 0)
            print(f"  wrote {os.path.relpath(out_path, root)}  ({n} codes)")

    if args.verbose:
        print(f"build_universal_ir: {written} file(s) written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
