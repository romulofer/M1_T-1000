# Universal AC Remote Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Flipper-style "Universal AC" brute-force remote that fires every brand's version of a chosen AC function (Off / Cool / Heat / Dehumidify) from a bundled `ac.ir`, including the raw-encoded codes that make up 94% of the file.

**Architecture:** Extend the existing streaming brute-force engine (`uremote_bf_stream`) to capture each raw record's timing samples into a shared buffer as it streams, and give the fire path a raw branch that transmits that buffer via the existing TIM16 OTA mechanism. Then add an AC category table + dashboard item, reusing the TV screen flow.

**Tech Stack:** C (STM32H5 / FreeRTOS / IRMP-irsnd), Flipper `.ir` format, u8g2 display, host test harness (gcc on Linux).

**Design:** `docs/superpowers/specs/2026-07-10-universal-ac-bruteforce-design.md`

**Constraints:** Cannot build/flash STM32 firmware from Linux (Windows toolchain) — host tests are the automated gate; firmware build + bench are owner steps. No AI attribution in commits. Commit only files named per task. LOCAL ONLY.

---

## File Structure

- `m1_csrc/m1_uremote_bf.h` — add raw-sample params to parser + stream prototypes.
- `m1_csrc/m1_uremote_bf.c` — capture raw samples in the parser; stop skipping raw in the stream. Stays HAL-free / host-testable.
- `m1_csrc/flipper_ir.h` — bump `FLIPPER_IR_RAW_MAX_SAMPLES` 512 → 640.
- `m1_csrc/m1_ir_universal.c` — shared raw buffer; `fire_raw_samples()` helper (extracted from `transmit_raw_command`); raw branch in `uremote_fire_cmd`; generalized category screen; AC category + dashboard item + dispatch; pass buffer into `uremote_bf_stream`.
- `ir_database/Universal/ac.ir` — bundled data (copied from Flipper assets).
- `tools/host_test/test_uremote_bf.c` — new assertions: raw counted/fired, samples captured, clamp.

---

## Task 1: Parser captures raw timing samples (host-tested)

**Files:**
- Modify: `m1_csrc/m1_uremote_bf.h` (parser + stream prototypes)
- Modify: `m1_csrc/m1_uremote_bf.c:61` (`uremote_parse_signal_block`), `:162` (`uremote_bf_stream`)
- Modify: `m1_csrc/m1_ir_universal.c:818` (call site), `:1481` (call site)
- Test: `tools/host_test/test_uremote_bf.c`

- [ ] **Step 1: Write the failing test** — append to `tools/host_test/test_uremote_bf.c` inside `main()` before the final summary. This adds a raw record to a fixture and asserts capture + that raw is no longer skipped. First, add a raw `Off` record to the existing library fixture string (`FIX_LIB` content near line 48-65): add this block to the fixture text (mark/space ints):

```c
    "name: Off\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\n"
    "data: 9000 4500 560 560 560 1690 560 40000\n"
```

Then add the test body:

```c
    /* --- Raw capture + fire (AC support) --- */
    int32_t rawbuf[64];
    uint16_t rawcap = 64;
    /* Off has 1 raw record in the fixture; it must now be counted (not skipped). */
    CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Off", NULL, NULL, rawbuf, rawcap), 1,
                 "raw Off counted");

    /* Fire pass captures the 8 samples of the raw Off record. */
    struct { uint16_t count; bool is_raw; int32_t first; int32_t last; } rawcap_res = {0};
    /* capture_cb variant defined below records raw_count + buffer ends. */
    n = uremote_bf_stream(FIX_LIB, "Off", raw_probe_cb, &rawcap_res, rawbuf, rawcap);
    CHECK_EQ_INT(n, 1, "raw Off fired once");
    CHECK(rawcap_res.is_raw, "Off record flagged raw");
    CHECK_EQ_INT(rawcap_res.count, 8, "raw Off sample count");
    CHECK_EQ_INT((int)rawcap_res.first, 9000, "first sample = 9000");
    CHECK_EQ_INT((int)rawcap_res.last, 40000, "last sample = 40000 (int32, no 16-bit clip)");
```

Add this callback above `main()` (near the existing `capture_cb`):

```c
static int32_t *g_rawbuf;   /* set in main() to the buffer passed to stream */
static bool raw_probe_cb(void *ctx, const ir_universal_cmd_t *cmd, uint16_t idx)
{
    (void)idx;
    struct RP { uint16_t count; bool is_raw; int32_t first; int32_t last; } *r = ctx;
    r->count  = cmd->raw_count;
    r->is_raw = cmd->is_raw;
    if (cmd->raw_count > 0) { r->first = g_rawbuf[0]; r->last = g_rawbuf[cmd->raw_count - 1]; }
    return true;
}
```

And in `main()` set `g_rawbuf = rawbuf;` before the raw test block.

- [ ] **Step 2: Update the existing `uremote_bf_stream` calls in the test to the new signature.** Every existing `uremote_bf_stream(FIX_LIB, "Power", NULL, NULL)` etc. gains two trailing args `, NULL, 0`. Also update the earlier "raw Power is skipped" test (line ~100): raw records are NO LONGER skipped, so `count Power` changes from 3 to 4 (the fixture's raw Power is now counted). Change:

```c
    CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Power", NULL, NULL, NULL, 0), 4, "count Power (raw now included)");
    CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Vol_up", NULL, NULL, NULL, 0), 2, "count Vol_up");
    CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Mute", NULL, NULL, NULL, 0), 1, "count Mute");
    CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Ch_next", NULL, NULL, NULL, 0), 0, "count missing");
```

Update the fire-pass `capture_cb` calls (lines ~107, ~116, ~125) similarly with `, NULL, 0` and bump the expected Power fire count from 3 to 4 if that record fires (raw fires too now). If `capture_cb` stores names into a fixed array, raise its bound to 4.

- [ ] **Step 3: Run test to verify it fails**

Run: `bash tools/host_test/run_tests.sh`
Expected: FAIL — `uremote_bf_stream` / `uremote_parse_signal_block` too few arguments (signature not yet changed).

- [ ] **Step 4: Change the prototypes** in `m1_csrc/m1_uremote_bf.h`. Replace:

```c
bool uremote_parse_signal_block(flipper_file_t *ff, ir_universal_cmd_t *cmd);
```
with:
```c
/* raw_out/raw_cap optional: for a raw record, up to raw_cap timing samples are
 * written to raw_out and cmd->raw_count is set to the stored count. Pass NULL/0
 * to keep count-only behaviour (parsed records ignore these). */
bool uremote_parse_signal_block(flipper_file_t *ff, ir_universal_cmd_t *cmd,
                                int32_t *raw_out, uint16_t raw_cap);
```
and replace:
```c
uint16_t uremote_bf_stream(const char *path, const char *record_name,
                           uremote_bf_cb_t cb, void *ctx);
```
with:
```c
uint16_t uremote_bf_stream(const char *path, const char *record_name,
                           uremote_bf_cb_t cb, void *ctx,
                           int32_t *raw_out, uint16_t raw_cap);
```

- [ ] **Step 5: Implement capture in the parser** — `m1_csrc/m1_uremote_bf.c`. Change the signature at line 61 to match the header. Then replace the `data` branch (the block that counts samples, ~lines 120-135) with one that also stores values:

```c
        else if (strcmp(ff_get_key(ff), "data") == 0 && is_raw_type)
        {
            const char *p = ff_get_value(ff);
            uint16_t count = 0;
            while (*p)
            {
                while (*p == ' ')
                    p++;
                if (*p == '\0')
                    break;
                if (raw_out != NULL && count < raw_cap)
                    raw_out[count] = (int32_t)strtol(p, NULL, 10);
                count++;
                while (*p && *p != ' ')
                    p++;
            }
            /* If a buffer was given, only the samples that fit were stored. */
            cmd->raw_count = (raw_out != NULL && count > raw_cap) ? raw_cap : count;
        }
```

- [ ] **Step 6: Thread the buffer through the stream** — `m1_csrc/m1_uremote_bf.c` in `uremote_bf_stream`. Update its signature to match the header, update the internal parse call (~line 190) from `uremote_parse_signal_block(&ff, &cmd)` to `uremote_parse_signal_block(&ff, &cmd, raw_out, raw_cap)`, and **remove the raw skip** (~lines 201-202):

```c
        /* (removed) previously: if (cmd.is_raw) continue;  -- raw now fired */
```

- [ ] **Step 7: Fix the firmware call sites** so the project still compiles later.
  - `m1_csrc/m1_ir_universal.c:818` — this is `show_commands`, single-file replay; it does not need captured samples. Change `uremote_parse_signal_block(&ff, &s_commands[count])` to `uremote_parse_signal_block(&ff, &s_commands[count], NULL, 0)`.
  - `m1_csrc/m1_ir_universal.c:1481` — leave for Task 5 (it becomes the buffer-passing call). For now change to `uremote_bf_stream(file_path, fn->record_name, uremote_bf_fire_cb, &c, NULL, 0)` so it compiles; Task 5 swaps `NULL, 0` for the real buffer.

- [ ] **Step 8: Run test to verify it passes**

Run: `bash tools/host_test/run_tests.sh`
Expected: PASS — all uremote checks green, including the new raw capture asserts.

- [ ] **Step 9: Commit**

```bash
git add m1_csrc/m1_uremote_bf.h m1_csrc/m1_uremote_bf.c m1_csrc/m1_ir_universal.c tools/host_test/test_uremote_bf.c
git commit -m "uremote: streaming parser captures raw samples; sweep no longer skips raw"
```

---

## Task 2: Bigger raw buffer + shared sweep sample buffer

**Files:**
- Modify: `m1_csrc/flipper_ir.h:17`
- Modify: `m1_csrc/m1_ir_universal.c` (near line 94-96, the raw buffer statics)

- [ ] **Step 1: Bump the sample cap** — `m1_csrc/flipper_ir.h`. Change:

```c
#define FLIPPER_IR_RAW_MAX_SAMPLES  512
```
to:
```c
#define FLIPPER_IR_RAW_MAX_SAMPLES  640   /* ac.ir longest raw frame = 595 samples */
```

- [ ] **Step 2: Add the shared sweep sample buffer** — `m1_csrc/m1_ir_universal.c`, right after the existing `s_raw_ota_buffer` declaration (~line 96):

```c
/* Raw timing samples for the brute-force sweep: the streaming parser fills this
 * with the current raw record's samples; fire_raw_samples() transmits them.
 * int32_t matches flipper_ir_signal_t.raw.samples (ac.ir gaps exceed 16 bits). */
static int32_t s_uremote_raw_samples[FLIPPER_IR_RAW_MAX_SAMPLES];
```

- [ ] **Step 3: Verify host suite still builds/passes** (the host test compiles `m1_uremote_bf.c`, not this file, but run it to confirm no regression):

Run: `bash tools/host_test/run_tests.sh`
Expected: PASS (unchanged).

- [ ] **Step 4: Commit**

```bash
git add m1_csrc/flipper_ir.h m1_csrc/m1_ir_universal.c
git commit -m "uremote: raw sample cap 512->640, add shared sweep sample buffer"
```

---

## Task 3: Extract `fire_raw_samples()` helper (no behaviour change)

**Files:**
- Modify: `m1_csrc/m1_ir_universal.c` — `transmit_raw_command` (~1037-1155)

- [ ] **Step 1: Add the helper** above `transmit_raw_command`. It contains the OTA-build + TIM16 kick currently inline in `transmit_raw_command` (lines ~1099-1150), parameterised on samples/count/freq. No file read, no screen, no buzzer:

```c
/*============================================================================*/
/* Convert alternating mark/space timing samples to the TIM16 OTA buffer and
 * kick asynchronous TX. Shared by transmit_raw_command() and the brute-force
 * raw fire path. Assumes infrared_encode_sys_init() has been called.          */
/*============================================================================*/
static void fire_raw_samples(const int32_t *samples, uint16_t count, uint32_t freq)
{
    uint16_t i, ota_len;
    uint32_t duration;

    if (samples == NULL || count == 0)
        return;

    ota_len = count;
    if (ota_len > IR_RAW_OTA_BUFFER_MAX)
        ota_len = IR_RAW_OTA_BUFFER_MAX;

    for (i = 0; i < ota_len; i++)
    {
        duration = (uint32_t)abs((int)samples[i]);
        if (duration > 65534)
            duration = 65534;
        if (duration < 2)
            duration = 2;

        if (i % 2 == 0)
            s_raw_ota_buffer[i] = (uint16_t)duration | IR_OTA_PULSE_BIT_MASK;
        else
            s_raw_ota_buffer[i] = (uint16_t)duration & IR_OTA_SPACE_BIT_MASK;
    }

    irsnd_set_carrier_freq(freq);

    ir_ota_data_tx_active   = TRUE;
    ir_ota_data_tx_counter  = 0;
    ir_ota_data_tx_len      = ota_len;
    pir_ota_data_tx_buffer  = s_raw_ota_buffer;

    __HAL_TIM_URS_ENABLE(&Timerhdl_IrTx);
    Timerhdl_IrTx.Instance->ARR = s_raw_ota_buffer[0];
    HAL_TIM_GenerateEvent(&Timerhdl_IrTx, TIM_EVENTSOURCE_UPDATE);
    __HAL_TIM_URS_DISABLE(&Timerhdl_IrTx);

    if (HAL_IS_BIT_SET(Timerhdl_IrTx.Instance->SR, TIM_FLAG_UPDATE))
        CLEAR_BIT(Timerhdl_IrTx.Instance->SR, TIM_FLAG_UPDATE);

    if (s_raw_ota_buffer[0] & 0x0001)
        irsnd_on();

    __HAL_TIM_ENABLE(&Timerhdl_IrTx);

    if (ota_len > 1)
        Timerhdl_IrTx.Instance->ARR = s_raw_ota_buffer[++ir_ota_data_tx_counter];
}
```

- [ ] **Step 2: Replace the inlined block in `transmit_raw_command`** (from the `/* Convert Flipper raw data ... */` comment at ~line 1099 through the `if (ota_len > 1) ... ` at ~line 1148) with a call, keeping the `infrared_encode_sys_init()` and `m1_buzzer_notification()` that bracket it:

```c
    /* Initialize the IR encoder hardware (TIM1 carrier + TIM16 baseband) */
    infrared_encode_sys_init();

    fire_raw_samples(s_raw_tx_signal.raw.samples,
                     (uint16_t)s_raw_tx_signal.raw.sample_count,
                     s_raw_tx_signal.raw.frequency);

    m1_buzzer_notification();
```

- [ ] **Step 3: Verify no host regression** (this file isn't host-compiled, but keep the gate green):

Run: `bash tools/host_test/run_tests.sh`
Expected: PASS (unchanged).

- [ ] **Step 4: Commit**

```bash
git add m1_csrc/m1_ir_universal.c
git commit -m "uremote: extract fire_raw_samples() from transmit_raw_command"
```

---

## Task 4: Raw fire branch in the sweep + pacing

**Files:**
- Modify: `m1_csrc/m1_ir_universal.c` — `uremote_fire_cmd` (~1355-1376), `uremote_bf_fire_cb` (~1393-1447), `uremote_bf_run` stream call (~1481)

- [ ] **Step 1: Add the raw branch to `uremote_fire_cmd`.** Replace the early raw rejection:

```c
    if (cmd->is_raw)
        return false;   /* brute-force is parsed-only; raw is skipped upstream */
```
with:
```c
    if (cmd->is_raw)
    {
        if (cmd->raw_count == 0 || cmd->raw_freq == 0)
            return false;
        infrared_encode_sys_init();
        fire_raw_samples(s_uremote_raw_samples, cmd->raw_count, cmd->raw_freq);
        return true;
    }
```

- [ ] **Step 2: In the callback, fire raw once and wait for it to finish; keep the 4x burst for parsed only.** In `uremote_bf_fire_cb`, replace the burst block:

```c
	{
		bool fired = false;
		for (uint8_t r = 0; r < UREMOTE_BF_FRAME_REPEATS; r++)
		{
			if (!uremote_fire_cmd(cmd))
				break;
			fired = true;
			vTaskDelay(pdMS_TO_TICKS(UREMOTE_BF_FRAME_GAP_MS));
		}
		if (fired)
			c->sent++;
	}

	vTaskDelay(pdMS_TO_TICKS(160));   /* settle gap before the next code */
```
with:
```c
	if (cmd->is_raw)
	{
		/* Raw AC frames carry full state and are long: fire once, then wait for
		 * the TIM16 ISR to finish the frame before moving on. */
		if (uremote_fire_cmd(cmd))
		{
			uint16_t guard = 0;   /* ~250 ms cap so a stuck frame can't hang the sweep */
			while (ir_ota_data_tx_active && guard < 250)
			{
				vTaskDelay(pdMS_TO_TICKS(5));
				guard += 5;
			}
			c->sent++;
		}
		vTaskDelay(pdMS_TO_TICKS(120));   /* settle gap before the next code */
	}
	else
	{
		bool fired = false;
		for (uint8_t r = 0; r < UREMOTE_BF_FRAME_REPEATS; r++)
		{
			if (!uremote_fire_cmd(cmd))
				break;
			fired = true;
			vTaskDelay(pdMS_TO_TICKS(UREMOTE_BF_FRAME_GAP_MS));
		}
		if (fired)
			c->sent++;
		vTaskDelay(pdMS_TO_TICKS(160));   /* settle gap before the next code */
	}
```

- [ ] **Step 3: Pass the shared buffer into the stream** — `uremote_bf_run` (~line 1481). Change:

```c
	uremote_bf_stream(file_path, fn->record_name, uremote_bf_fire_cb, &c, NULL, 0);
```
to:
```c
	uremote_bf_stream(file_path, fn->record_name, uremote_bf_fire_cb, &c,
	                  s_uremote_raw_samples, FLIPPER_IR_RAW_MAX_SAMPLES);
```

- [ ] **Step 4: Verify host suite still passes** (fire path is HAL, not host-compiled):

Run: `bash tools/host_test/run_tests.sh`
Expected: PASS (unchanged).

- [ ] **Step 5: Commit**

```bash
git add m1_csrc/m1_ir_universal.c
git commit -m "uremote: fire raw codes in the sweep (once + wait); parsed keeps burst"
```

---

## Task 5: AC category + generalized screen + dashboard item

**Files:**
- Modify: `m1_csrc/m1_uremote_bf.h` (declare `uremote_category_ac`)
- Modify: `m1_csrc/m1_uremote_bf.c` (define `s_ac_functions`, `uremote_category_ac`)
- Modify: `m1_csrc/m1_ir_universal.c` (generalize screen; dashboard item + dispatch)

- [ ] **Step 1: Declare the AC category** — `m1_csrc/m1_uremote_bf.h`, after `extern const uremote_category_t uremote_category_tv;`:

```c
extern const uremote_category_t uremote_category_ac;
```

- [ ] **Step 2: Define the AC category** — `m1_csrc/m1_uremote_bf.c`, after the TV table (`uremote_category_tv`):

```c
/*============================================================================*/
/* AC category. ac.ir is mostly raw (per-brand full-state frames); the sweep    */
/* now fires raw records, so these functions map to Flipper's universal AC set. */
/*============================================================================*/
static const uremote_function_t s_ac_functions[] = {
	{ "Off",        "Off"     },
	{ "Cool Hi",    "Cool_hi" },
	{ "Cool Lo",    "Cool_lo" },
	{ "Heat Hi",    "Heat_hi" },
	{ "Heat Lo",    "Heat_lo" },
	{ "Dehumidify", "Dh"      },
};

const uremote_category_t uremote_category_ac = {
	.menu_label     = "Universal AC",
	.ir_file_path   = IR_UNIVERSAL_IRDB_ROOT "/Universal/ac.ir",
	.functions      = s_ac_functions,
	.function_count = (uint8_t)(sizeof(s_ac_functions) / sizeof(s_ac_functions[0])),
};
```

- [ ] **Step 3: Generalize the category screen** — `m1_csrc/m1_ir_universal.c`. Rename `uremote_tv_screen(void)` to `uremote_category_screen(const uremote_category_t *cat)` and delete the internal `const uremote_category_t *cat = &uremote_category_tv;` line (now a parameter). Update its prototype near the other prototypes accordingly. Then add two thin wrappers where `uremote_tv_screen` used to be declared/called:

```c
static void uremote_tv_screen(void) { uremote_category_screen(&uremote_category_tv); }
static void uremote_ac_screen(void) { uremote_category_screen(&uremote_category_ac); }
```

- [ ] **Step 4: Add the dashboard item** — `m1_csrc/m1_ir_universal.c`. Change `#define DASHBOARD_ITEM_COUNT 8` to `9`, and add `"Universal AC"` to `s_dashboard_items[]` after `"Universal TV"`:

```c
static const char *s_dashboard_items[DASHBOARD_ITEM_COUNT] = {
	"Browse IRDB",
	"Learned",
	"Favorites",
	"Recent",
	"Remote Mode",
	"Power Off TVs",
	"Power Off A/V",
	"Universal TV",
	"Universal AC"
};
```

- [ ] **Step 5: Dispatch the new item** — `m1_csrc/m1_ir_universal.c`, in the dashboard switch (~line 379):

```c
						case 7: /* Universal TV — Flipper-style brute-force remote */
							uremote_tv_screen();
							break;
						case 8: /* Universal AC — raw-capable brute-force remote */
							uremote_ac_screen();
							break;
```

- [ ] **Step 6: Verify host suite still passes**

Run: `bash tools/host_test/run_tests.sh`
Expected: PASS (unchanged — this task is UI wiring).

- [ ] **Step 7: Commit**

```bash
git add m1_csrc/m1_uremote_bf.h m1_csrc/m1_uremote_bf.c m1_csrc/m1_ir_universal.c
git commit -m "uremote: Universal AC category + dashboard item, generalize category screen"
```

---

## Task 6: Bundle ac.ir data

**Files:**
- Create: `ir_database/Universal/ac.ir` (copied from Flipper assets)

- [ ] **Step 1: Copy the data file**

Run:
```bash
cp "/home/romulo2/development/utilities/flipper_firmwares/Momentum-Firmware/applications/main/infrared/resources/infrared/assets/ac.ir" \
   "ir_database/Universal/ac.ir"
```

- [ ] **Step 2: Sanity-check counts**

Run:
```bash
grep -c '^name:' ir_database/Universal/ac.ir; grep -c 'type: raw' ir_database/Universal/ac.ir
```
Expected: 322 names, 305 raw (matches the design's coverage figures).

- [ ] **Step 3: Confirm the SD deploy path.** The category path is `0:/IR/Universal/ac.ir`. Verify how `tv.ir` reaches the SD card (same mechanism must carry `ac.ir`):

Run:
```bash
grep -rn "Universal/tv.ir\|ir_database/Universal" build.sh CMakeLists.txt sdcard 2>/dev/null | head
ls sdcard 2>/dev/null
```
If `tv.ir` is staged into `sdcard/` or copied by a script, add `ac.ir` alongside it the same way (e.g. `cp ir_database/Universal/ac.ir sdcard/IR/Universal/`). If deployment is manual (owner copies `ir_database/` to SD), no change needed — note it in the commit.

- [ ] **Step 4: Commit**

```bash
git add ir_database/Universal/ac.ir
git commit -m "uremote: bundle universal ac.ir (Flipper aggregated AC library)"
```

---

## Task 7: Docs + final host gate

**Files:**
- Modify: `documentation/universal_remotes.md` (if present) or the feature doc

- [ ] **Step 1: Document the AC remote.** Add a short section mirroring the TV one: the dashboard item, the six functions, that AC codes are mostly raw and now fired, the bench checklist (point at a real AC, run Off, confirm power-off; then Cool Hi / Heat Hi). If `documentation/universal_remotes.md` does not exist, add the section to the design doc's sibling or `README.md` where TV is described.

Run to locate:
```bash
grep -rln "Universal TV\|universal_remotes" documentation README.md docs 2>/dev/null
```

- [ ] **Step 2: Full host suite green**

Run: `bash tools/host_test/run_tests.sh`
Expected: `RESULT: PASS`, all uremote checks including raw capture.

- [ ] **Step 3: Commit**

```bash
git add documentation/universal_remotes.md
git commit -m "docs: universal AC brute-force remote"
```

---

## Owner bench verification (post-implementation, Windows toolchain)

1. Build firmware (STM32CubeIDE toolchain + CRC step per CLAUDE.md).
2. Ensure `ac.ir` is on SD at `0:/IR/Universal/ac.ir`.
3. Flash. IR → Universal Remote → **Universal AC** → **Off**, pointed at a real AC.
4. Confirm the AC powers off during the sweep (it will fire ~139 Off codes). Then test **Cool Hi** / **Heat Hi** latch.
5. If nothing emits at all (no LED/AC reaction), that is the separate carrier issue (`ir-tx-silence-tim1-conflict`), not this feature.

---

## Notes for the implementer

- The host test is the only automated gate available on Linux; the firmware cannot be built here. Every task keeps the host suite green.
- Do not modify `S_M1_FW_CONFIG_t`. No heap. Keep FreeRTOS include order.
- `abs()` on `int32_t` in `fire_raw_samples` needs `<stdlib.h>` — already included in `m1_ir_universal.c`; verify before relying on it.
- Commit messages: no AI attribution, no `Co-Authored-By`. LOCAL ONLY — do not push.
