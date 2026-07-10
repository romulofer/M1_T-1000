# Universal AC Remote — Brute-Force Design

**Date:** 2026-07-10
**Branch:** `feat--rework-universal-remotes--next`
**Status:** Approved design, pre-implementation
**Related:** `2026-07-07-universal-remote-tv-bruteforce-design.md` (TV feature this extends)

## Goal

Add an "Universal AC" item to the IR → Universal Remote dashboard that works like
the existing Universal TV brute-force: pick a function (Off / Cool Hi / Cool Lo /
Heat Hi / Heat Lo / Dehumidify) and the device fires every brand's version of that
function from a bundled `0:/IR/Universal/ac.ir`, showing progress, BACK to stop.

## The core problem

Unlike `tv.ir` (parsed address/command codes), `ac.ir` is **94% raw**: 305 raw /
17 parsed blocks. AC protocols carry a full state payload (temp+mode+fan+power)
that does not fit the parsed `address`/`command` fields, so brands are stored as
raw timing frames. The current sweep engine is **parsed-only** — it skips raw
records (`if (cmd.is_raw) continue;`). Bundling `ac.ir` unchanged would fire only
~17 codes across all functions — useless. **The sweep must be extended to fire raw
codes.**

Function coverage in `ac.ir`: Off=139, Dh=43, Cool_lo=38, Cool_hi=37, Heat_hi=33,
Heat_lo=32. Longest raw frame = 595 samples, average 253.

## Approach (chosen: A — capture-during-stream)

The streaming parser already walks each raw record's `data:` line to count samples;
extend it to also **store** those samples into a shared buffer as it visits records
in order. The fire path then transmits that buffer directly. No second file read,
no "seek the Nth same-named record" problem (streaming already yields records
sequentially), one engine, core stays host-testable.

Rejected: (B) a parallel raw sweep using `flipper_ir_read_signal` — duplicates the
progress/abort/count loop and diverges from the host-tested `uremote_bf_stream`.
(C) parsed-only stopgap — fires ~17 codes, not functional.

## Components

### 1. Raw buffer size — `m1_csrc/flipper_ir.h`
`FLIPPER_IR_RAW_MAX_SAMPLES` 512 → 640. Covers `ac.ir`'s 595-sample max (512 would
truncate 83 frames into broken TX). Cost from the +128 samples: `s_raw_tx_signal.raw
.samples` and `s_uremote_raw_samples` (int32) grow ~512 B each, `s_raw_ota_buffer`
(uint16) ~256 B — plus `s_uremote_raw_samples` is a new ~2.5 KB buffer. Low single-
digit KB of static RAM on a 640 KB device. Trivial.

### 2. Streaming parser captures raw samples — `m1_csrc/m1_uremote_bf.{c,h}`
- `uremote_parse_signal_block` takes an optional out-buffer (`int32_t *raw_out,
  uint16_t raw_cap`; pass `NULL`/`0` to keep count-only behaviour). For a raw
  record it parses each whitespace-separated integer of `data:` into `raw_out`
  (clamped to `raw_cap`) and sets `cmd->raw_count` to the stored count. `int32_t`
  matches `flipper_ir_signal_t.raw.samples` — `ac.ir` values reach 132136 µs
  (inter-frame gaps), well beyond 16 bits.
- `uremote_bf_stream` no longer skips raw (`if (cmd.is_raw) continue;` removed);
  it threads the caller's buffer/cap through to the parser. Module stays HAL-free
  and host-testable (it only writes ints to a caller array).
- Signature change is additive; the TV path passes the same shared buffer and is
  unaffected (its records are parsed, so nothing is captured).

### 3. Raw fire path — `m1_csrc/m1_ir_universal.c`
- Extract the OTA-build + TIM16 kick out of `transmit_raw_command` into
  `static void fire_raw_samples(const int32_t *samples, uint16_t count,
  uint32_t freq)`: convert alternating mark/space to OTA (`| IR_OTA_PULSE_BIT_MASK`
  for even/mark, `& IR_OTA_SPACE_BIT_MASK` for odd/space), per-sample duration
  clamped to 65534 and count clamped to `IR_RAW_OTA_BUFFER_MAX` (existing raw-TX
  behaviour), set carrier, kick TX. No file re-read, no per-code screen.
  `transmit_raw_command` is refactored to call this helper after it loads
  `s_raw_tx_signal` (behaviour preserved).
- `uremote_fire_cmd`: `cmd->is_raw` → `fire_raw_samples(s_uremote_raw_samples,
  cmd->raw_count, cmd->raw_freq)`; parsed → existing IRSND path.
- Shared `static int32_t s_uremote_raw_samples[FLIPPER_IR_RAW_MAX_SAMPLES]` owned
  here, passed into `uremote_bf_stream`.

### 4. Burst / pacing rule — `m1_csrc/m1_ir_universal.c`
- **Parsed** codes: keep the 4× burst (`UREMOTE_BF_FRAME_REPEATS`) added for the
  TV/Samsung latch fix.
- **Raw** codes: fire **once** (AC latches full state; repeats unnecessary and
  595-sample frames are long). After kicking a raw frame, wait until
  `ir_ota_data_tx_active` clears (timeout ~250 ms) so the frame finishes before
  the next code, then the existing settle gap.

### 5. Category + dashboard — `m1_csrc/m1_ir_universal.c`
- `s_ac_functions[]`: `{ "Off","Off" }, { "Cool Hi","Cool_hi" }, { "Cool Lo",
  "Cool_lo" }, { "Heat Hi","Heat_hi" }, { "Heat Lo","Heat_lo" },
  { "Dehumidify","Dh" }`.
- `uremote_category_ac`: label "Universal AC", path
  `IR_UNIVERSAL_IRDB_ROOT "/Universal/ac.ir"`, the 6 functions.
- New dashboard item #9 "Universal AC" (mirrors how Universal TV was added as #8);
  bump `DASHBOARD_ITEM_COUNT`.

### 6. Data — `ir_database/Universal/ac.ir`
Copy verbatim from the Flipper assets
(`.../Momentum-Firmware/.../infrared/assets/ac.ir`), same provenance as `tv.ir`.
Deploys to SD `0:/IR/Universal/ac.ir`.

## Data flow

```
Dashboard "Universal AC" (item 9)
  -> function list (Off / Cool Hi / ... / Dehumidify)
    -> uremote_bf_run("0:/IR/Universal/ac.ir", fn)
       -> draw "Starting..." (instant feedback, pre-SD)
       -> uremote_bf_stream(path, record_name, fire_cb, ctx)   [single pass]
            for each block in order:
              parse (capturing raw samples into s_uremote_raw_samples)
              name != record_name  -> skip
              parsed -> uremote_fire_cmd -> 4x burst
              raw    -> uremote_fire_cmd -> fire_raw_samples() once
                     -> wait ir_ota_data_tx_active (<=250 ms)
              draw "Sending N"; poll BACK to abort
       -> "Done / N codes sent"
```

## Error handling
- Frame > 640 samples: clamp to `IR_RAW_OTA_BUFFER_MAX` (existing behaviour).
- `raw_freq == 0` or `raw_count == 0`: record is invalid — skip, do not count.
- `ac.ir` missing on SD: existing "No codes" screen (triggered by `sent == 0`).
- Carrier ownership: `uremote_fire_cmd` already calls `infrared_encode_sys_init()`
  per code; the raw branch sets frequency via `irsnd_set_carrier_freq`.

## Testing
- **Host** (`tools/host_test/test_uremote_bf.c`): extend the fixture with raw `Off`
  records and assert:
  1. raw records are now **counted and fired** (previously skipped);
  2. captured samples equal the `data:` integers (order + values);
  3. parsed records still count/fire unchanged;
  4. a frame longer than `raw_cap` clamps to `raw_cap`.
- **Bench** (owner, Windows toolchain): flash; Universal AC → Off sweep pointed at
  a real AC; confirm power-off, then Cool Hi / Heat Hi latch. Linux cannot build
  the STM32 firmware.

## Side effect (intended)
- Removing the `if (cmd.is_raw) continue;` skip is global, so **every** category's
  sweep now fires raw codes — including the TV Power sweep's ~182 previously
  skipped raw codes. This is a coverage win (flagged earlier as the biggest TV
  gap). Trade-off: sweeps that contain raw codes take longer (raw fires once + a
  ~120 ms settle vs. the parsed 4x burst). Acceptable — BACK stops the sweep.

## Out of scope
- AC state modelling (temp stepping, fan speed) — universal AC is fixed-command
  brute-force only, matching Flipper.

## Constraints / notes
- `S_M1_FW_CONFIG_t` untouched. No heap. FreeRTOS include order preserved.
- No new source files → no `CMakeLists.txt` source changes (edits only).
- Nothing pushed; LOCAL ONLY per project rules.
