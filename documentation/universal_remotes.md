# Universal Remotes

A Momentum-style, **table-driven** universal remote for the M1. Pick a device
category (TV, Audio, Projector, …), see a grid of remote-key icons (Power, Vol
±, Ch ±, Mute, Source, Menu), press one, and the M1 **brute-forces that single
function across every brand** in a bundled `.ir` file until the appliance
reacts. BACK aborts.

Reachable from **Infrared → Universal Remotes**. All other Infrared entries
(Browse IRDB, Learned, Favorites, Recent, Remote Mode, Power Off) are unchanged.

## Flow

1. **Infrared → Universal Remotes** lists the available categories. A category
   is shown only if its aggregated file exists on the SD card
   (`0:/IR/Universal/<cat>.ir`); missing ones are hidden.
2. Selecting a category opens the **icon panel**: a 2-column scrolling grid of
   that category's functions. The four arrow keys move the selection; BACK
   returns to the category list.
3. Pressing **OK** on a function blasts *only that function's* codes, one per
   brand, showing the same progress / stop / summary UI as Power-Off. BACK (or
   LEFT) stops early. Control returns to the panel afterward.
4. Functions with **no matching code** in the file are drawn struck-through
   (muted) and do nothing on OK.

## Architecture — categories are data

The whole feature is a table plus a data file. Adding a category needs **no
scene code**.

- `uremote_fn_t` — one panel key: caption + 8×8 glyph + the canonical record
  name it matches (and optional aliases, e.g. `Source` ↔ `Input`).
- `uremote_category_t` — a category: label + aggregated `.ir` file +
  `uremote_fn_t[]`.
- `s_uremote_categories[]` in `m1_csrc/m1_ir_universal.c` — the v1 rows: **TV**
  (8 functions), **Audio** (5), **Projector** (1).

The matcher (`uremote_name_matches`, `m1_csrc/m1_uremote_match.c`) compares a
parsed record's `name:` to the function's record/aliases case-insensitively
(full-string, no substring hits). The blast (`ir_ir_blast`) **streams** the
file record-by-record, so aggregated files larger than the in-RAM command
buffer are fully covered (e.g. `tv.ir` has 89 records; Source and Menu live
past record 64 and would be unreachable if the file were slurped).

The panel renderer (`m1_uremote_panel`, `m1_csrc/m1_display.c`) is a thin
wrapper over the host-tested grid geometry in `m1_csrc/m1_uremote_layout.c`
(2 columns × 3 visible rows, scrolls when a category has more functions than
fit). Glyphs live in `m1_csrc/m1_display_data.c` (externs in
`m1_uremote_icons.h`).

## The aggregated `.ir` files

`ir_database/Universal/<cat>.ir` is **generated**, not hand-edited. The
generator `tools/build_universal_ir.py` walks the per-brand files under
`ir_database/<Cat>/`, renames each record to its canonical function name,
de-duplicates identical protocol/address/command (or raw) codes across brands,
and writes one file per category. Copy `ir_database/Universal/` to the SD card
at `0:/IR/Universal/`.

Regenerate after changing the per-brand database:

```
python3 tools/build_universal_ir.py --verbose
```

Every generated file is re-parsed through the real firmware parser by
`tools/host_test/validate.sh` (the same `flipper_ir.c` the device uses).

## Add a category (data only)

1. Drop per-brand `.ir` files under `ir_database/<Category>/` (function records
   named `Power`, `Vol_Up`, `Vol_Down`, `Ch_Up`, `Ch_Down`, `Mute`, `Source`,
   `Menu`; a per-brand *power dump* file maps every record to `Power`).
2. Add a `CATEGORIES` entry in `tools/build_universal_ir.py` (source dir +
   function/alias list) and re-run it to produce `Universal/<cat>.ir`.
3. Add a `uremote_category_t` row to `s_uremote_categories[]` in
   `m1_ir_universal.c` (label + file + a `uremote_fn_t[]` reusing the existing
   glyphs). Reuse an existing glyph or add one to `m1_display_data.c` +
   `m1_uremote_icons.h` only if a new key type is needed.

No new screen, event loop, or blast code is required — the engine is shared.

## Tests

- `tools/host_test/test_uremote_panel/` — the record-name matcher and the grid
  geometry (in-frame, non-overlap, scroll window, index round-trip).
- `tools/test_build_universal_ir.py` — the generator core (parse / normalize /
  dedup / emit).
- Both run inside `tools/host_test/run_tests.sh`; the generated files are
  round-tripped by `tools/host_test/validate.sh`.

## Bench checklist (owner)

Host tests and the build cannot emit IR or eyeball the 128×64 panel — verify on
hardware:

1. Copy `ir_database/Universal/` to the SD card at `0:/IR/Universal/`.
2. **Infrared → Universal Remotes** lists **TV / Audio / Projector**. Remove a
   file → that category disappears; all three gone → guidance screen.
3. Open **TV**: the icon grid is fully inside the frame; arrows move the
   highlight; it scrolls (TV has 8 functions > one 6-cell page).
4. Point at a TV, press **Power** → codes stream (progress `n/total`); the TV
   reacts; BACK stops early; the summary shows the count; control returns to the
   panel at the same selection.
5. Confirm **Source** and **Menu** (records past #64 in `tv.ir`) actually emit —
   proves the streaming path, not the old 64-cap.
6. A function with no code in the file is struck-through and does nothing on OK.
7. BACK unwinds panel → category list → dashboard with no rotation/queue
   leftovers.
8. **Regression:** Power Off TVs / Power Off A/V behave exactly as before.
