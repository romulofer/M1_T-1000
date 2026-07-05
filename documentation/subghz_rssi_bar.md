# Sub-GHz Live RSSI Indicator

A lightweight, always-visible **RSSI level bar** on the Sub-GHz **Record** view so
the operator can tell signal from silence at a glance, and gauge channel activity
*before* committing to a recording.

It is screen-only and additive: nothing about the record → save → replay flow, the
protocol-decode beep, or the `.sgh`/Flipper `.sub` output changes.

## What you see

Both record states drop the antenna icon and draw a full-width bar across the
bottom of the status panel, with a numeric dBm readout on the top row:

```
+----------------------------+
| Sub-GHz    Record    82% SD|   header
|+--------------------------+|
|| Recording...    -88 dBm  ||   line 1 (+ live dBm, right-aligned)
|| Waiting for signal       ||   line 2
|| --|####################--||   RSSI bar + threshold tick
|+--------------------------+|
| < Back            Stop  () |   bottom bar
+----------------------------+
```

- **Bar fill** grows left→right with signal strength.
- **Threshold tick** marks the detection threshold (noise floor + SNR); fill past
  the tick reads as real signal, below it as noise/silence.
- **dBm readout** shows the current RSSI numerically.

### READY (pre-scan)
The radio is placed in RX (listening only — no SD writer, no raw-capture) so the
bar tracks the selected frequency live. `LEFT/RIGHT` cycle the preset band,
`UP` sets a custom frequency, `DOWN` opens config — each re-tunes the pre-scan.
Pressing `OK` starts recording and hands the radio to the record path with no gap;
`BACK` isolates the radio and leaves.

### ACTIVE (recording)
The bar refreshes on a ~10 Hz timer so it moves during silence, not only when
samples flush to SD or a protocol decodes. When a protocol is decoded, its
protocol / key / bits / TE are shown (dBm on the bar readout) and the decode beep
fires as before; the readout then holds the decoded RSSI.

## How it works

- **Pure mapping (`m1_sub_ghz_rssi_bar.inc`).** `subghz_rssi_to_bar(dbm, bar_w)`
  maps dBm to a fill width and `subghz_rssi_threshold_px(bar_w)` gives the tick
  column. Header-free `#include` snippet shared verbatim by the firmware and the
  host unit test — one source of truth, no extra translation unit. Range:
  min `-110 dBm` (empty) · max `-30 dBm` (full) · threshold `-90 dBm`.
- **Sampling.** The record message loop takes a finite `SUBGHZ_RSSI_BAR_REFRESH_MS`
  (100 ms) timeout on its queue receive. On a timed wake with no queue event it
  samples `SI446x_Get_ModemStatus(0x00)->CURR_RSSI` (~100 µs) on the Sub-GHz task
  and redraws. A real queue event still preempts immediately, so the raw-sample
  flush/decode path is never delayed — recording fidelity comes first.
- **Drawing.** `subghz_draw_rssi_bar()` uses u8g2 frame/box/line primitives inside
  the existing `firstpage()…nextpage()` block; radio and display are touched only
  on the Sub-GHz task, never from an ISR. No heap; fixed geometry.

## Testing

- **Host unit test:** `tools/host_test/test_subghz_rssi.c` (wired into
  `run_tests.sh`) covers endpoint saturation, clamping of extreme inputs,
  monotonicity, and the on-screen threshold/midpoint pixel columns.
- **Bench (hardware):** on READY the bar idles at the floor and fills when a
  transmitter keys up on the selected frequency; on ACTIVE it tracks live during
  silence and signal; decode still beeps and shows protocol/key/bits/dBm/TE; a
  capture → save → replay is byte-identical to pre-change output.

## Tunables

| Constant | File | Meaning |
|----------|------|---------|
| `SUBGHZ_RSSI_BAR_MIN_DBM` (`-110`) | `m1_sub_ghz_rssi_bar.inc` | Empty-bar floor |
| `SUBGHZ_RSSI_BAR_MAX_DBM` (`-30`)  | `m1_sub_ghz_rssi_bar.inc` | Full-bar ceiling |
| `SUBGHZ_RSSI_BAR_THRESHOLD_DBM` (`-90`) | `m1_sub_ghz_rssi_bar.inc` | Threshold tick |
| `SUBGHZ_RSSI_BAR_FILL_W` (`114`)   | `m1_sub_ghz_rssi_bar.inc` | On-screen inner width (px) |
| `SUBGHZ_RSSI_BAR_REFRESH_MS` (`100`) | `m1_sub_ghz.c` | Live refresh cadence |
