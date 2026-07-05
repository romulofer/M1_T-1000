# Centered "Please wait…" Box

A single reusable modal replaces the hourglass glyph that was previously drawn
inline — at inconsistent positions — on every blocking screen. One helper,
`m1_please_wait_box()`, is now called at every indeterminate-wait site so they
all render the same centered, rounded, drop-shadowed box with the
`hourglass_18x32` icon and the caption **`Please wait...`**.

## Helper

```c
/* Overlay: draws on top of the CURRENT frame buffer. Does NOT clear and does
 * NOT flush — the caller owns m1_u8g2_firstpage() and m1_u8g2_nextpage().
 * Restores the draw color to M1_DISP_DRAW_COLOR_TXT before returning. */
void m1_please_wait_box(u8g2_t *u8g2);
```

- **Renderer:** `m1_csrc/m1_display.c` — measures the caption in
  `M1_PLEASE_WAIT_FONT` (courB08), then draws shadow → interior (BG mask) →
  border → hourglass → caption.
- **Geometry:** pure, host-testable `m1_please_wait_layout()` in
  `m1_csrc/m1_please_wait_layout.{c,h}`. Box `w = max(icon, text) + 2*PAD`,
  `h = PAD + ICON_H + GAP + ascent + PAD`, centered in 128×64; box + 2px shadow
  provably stay on-screen (`tools/host_test/test_please_wait_box`).

## Converted sites

| File | Screen |
|---|---|
| `m1_fw_update.c` | FW-update file select → "ready to flash" prep |
| `m1_esp32_fw_update.c` | ESP32 flash **prep** screen (pre-flash) |
| `m1_storage.c` | SD **Mounting… / Unmounting… / Formatting…** |
| `m1_bt.c` | BLE **Initializing / Scanning / Advertising / scan-stop** |

Context captions (e.g. "Mounting…") are left in the buffer behind the opaque
box per the overlay design; the box masks its own footprint and peripheral
headers peek around it.

## Deliberately **not** converted

- **`m1_bt.c` status panels** (`m1_draw_status_panel`, "Bluetooth / Init" and
  "Bluetooth / Scan"): richer labeled panels; converting would lose their
  descriptive lines.
- **`m1_esp32_fw_update.c` flash-progress screen** (`esp32_draw_progress_overlay`):
  shows a live progress bar and percentage. The centered box would mask that
  determinate feedback, so its hourglass is kept.

## Bench checklist (manual, on 128×64 LCD)

Trigger each path and confirm the **same** centered box — rounded corners, a 2px
down-right shadow, the hourglass, and `Please wait...` — fully on-screen:

- [ ] **FW update:** Settings → firmware update, select a `.bin` → prep screen.
- [ ] **ESP32 flash prep:** trigger an ESP32 firmware flash → the *preparing*
      screen (before the progress bar) shows the box; the **progress bar screen
      still shows its live %** (unchanged).
- [ ] **SD mount:** Storage → Mount → confirm → "Mounting…" shows the box.
- [ ] **SD unmount:** Storage → Unmount → confirm → box.
- [ ] **SD format:** Storage → Format → confirm → box (then Successful/Failed).
- [ ] **BLE init/scan:** Bluetooth → scan → the direct init/scan/advertising and
      scan-stop waits show the box; the labeled **Init/Scan panels are unchanged**.

## Host test

`sh tools/host_test/run_tests.sh` runs the geometry test (centering, on-screen
bounds including shadow, baseline-below-icon, containment) alongside the Flipper
`.ir` suite.
