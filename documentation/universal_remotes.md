# Universal Remotes (Flipper-style: TV + AC)

IR → Universal Remote runs Flipper-style brute-force sweeps: pick a function and
the M1 auto-fires every brand's version of that function from a bundled library,
until the target device reacts. **BACK** stops the sweep.

- **Universal TV** (dashboard item 8) — `0:/IR/Universal/tv.ir`, functions
  Power / Vol +/− / Ch +/− / Mute.
- **Universal AC** (dashboard item 9) — `0:/IR/Universal/ac.ir`, functions
  Off / Cool Hi / Cool Lo / Heat Hi / Heat Lo / Dehumidify.

## Data
- `0:/IR/Universal/tv.ir` — aggregated `IR library file`. Bundled from
  `ir_database/Universal/tv.ir`. Parsed Power codes were extended with brand
  variants harvested from `TV/Universal_Power.ir` + per-brand `TV/*.ir`.
- `0:/IR/Universal/ac.ir` — aggregated AC library (322 signals: Off 139, Dh 43,
  Cool_lo 38, Cool_hi 37, Heat_hi 33, Heat_lo 32). **94% raw** — AC protocols
  carry a full state payload (temp+mode+fan+power) that does not fit parsed
  address/command, so brands are stored as raw timing frames. Bundled from
  `ir_database/Universal/ac.ir`.
- Deploy: copy the `ir_database/` tree to the SD card at `0:/IR/` (see
  `ir_database/README.txt`). No build step stages it.

## Code
- `m1_csrc/m1_uremote_bf.{c,h}` — shared Flipper `.ir` block parser
  (`uremote_parse_signal_block`, `uremote_map_flipper_protocol`) + the streaming,
  name-filtered brute-force iterator (`uremote_bf_stream`) + the category tables
  (`uremote_category_tv`, `uremote_category_ac`). Host-tested by
  `tools/host_test/test_uremote_bf.c`. HAL-free.
- `m1_csrc/m1_ir_universal.c` — the `Universal TV`/`Universal AC` dashboard items,
  the shared function-list screen (`uremote_category_screen`, with
  `uremote_tv_screen` / `uremote_ac_screen` wrappers), and the sweep
  (`uremote_bf_run` / `uremote_bf_fire_cb` / `uremote_fire_cmd`). Raw frames are
  transmitted by `fire_raw_samples()` (shared with `transmit_raw_command`).

## How the sweep fires
- **Single streaming pass.** `uremote_bf_run` draws an instant `<label> /
  Starting...` screen, then streams the library once, firing each matching record
  as it is parsed. Progress shows a running `Sending N` (no pre-count pass — that
  used to read the whole file twice and lag the screen/first TX). `No codes` shows
  only if nothing matched.
- **Parsed codes** fire as a short burst: `UREMOTE_BF_FRAME_REPEATS` (4) frames,
  `UREMOTE_BF_FRAME_GAP_MS` (75 ms) apart, so debouncing TVs (e.g. Samsung) latch
  POWER instead of only blinking on reception. Counted once.
- **Raw codes** (AC) are captured sample-by-sample during streaming into
  `s_uremote_raw_samples` (`int32_t`, cap `FLIPPER_IR_RAW_MAX_SAMPLES` = 640;
  `ac.ir`'s longest frame is 595) and fired **once**, then the sweep waits for the
  TIM16 ISR to finish the frame (`ir_ota_data_tx_active`, ~250 ms cap) before the
  next code. AC frames are long and carry full state, so repeats are unnecessary.

## Add another category later
1. Bundle its aggregated file at `ir_database/Universal/<cat>.ir`.
2. Add a `uremote_function_t[]` + `uremote_category_t` in `m1_uremote_bf.c`
   (export it in `m1_uremote_bf.h`).
3. Add a dashboard item + `switch` case that calls
   `uremote_category_screen(&uremote_category_<cat>)` (a wrapper). No new
   parsing/TX code — parsed and raw both work.

## Bench checklist (owner — requires real IR emission; Windows toolchain build)
- [ ] `Universal TV` (item 8) and `Universal AC` (item 9) appear on the dashboard.
- [ ] TV `Power` sweep: a real TV powers off mid-sweep; BACK stops immediately;
      result screen shows `Stopped` / `Done` with a code count. Old Samsung should
      now latch (burst fix), not just blink.
- [ ] AC `Off` sweep pointed at a real AC: the unit powers off during the sweep
      (fires ~139 Off codes). Then `Cool Hi` / `Heat Hi` change mode/temp.
- [ ] Each function fires only its own records (running `Sending N` advances).
- [ ] If nothing emits at all (no LED flicker on the target), suspect the
      orthogonal TIM1 / IR-carrier issue (Sub-GHz vs IR), not this feature — it
      reuses the same TX path as every other IR feature.
