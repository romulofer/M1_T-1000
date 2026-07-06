# IR Transmit — Parsed-Protocol Frame Audit

Status as of 2026-07-06. Companion to the `ir_expand_parsed_code()` expander in
`m1_csrc/flipper_ir.c` and the on-air oracle harness in
`tools/host_test/test_ir_tx_frames.c`.

> **See also §0 below — the frame audit is about *which bytes* go out. A separate
> regression (post-rework) is about *no carrier at all* going out. They are
> unrelated: the byte-level frames here are host-proven correct even while the
> device emits nothing.** Root cause + fix for the silence live in `SPEC.md`.

## 0. Hardware note — `TIM1` is shared between IR TX and Sub-GHz

The IR transmit **carrier** is `TIM1_CH4` PWM on `PC5` (`AF1_TIM1`, 38 kHz). The
IR **baseband** (OTA bit timing) is `TIM16`. Crucially, **`TIM1` is also the
Sub-GHz timer**:

| Consumer | `TIM1` mode | Pin | IRQ / DMA |
|---|---|---|---|
| IR TX carrier | PWM output (`irsnd_set_freq` + `HAL_TIMEx_PWMN_Start`) | `PC5` | none |
| Sub-GHz RX | input capture | SI4463 GPIO0 (port E) | `TIM1_CC_IRQn` |
| Sub-GHz TX | PWM + DMA | SI4463 GPIO2 (port D) | `TIM1_UP_IRQn`, `GPDMA1_REQUEST_TIM1_UP` |

The IR carrier's own `HAL_TIM_PWM_Init(TIM1)` inside
`infrared_encode_sys_init()` is **commented out** (since Initial Upload); IR
relies on `irsnd_set_freq()` to re-init the PWM and does **not** first DeInit
`TIM1` or clear the DMA/IRQ bindings a prior Sub-GHz session may have left. When
the Sub-GHz receiver (now driven aggressively by the RSSI/pre-scan rework) leaves
`TIM1` in input-capture mode or with a `TIM1_UP` DMA request bound, the IR
carrier can stop reaching `PC5` while the `TIM16` baseband still runs to
completion — the firmware shows "Transmitting… / Done" but **no RF is emitted**.

**Contract:** whoever uses `TIM1` must fully reclaim it (DeInit → re-init for its
own mode, clear stale DMA/IRQ) before driving it, and hand it back neutral. Any
future change to Sub-GHz's `TIM1` usage must preserve this or IR TX silently
dies. Fix tracked in `SPEC.md` (§4 root cause, §6 fix, §7 bench triage).

## 1. The problem

Universal-Remote / saved-remote transmit uses IRSND (`Infrared/irsnd.c`) to
encode a parsed `.ir` record. The `ir_database/**.ir` files store **Flipper-
canonical** values — the minimal address/command a Flipper writes — while IRSND
packs whatever 16-bit values it is handed **verbatim**. For protocols that carry
redundancy Flipper omits (a duplicated device byte, a complement byte), the
canonical value produces a **structurally wrong on-air frame** that real devices
reject. Cloning a signal (capture → re-transmit) mostly works because the
IRMP-decoded value already carries the redundancy; the DB path does not.

Fix: a single, **idempotent**, TX-time expander,
`ir_expand_parsed_code(uint8_t proto, uint16_t *addr, uint16_t *cmd)`, applied
immediately before `irsnd_generate_tx_data()` at the two DB-fed sites
(`transmit_command`, `ir_blast_step`). It is **not** applied in the reader, so
the on-disk DB and the rename/delete rewrite path stay canonical. Idempotency
means a clone/replay/already-expanded value flows through unchanged.

## 2. How the audit was done

`tools/host_test/test_ir_tx_frames.c` compiles the **real** `Infrared/irsnd.c`
on host (via `irsnd_host.c` + the `tools/host_test/stubs/` HAL/`main.h`/
`m1_infrared.h` shadows) and asserts the exact bytes IRSND packs into
`ir_tx_buffer[]`. Oracles are pinned from the protocol definition (e.g. NEC's
`{addr,~addr,cmd,~cmd}` bit-reversed), **not** from the encoder, so they catch
real bugs. Run: `sh tools/host_test/run_tests.sh`.

`ir_tx_buffer[]` holds each logical on-air byte **bit-reversed** (verified:
Samsung `0xE0 == bitrev8(0x07)`). That fact lets oracles be built from a known
logical frame.

## 3. Per-protocol status

Histogram of shipped `ir_database/` (472 parsed records):

| Protocol | Codes | Status | Notes |
|----------|------:|--------|-------|
| Samsung32 | 59 | **FIXED (expand)** | dup device byte; synth `~cmd` check byte |
| NEC | 226 | **FIXED (expand)** | synth `~addr`; NECext (hi≠0) left as-is |
| Denon | 36 | OK (no-op) | IRSND synthesizes the 2nd-frame `~cmd` |
| Bose | 14 | OK (no-op) | IRSND synthesizes `~cmd`; Bose has no address |
| JVC | 2 | OK (no-op) | addr + cmd packed verbatim |
| RC5 | 44 | OK (no-op) | bit-packed; carries a stateful toggle bit |
| RC6 | 4 | OK (no-op) | bit-packed; carries a stateful toggle bit |
| **SIRC/SIRC20** | 51 | **BROKEN** | device address dropped — see §4.1 |
| **RCA** | 2 | **BROKEN (no TX)** | no IRSND encoder — see §4.2 |
| **Apple** | 7 | **SUSPECT** | address/command handling unverified — see §4.3 |
| **Kaseikyo** | 27 | **SUSPECT** | vendor/genre/checksum mapping unverified — see §4.3 |

"FIXED" and "OK" protocols each have a green oracle in `test_ir_tx_frames.c`, so
the audit is provably complete. The BROKEN/SUSPECT protocols are left as an
expander **no-op** (behavior unchanged) with an oracle asserting the expander
does not silently ship a guessed fix.

## 4. Known limitations (follow-up work)

None of these are fixable by a canonical→expanded value transform. They need
either raw conversion of the `.ir` records or a change to the vendor encoder —
both require **on-device hardware verification**, which the Linux host workspace
cannot provide. They were deliberately left unshipped rather than guessed.

### 4.1 SIRC / SIRC20 (51 codes) — device address dropped

Sony SIRC-12 = 7 command bits + 5 device-address bits. IRSND's SIRC case reads
the *frame bit-length* from the **address high byte** and only emits the address
when `additional_bitlen > 3`, placing those bits at `ir_tx_buffer[]` positions
15–19. But `complete_data_len = SIRCS_MINIMUM_DATA_LEN(12) + additional_bitlen`
(`irsnd.c:1280`), so any value large enough to emit the address forces a ≥16-bit
frame. **There is no address-field value that yields a correct 12-bit Sony frame
*with* the device address.** All 49 shipped SIRC records (+2 SIRC20) store
address high byte `00`, so IRSND transmits command-only (`A8 00 00` for the
shipped `0x01/0x15`).

Additionally, IRMP *decode* (`irmp.c:2119`) stores SIRC-12 as `address =
5-bit device` (high byte 0), i.e. the same shape as the DB — so even a **clone**
of a SIRC-12 signal loses the bit-length and would fail the same way.

Pick-up options: (a) convert the 51 records to `raw` timing (SIRC = 40 kHz
carrier, 2.4 ms header, 0.6 ms bit-low, 1.2/0.6 ms marks), or (b) fix IRSND's
SIRC encoder to place the address for 12/15-bit frames. Both need a Sony device
to confirm.

### 4.2 RCA (2 codes) — no IRSND encoder

Both `.ir` records map `RCA → IRMP_RCCAR_PROTOCOL` — a mis-map to the RC *car*
protocol, which IRSND has compiled out (`IRSND_SUPPORT_RCCAR_PROTOCOL==0`,
`irsndconfig.h:70`), so `irsnd_generate_tx_data` falls through and **transmits
nothing**. IRMP has no real RCA-TV encoder (`IRMP_RCII_PROTOCOL`=54 is "RC II",
not RCA). Pick-up: convert the 2 records to `raw` (RCA = 56 kHz, 4 ms header,
0.5 ms bit-low, 1/2 ms marks, address+command each followed by its complement),
verified on an RCA TV.

### 4.3 Apple (7) & Kaseikyo (27) — suspect, not deep-audited

- **Apple:** IRSND's Apple case does `command |= (address << 8)` then forces
  `address = 0x87EE` (the Apple customer code). But Flipper already stores
  `0x87EE` in the **address** field, so the fed address lands in the command high
  byte — almost certainly wrong. Needs a decode/re-encode round-trip or an Apple
  remote to confirm and fix.
- **Kaseikyo/Panasonic:** vendor id + genre + XOR checksum. IRSND computes the
  checksum itself, but whether Flipper's 4-byte address (`00 40 04 00`) and the
  flags→genre mapping line up with IRSND's field layout was not verified.

Grouped here as "presumed-unverified" by owner decision; no code change shipped.

### 4.4 LG & Samsung48 — latent traps (no shipped files)

- `LG → IRMP_LGAIR_PROTOCOL` is the LG **air-conditioner** protocol, not a TV
  remote → wrong frame if ever used.
- `Samsung48 → IRMP_SAMSUNG48_PROTOCOL` has IRSND TX compiled out → no TX.

No shipped `.ir` uses either. Both rows are commented in **both** protocol tables
(`flipper_ir.c` `ir_proto_table[]` and `m1_ir_universal.c`
`map_flipper_protocol()`); no mapping was changed and no new IRSND protocol was
enabled (nothing needs one).

## 5. Owner hardware-verification checklist

The host suite is the local gate; on-air correctness is the owner's step.

- [ ] Build `.bin` (`append_crc32.py --c3-revision 4`), flash the M1.
- [ ] **Samsung (headline):** a 2010 Samsung TV responds to Power from Universal
      Remotes (was the reported failure).
- [ ] **NEC:** an NEC device responds (largest protocol, 226 codes).
- [ ] TX no longer beeps (LED still blinks); clone capture still beeps; the
      buzzer never locks out after a zero-arg call.
- [ ] Spot-check Denon / Bose / JVC / RC5 / RC6 devices if available (audited OK).
- [ ] Note SIRC / RCA / Apple / Kaseikyo as unverified/known-broken (this doc).
