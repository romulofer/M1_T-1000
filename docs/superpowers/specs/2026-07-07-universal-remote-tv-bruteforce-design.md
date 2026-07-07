# Universal Remote — Flipper-style TV brute-force (v1)

Date: 2026-07-07
Branch: `feat--rework-universal-remotes--next`
Status: approved design, plan pending

## Goal

Make M1 "Universal Remotes" work like the Flipper Zero universal remote: pick a
device category, press a function button (Power, Vol +, …), and the M1 auto-fires
every brand's version of that one function until the target device reacts. v1
ships **TV only**; more categories are added later as data rows, not new code.

Visuals stay deliberately simple (scrolling text lists, no icon button-panel).
Priority is a working brute-force flow, not polish.

## Locked decisions

1. **Add alongside** the existing dashboard — the current dashboard
   (Browse IRDB / Learned / Favorites / Recent / Remote Mode / Power Off TVs /
   Power Off A/V) is kept; the Flipper flow is a new entry on it.
2. **Bundle Flipper assets** — copy Flipper's aggregated `IR library file`
   universal set. v1 uses `tv.ir` only.
3. **TV only** for v1.
4. **Auto-fire + BACK-stop** brute-force behavior (no pause/step in v1).
5. **Simple list visuals** — reuse the existing list/progress renderers, no new
   icons or fonts.

## Data

- Source: `../../flipper_firmwares/Momentum-Firmware/applications/main/infrared/resources/infrared/assets/tv.ir`.
- Bundle into repo at `ir_database/Universal/tv.ir`, which deploys to SD
  `0:/IR/Universal/tv.ir` (repo `ir_database/` → SD `0:/IR`, confirmed by the
  existing `0:/IR/TV/Universal_Power.ir` power-off path).
- Facts about `tv.ir`: 815 signals; header `Filetype: IR library file` (Version 1);
  6 distinct function names — `Power` (324), `Vol_up` (99), `Vol_dn` (104),
  `Ch_next` (94), `Ch_prev` (101), `Mute` (93). Signal blocks are the same
  key/value form the M1 parser already reads.
- 815 records and 324 Power codes both exceed `IR_UNIVERSAL_MAX_CMDS` (64) — the
  brute-force path **must stream** the file, never load it whole.

## UI flow

1. **Dashboard** gains one item: `"Universal TV"` (appended; existing items and
   their order unchanged). `DASHBOARD_ITEM_COUNT` 7 → 8.
2. Selecting it opens a **function list** screen via the existing
   `draw_list_screen`: `Power`, `Vol +`, `Vol -`, `Ch +`, `Ch -`, `Mute`.
   UP/DOWN scroll, OK selects, BACK returns to dashboard.
3. OK on a function opens the **brute-force screen**: auto-fires each brand's code
   for that one function in file order, showing `Power  7/324`-style progress
   with a ~200 ms gap between sends. **BACK stops immediately.** A `Done` /
   `Stopped` result screen follows (mirrors the existing `ir_power_blast` UI),
   then returns to the function list.

## Core mechanism

A new streaming, name-filtered brute-force function — conceptually a filtered,
streaming variant of the existing `ir_power_blast()`:

- Open `0:/IR/Universal/tv.ir`. Validate the header accepting
  **`IR library file`** (the current `parse_ir_file` accepts only
  `IR signals file`; the brute-force path must also accept the library header).
- Loop `parse_ir_signal_block()` block by block — O(1) memory, no 64-cmd cap.
  For each block whose `name` equals the selected function's record name:
  encode and fire it through the existing parsed/raw TX path (same code
  `transmit_command()` / `ir_power_blast()` use), bump progress, poll for a BACK
  press (non-blocking), then delay. Blocks with other names are skipped.
- Records that fail to parse or use an unknown protocol are skipped (not counted
  as a fired code), matching the existing blast's tolerance.

### Table-driven categories

Represent a category as a row: `{ menu_label, ir_file_path, function_list[] }`
where each function is `{ display_label, record_name }`. v1 has one row (TV) with
the six functions above. Adding Audio/Projector/etc. later = add a row (and its
dashboard item or a category submenu) plus bundle its `.ir` file — no new
control-flow code.

## Reuse / untouched

- **Reuse:** `parse_ir_signal_block`, the parsed/raw encode+fire logic inside
  `transmit_command` / `transmit_raw_command`, `draw_list_screen`, and the
  `ir_power_blast` progress/result screen pattern.
- **No** new icons, fonts, or scenes.
- **Untouched:** the 20-byte `S_M1_FW_CONFIG_t` struct, the existing dashboard
  items and behavior, and the on-disk `.ir` format.

## Testing

- **Host tests** (authoritative for logic): drive the streaming name-filter over
  a fixture `.ir` library through the real `flipper_file` / `flipper_ir` parser —
  assert the correct count of matching records is selected per function and
  non-matching names are skipped. Follows the existing `tools/host_test/`
  pattern. Per [[ir-target-is-m1-not-flipper]], the host round-trip through the
  M1's own parser is the correctness gate; loading on a real Flipper is not.
- **Bench (owner only):** flash, open Universal TV, press Power, confirm a real
  TV powers off mid-sweep and BACK stops. Real IR emission cannot be verified
  from this Linux host (Windows toolchain).

## Risks / dependencies

- **IR-TX-silence / TIM1-vs-SubGHz** ([[ir-tx-silence-tim1-conflict]]) may be live
  on this branch. It is orthogonal: the brute-force reuses the same TX path as
  every other IR feature, so if plain IR TX emits, this does too. Not addressed
  by this feature; called out so a silent bench result is attributed correctly.
- Cannot build or flash from this Linux host — logic is host-tested here; emission
  is an owner bench step.

## Out of scope (v1)

- Categories other than TV (Audio, Projector, AC, LEDs, Fans, Blu-ray, Monitor,
  Digital Sign) — later rows.
- Pause / step / re-fire-last controls.
- Icon button-panel layout.
- Fixing the TIM1/IR-silence bug.
