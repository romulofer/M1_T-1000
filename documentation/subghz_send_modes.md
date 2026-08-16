# Sub-GHz Send Modes — Once or Repeat

The Sub-GHz replay screen lets the operator choose whether a saved signal is sent
**once** or **repeated** until they stop it. Before this, replay always looped:
opening a signal keyed the transmitter over and over until `BACK`, which is wrong
for a gate/garage remote where one press is one command.

## What you see

```
+----------------------------+
| Sub-GHz    Replay    82% SD|   header
|+--------------------------+|
|| 433.920MHz OOK           ||   line 1: frequency + modulation
|| Mode: Once               ||   line 2: active send mode
|| OK send  L/R mode        ||   line 3: key hints
|+--------------------------+|
| < Back            Send  () |   bottom bar
+----------------------------+
```

- `LEFT` / `RIGHT` — toggle **Once** ⇄ **Repeat**
- `OK` — send now
- `BACK` — stop and leave the screen

While transmitting, the screen switches to a sending state (`Sending...` or
`Sending (repeat)`, bottom bar `Stop`) and the RGB LED blinks; it returns to the
mode line when the signal is done.

## Behaviour

| Mode | On end of signal |
|------|------------------|
| **Once** (default) | Transmitter stops, radio is isolated, LED off. `OK` sends again. |
| **Repeat** | Signal is re-armed and sent again immediately, until `BACK`. |

- One "send" is one **burst**: the file is transmitted
  `1 + SUBGHZ_TX_RAW_REPLAY_REPEAT_DEFAULT` (= 5) times back to back, which is
  what a real remote does for a single button press. Repeat mode loops that burst.
- Opening a signal transmits immediately — `sub_ghz_replay_start()` keys the
  radio as soon as the samples are loaded — so the first burst always goes out;
  the mode decides what happens *after* it.
- The mode can be toggled **during** a transmission. It is read at the end of the
  current burst, so switching to Once while looping stops it cleanly at the next
  signal boundary rather than cutting a frame in half.
- The choice is session-sticky (module-scoped `subghz_tx_repeat_mode`, defaults to
  Once on boot); it is not written to the SD card.

## Where it applies

All send paths share the state and the screen:

- Flipper `.sub` files (`sub_ghz_replay_flipper_file()`) — **Saved → Emulate** and
  the replay file browser
- Native `.sgh` captures — **Saved → Emulate**
- The browse → play view path (`VIEW_MODE_SUBGHZ_REPLAY_PLAY`)
- **Add Manually**, which builds a temporary `.sub` and reuses the same engine
- M1 Link **remote trigger**, which replays a received `.sub` through the same
  engine on the peer, using that peer's current mode

The Record view's own "replay armed" screen is unaffected: it already sent a
single burst per `OK`, and it uses `LEFT` for reset.

## How it works

- `subghz_tx_repeat_mode` (in `m1_csrc/m1_sub_ghz.c`) holds the choice; the play
  screen renders it and each `LEFT`/`RIGHT` press flips it and redraws.
- `sub_ghz_replay_tx_burst_start()` is the single place that arms a burst
  (`sub_ghz_set_opmode(TX)` → `sub_ghz_raw_replay_init()` → `ntx_raw_repeat`),
  replacing four copies of that sequence.
- When `sub_ghz_replay_continue()` reports `SUB_GHZ_RAW_DATA_PARSER_IDLE`, the
  event loops either re-arm (Repeat) or drop the LED and redraw the mode line
  (Once). A failed re-arm falls back to the idle state instead of looping.
- `SUBGHZ_REPLAY_DISPLAY_PARAM_SENDING` is the transmitting screen state; the
  `OK` handler ignores presses unless the engine is idle, so a send can't be
  stacked on top of itself.

## Testing

- **Build:** firmware builds clean with CRC injection; host suite unaffected
  (`tools/host_test/run_tests.sh`).
- **Bench (hardware):** with a receiver on the captured frequency — Once keys the
  radio for a single burst and stops; Repeat keys continuously until `BACK`;
  toggling to Once mid-loop stops at the next signal end; `BACK` during a send
  stops TX and releases the radio.
