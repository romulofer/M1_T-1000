# Custom IR Remotes

The M1 can build, learn, replay, and edit user-defined IR remotes on the device.
Custom remotes are stored as standard Flipper Zero `.ir` files at `0:/IR/*.ir`
(the SD-card root of the IR folder, alongside the shipped category directories),
so they interoperate with the Flipper IRDB format.

Everything below is reachable from **Infrared → Custom Remotes**. The original
three Infrared entries (Universal Remotes, Learn, Replay) are unchanged.

## Create a remote

1. **Infrared → Custom Remotes** opens the *My Remotes* manager: `[+ New Remote]`
   followed by every `0:/IR/*.ir` file.
2. Select `[+ New Remote]`, type a name on the on-screen keyboard, confirm.
   - The name is sanitized to a FAT-legal filename (illegal characters → `_`,
     trailing spaces/dots trimmed, empty → `Remote`).
   - Collisions are de-duplicated as `Name_1 … Name_99`.
   - A valid, empty (header-only) `.ir` file is written. Escaping the keyboard
     writes nothing.

## Open a remote

Selecting a remote opens its action menu:

- **Play Buttons** — the shared scrolling button-list replay screen (the same
  engine the Universal Remotes browser uses). UP/DOWN navigate, OK transmits the
  selected button. Parsed and raw buttons both transmit.
- **Learn Button** — capture a new button (below).
- **Edit Buttons** — rename/delete buttons (below).

## Learn a button

From **Learn Button**, point a remote at the M1 and press a key.

- **Parsed capture (preferred):** if IRMP decodes the signal, the protocol,
  address, and command are shown. OK prompts for a name (auto-suggested
  `<Protocol>_<Command>`, editable) and appends the parsed button to the file.
- **Raw fallback (on UNKNOWN):** if IRMP cannot decode the frame, the raw
  mark/space edge stream is captured instead and shown as a sample count. OK
  prompts for a name and appends it as a `raw` signal (38 kHz / 0.33 duty by
  default). Raw buttons replay through the raw transmitter.

  A raw capture is only finalized on the inter-frame gap and only if it has at
  least a handful of edges — shorter bursts are treated as noise and ignored, so
  stray IR does not create junk buttons.

BACK/LEFT at any point leaves the file untouched.

## Edit buttons

**Edit Buttons** lists the remote's buttons. Select one to open its action menu:

- **Rename** — the current name is pre-filled on the keyboard; confirm to rewrite
  the file with the new name. ESC cancels with no file change.
- **Delete** — asks for confirmation first; confirming rewrites the file without
  that button. Cancel/BACK leaves the file untouched. Deleting the last button
  leaves a valid, empty (header-only) remote you can keep learning into.

All edits use an atomic read-all → temp-file → rename rewrite, so an interrupted
edit never corrupts the original file. Every other button's name, type, data, and
order are preserved exactly.

## On-screen presentation

The manager, menus, and editor share one **rounded-card list** renderer
(`m1_card_list`): the selected row is a filled rounded card, rows may carry a
left 8×8 icon, and a proportional scroll thumb appears on the right when the list
overflows. Action rows are iconed where a glyph is meaningful — **Play** (play),
**Edit**/**Rename** (pencil), **Delete** (trash); rows with no natural icon
(remote names, button names, Learn) are left blank but stay aligned. The Learn
screen shows a target icon while waiting and a check once a signal is captured;
the Delete prompt carries a warning icon.

Transmit and status screens share one centered **status card**
(`m1_tx_status_box`): a bold title over up to two body lines, auto-truncated to
fit the 128×64 frame. It backs the "Transmitting…" screen (button name +
`Addr/Cmd`), the Unsupported / file-error / not-found toasts, and the power-blast
progress and summary screens (the running count and BACK-to-stop affordance stay
in the bottom bar).

> The icon **button panel** (a grid playback view mapping named buttons to fixed
> keys) is planned but not yet in the firmware; **Play Buttons** currently uses
> the scrolling card list, which reaches every button regardless of name.

Both renderers are thin wrappers over host-tested pure-geometry helpers
(`m1_card_list_layout`, `m1_tx_status_layout`) — see *Host validation*.

## Limits

- Up to 64 buttons are listed per remote in the editor.
- Raw signals hold up to 512 mark/space samples.
- Names are truncated to 31 characters.
- Custom remotes live at the `0:/IR/` root; the shipped category folders
  (`TV/`, `Audio/`, `Bluray/`, …) are browsed via **Universal Remotes**.

## Host validation

The `.ir` file layer is covered by host round-trip tests and a validator, and
the UI geometry helpers by a pure-geometry test — all run without hardware:

```
sh tools/host_test/run_tests.sh   # please-wait + Custom Remotes UI geometry +
                                  # .ir round-trip (append/rename/delete/raw) + subghz
sh tools/host_test/validate.sh    # unit suite + validate every shipped .ir
```

`test_remotes_ui` asserts the card-list scroll-window / row / scrollbar math and
the tx-status card centering and line stacking all stay within 128×64.

`validate.sh` also parses every file in `ir_database/`, the same check applied to
M1-authored output (valid header + at least one parseable signal).
