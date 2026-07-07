# Universal Remotes (Flipper-style, TV v1)

IR → Universal Remote → **Universal TV** runs a Flipper-style brute-force:
pick a function (Power / Vol +/− / Ch +/− / Mute) and the M1 auto-fires every
brand's version of that function from `0:/IR/Universal/tv.ir`, ~200 ms apart,
until the target device reacts. **BACK** stops the sweep.

## Data
- `0:/IR/Universal/tv.ir` — aggregated `IR library file` (815 signals; functions
  Power 324, Vol_up 99, Vol_dn 104, Ch_next 94, Ch_prev 101, Mute 93). Bundled
  from `ir_database/Universal/tv.ir`.

## Code
- `m1_csrc/m1_uremote_bf.{c,h}` — shared Flipper `.ir` block parser
  (`uremote_parse_signal_block`, `uremote_map_flipper_protocol`) + the streaming,
  name-filtered brute-force iterator (`uremote_bf_stream`) + the TV category
  table (`uremote_category_tv`). Host-tested by
  `tools/host_test/test_uremote_bf.c`.
- `m1_csrc/m1_ir_universal.c` — `Universal TV` dashboard item, the function-list
  screen (`uremote_tv_screen`), and the brute-force progress screen
  (`uremote_bf_run` / `uremote_bf_fire_cb` / `uremote_fire_cmd`).

## Behaviour notes / limitations
- **Parsed-only.** The brute-force skips `type: raw` records. `transmit_raw_command`
  locates a raw signal by name and stops at the first match, so it cannot target
  the Nth same-named brand in a sweep; counting raw records would also desync the
  `n/total` progress. `tv.ir` is entirely `parsed`, so nothing is lost for v1. A
  future category with raw entries needs `uremote_bf_stream` + the raw TX path
  extended to seek the Nth matching raw block.
- The progress total is a pre-count pass (`uremote_bf_stream(..., NULL, NULL)`),
  then a second pass fires; both skip raw, so `n` and `total` stay consistent.

## Add another category later
1. Bundle its aggregated file at `ir_database/Universal/<cat>.ir`.
2. Add a `uremote_function_t[]` + `uremote_category_t` in `m1_uremote_bf.c`
   (export it in `m1_uremote_bf.h`).
3. Add a dashboard item + `switch` case that opens a screen pointed at the new
   category. No new parsing/TX code.

## Bench checklist (owner — requires real IR emission)
- [ ] `Universal TV` appears on the Universal Remote dashboard (item 8).
- [ ] Function list shows all six buttons (Power / Vol +/− / Ch +/− / Mute).
- [ ] `Power` sweeps `n/324`; a real TV powers off mid-sweep; BACK stops
      immediately; result screen shows `Stopped` / `Done`.
- [ ] Vol/Ch/Mute each fire their own function only (progress total matches the
      per-function counts above).
- [ ] If nothing emits at all, suspect the orthogonal TIM1 / IR-silence issue
      (Sub-GHz vs IR carrier), not this feature — it reuses the same TX path as
      every other IR feature.
