# Universal Remote — Flipper-style TV brute-force — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Flipper-style TV universal remote to the M1: from the existing IR "Universal Remote" dashboard, pick a function (Power/Vol/Ch/Mute) and auto-fire every brand's version of that one function from a bundled aggregated `tv.ir`, BACK to stop.

**Architecture:** A new host-testable leaf module `m1_uremote_bf.{c,h}` owns the Flipper `.ir` block parsing (moved out of `m1_ir_universal.c` to remove duplication) plus a streaming, name-filtered brute-force iterator. `m1_ir_universal.c` gains one dashboard item, a function-list screen, and a brute-force progress screen that drives the iterator and reuses the existing IRMP encode/transmit path. Data is Flipper's aggregated `tv.ir` bundled under `ir_database/Universal/`.

**Tech Stack:** C11, STM32H5 HAL, FreeRTOS, u8g2, FatFs, IRMP/IRSND, the M1 `flipper_file` parser. Host tests are plain C compiled with `cc` against the FatFs/IRMP shims in `tools/host_test/`.

---

## Context an executor needs

- **Cannot build/flash firmware from this Linux host** (STM32 toolchain is Windows-only). Firmware build/flash/bench steps are executed on the Windows toolchain or by the owner. **Host tests (`tools/host_test/`) are the Linux-verifiable gate** and MUST pass here.
- The Flipper universal data lives at
  `../../flipper_firmwares/Momentum-Firmware/applications/main/infrared/resources/infrared/assets/tv.ir`
  (815 signals; header `Filetype: IR library file`; functions `Power` 324, `Vol_up` 99, `Vol_dn` 104, `Ch_next` 94, `Ch_prev` 101, `Mute` 93).
- Repo `ir_database/` deploys to SD `0:/IR` (`IR_UNIVERSAL_IRDB_ROOT` in `m1_csrc/m1_ir_universal.h`).
- The M1 `flipper_file` API (`m1_csrc/flipper_file.h`): `ff_open`, `ff_close`, `ff_validate_header(ctx, filetype, min_ver)`, `ff_read_line`, `ff_is_separator`, `ff_parse_kv`, `ff_get_key`, `ff_get_value`, `ff_parse_hex_bytes`, and `flipper_file_t.eof`.
- `ir_universal_cmd_t` (public in `m1_ir_universal.h`) is the parsed-signal struct reused throughout.
- Known orthogonal risk: IR-TX-silence / TIM1-vs-SubGHz. Not fixed here; the brute-force reuses the same TX path as every other IR feature.

---

## File structure

- **Create** `m1_csrc/m1_uremote_bf.h` — public API: streaming brute-force iterator, moved parse-helper decls, TV category table decls.
- **Create** `m1_csrc/m1_uremote_bf.c` — moved `map_flipper_protocol` + `parse_ir_signal_block`, the `uremote_bf_stream` iterator, and the TV table.
- **Create** `tools/host_test/test_uremote_bf.c` — host test for the streaming name filter.
- **Modify** `tools/host_test/run_tests.sh` — build + run the new host test.
- **Modify** `m1_csrc/m1_ir_universal.c` — drop the two moved statics, include the new header, add the `"Universal TV"` dashboard item + function-list screen + brute-force screen.
- **Create** `ir_database/Universal/tv.ir` — bundled aggregated data.
- **Create/Modify** `documentation/universal_remotes.md` — flow + bench checklist.

---

## Task 1: Streaming brute-force core module (host-tested)

**Files:**
- Create: `m1_csrc/m1_uremote_bf.h`
- Create: `m1_csrc/m1_uremote_bf.c`
- Create: `tools/host_test/test_uremote_bf.c`
- Modify: `tools/host_test/run_tests.sh`

- [ ] **Step 1: Write the failing host test**

Create `tools/host_test/test_uremote_bf.c`:

```c
/* See COPYING.txt for license details. */

/*
 * test_uremote_bf.c  (HOST TEST)
 *
 * Verifies the streaming, name-filtered universal-remote brute-force iterator
 * (m1_uremote_bf.c) against the real m1_csrc/flipper_file.c using the FatFs
 * host shim. Writes a small "IR library file" fixture with several brands'
 * Power / Vol_up records plus noise, then checks that uremote_bf_stream():
 *   - counts only records whose name matches the target (cb == NULL),
 *   - invokes the callback exactly on matching records, in file order,
 *   - stops early when the callback returns false,
 *   - accepts both "IR library file" and "IR signals file" headers.
 */

#include "m1_uremote_bf.h"
#include "flipper_file.h"

#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
	do {                                                                       \
		g_checks++;                                                            \
		if (!(cond)) {                                                         \
			g_failures++;                                                      \
			printf("  FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);        \
		}                                                                      \
	} while (0)

#define CHECK_EQ_INT(actual, expected, msg)                                    \
	do {                                                                       \
		g_checks++;                                                            \
		long _a = (long)(actual);                                             \
		long _e = (long)(expected);                                           \
		if (_a != _e) {                                                        \
			g_failures++;                                                      \
			printf("  FAIL: %s  (got %ld, want %ld)  (%s:%d)\n",               \
			       (msg), _a, _e, __FILE__, __LINE__);                         \
		}                                                                      \
	} while (0)

#define FIX_LIB     "uremote_lib.ir"

/* 3x Power, 2x Vol_up, 1x Mute, interleaved. */
static const char *k_lib =
	"Filetype: IR library file\n"
	"Version: 1\n"
	"#\n"
	"name: Power\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00 00\n"
	"#\n"
	"name: Vol_up\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 02 00 00 00\n"
	"#\n"
	"name: Power\ntype: parsed\nprotocol: SIRC\naddress: 01 00 00 00\ncommand: 15 00 00 00\n"
	"#\n"
	"name: Mute\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 0D 00 00 00\n"
	"#\n"
	"name: Vol_up\ntype: parsed\nprotocol: SIRC\naddress: 01 00 00 00\ncommand: 12 00 00 00\n"
	"#\n"
	"name: Power\ntype: parsed\nprotocol: Samsung32\naddress: 07 07 00 00\ncommand: 02 00 00 00\n"
	"#\n";

static void write_fixture(const char *path, const char *body)
{
	FILE *f = fopen(path, "wb");
	fwrite(body, 1, strlen(body), f);
	fclose(f);
}

/* Callback that records the names it is handed, and can abort after N calls. */
typedef struct {
	int calls;
	int abort_after;   /* return false once calls == abort_after; 0 = never */
	char names[16][IR_UNIVERSAL_NAME_MAX_LEN];
} capture_t;

static bool capture_cb(void *ctx, const ir_universal_cmd_t *cmd, uint16_t idx)
{
	capture_t *c = (capture_t *)ctx;
	(void)idx;
	if (c->calls < 16)
		strncpy(c->names[c->calls], cmd->name, IR_UNIVERSAL_NAME_MAX_LEN - 1);
	c->calls++;
	if (c->abort_after && c->calls >= c->abort_after)
		return false;
	return true;
}

int main(void)
{
	printf("== universal-remote brute-force stream test ==\n");
	write_fixture(FIX_LIB, k_lib);

	/* Count pass (cb == NULL) counts only matching records. */
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Power", NULL, NULL), 3, "count Power");
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Vol_up", NULL, NULL), 2, "count Vol_up");
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Mute", NULL, NULL), 1, "count Mute");
	CHECK_EQ_INT(uremote_bf_stream(FIX_LIB, "Ch_next", NULL, NULL), 0, "count missing");

	/* Fire pass invokes cb only on matches, in order. */
	capture_t cap = {0};
	uint16_t n = uremote_bf_stream(FIX_LIB, "Power", capture_cb, &cap);
	CHECK_EQ_INT(n, 3, "fire Power returns 3");
	CHECK_EQ_INT(cap.calls, 3, "cb called 3x");
	CHECK(strcmp(cap.names[0], "Power") == 0, "match name 0");
	CHECK(strcmp(cap.names[2], "Power") == 0, "match name 2");

	/* Early abort: cb returns false after the 2nd call. */
	capture_t cap2 = {0};
	cap2.abort_after = 2;
	n = uremote_bf_stream(FIX_LIB, "Power", capture_cb, &cap2);
	CHECK_EQ_INT(cap2.calls, 2, "abort stops at 2 calls");
	CHECK_EQ_INT(n, 2, "abort returns 2");

	/* "IR signals file" header is also accepted. */
	char sig[2048];
	strcpy(sig, k_lib);
	memcpy(sig, "Filetype: IR signals file", 25); /* same length as library header */
	write_fixture("uremote_sig.ir", sig);
	CHECK_EQ_INT(uremote_bf_stream("uremote_sig.ir", "Power", NULL, NULL), 3,
	             "signals-file header accepted");

	remove(FIX_LIB);
	remove("uremote_sig.ir");

	printf("  %d checks, %d failures\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
```

Note: `"Filetype: IR library file"` and `"Filetype: IR signals file"` are both 25 chars before the value, so the `memcpy` swap keeps the fixture valid.

- [ ] **Step 2: Wire the test into run_tests.sh and run it (expect FAIL — module missing)**

In `tools/host_test/run_tests.sh`, before the final `exit`, append:

```sh
# Universal-remote brute-force streaming name-filter (Task 1).
UREMOTE_BIN="$DIR/test_uremote_bf"
"$CC" -std=c11 -Wall -Wextra -O0 -g \
	-I"$DIR" -I"$ROOT/m1_csrc" \
	"$DIR/test_uremote_bf.c" \
	"$DIR/ff_shim.c" \
	"$ROOT/m1_csrc/flipper_file.c" \
	"$ROOT/m1_csrc/m1_uremote_bf.c" \
	-o "$UREMOTE_BIN"
SCRATCH2="$(mktemp -d)"
( cd "$SCRATCH2" && "$UREMOTE_BIN" )
rc=$?
rm -rf "$SCRATCH2"
exit $rc
```

Also change the existing `test_subghz_rssi` block just above it so it no longer ends the script with `exit $?` — replace that block's final `exit $?` with a plain run:

```sh
"$SUBGHZ_BIN" || exit $?
```

Run: `sh tools/host_test/run_tests.sh`
Expected: FAIL — compile error, `m1_uremote_bf.h` / `m1_uremote_bf.c` do not exist yet.

- [ ] **Step 3: Create the header `m1_csrc/m1_uremote_bf.h`**

```c
/* See COPYING.txt for license details. */

/*
 * m1_uremote_bf.h
 *
 * Flipper-style universal-remote brute-force core.
 *
 * Owns the Flipper .ir signal-block parser (moved out of m1_ir_universal.c so
 * both callers share one copy and it is host-testable) and a streaming,
 * name-filtered iterator over an aggregated "IR library file" / "IR signals
 * file": for every record whose `name` matches a target function it can either
 * be counted (cb == NULL) or handed to a callback for transmission.
 *
 * M1 Project
 */

#ifndef M1_UREMOTE_BF_H_
#define M1_UREMOTE_BF_H_

#include <stdint.h>
#include <stdbool.h>
#include "m1_ir_universal.h"   /* ir_universal_cmd_t, IR_UNIVERSAL_NAME_MAX_LEN */
#include "flipper_file.h"

/* Map a Flipper protocol name to an IRMP protocol ID (0 = unknown). */
uint8_t uremote_map_flipper_protocol(const char *name);

/* Parse one signal block from a positioned flipper_file cursor.
 * Returns true and fills *cmd on a valid parsed or raw block. */
bool uremote_parse_signal_block(flipper_file_t *ff, ir_universal_cmd_t *cmd);

/* Callback invoked once per matching record. Return false to stop early. */
typedef bool (*uremote_bf_cb_t)(void *ctx, const ir_universal_cmd_t *cmd, uint16_t match_index);

/* Stream `path`, invoking `cb` for every record whose name == `record_name`.
 * If `cb` is NULL, records are only counted (no transmission). Accepts both
 * "IR library file" and "IR signals file" headers. O(1) memory.
 * Returns the number of matching records handled (or counted). */
uint16_t uremote_bf_stream(const char *path, const char *record_name,
                           uremote_bf_cb_t cb, void *ctx);

/* One selectable function on a category screen. */
typedef struct {
	const char *label;        /* shown in the list, e.g. "Vol +" */
	const char *record_name;  /* matched in the .ir file, e.g. "Vol_up" */
} uremote_function_t;

/* A device category (v1: TV only). */
typedef struct {
	const char               *menu_label;   /* e.g. "Universal TV" */
	const char               *ir_file_path; /* e.g. "0:/IR/Universal/tv.ir" */
	const uremote_function_t *functions;
	uint8_t                   function_count;
} uremote_category_t;

extern const uremote_category_t uremote_category_tv;

#endif /* M1_UREMOTE_BF_H_ */
```

- [ ] **Step 4: Create `m1_csrc/m1_uremote_bf.c`**

Move `map_flipper_protocol` and `parse_ir_signal_block` here **verbatim** from `m1_ir_universal.c` (renamed to the public names), add the iterator and TV table:

```c
/* See COPYING.txt for license details. */

/*
 * m1_uremote_bf.c  — see m1_uremote_bf.h
 *
 * M1 Project
 */

#include <string.h>
#include <stdlib.h>
#include "m1_uremote_bf.h"
#include "irmp.h"

/*============================================================================*/
/* Map a Flipper-format protocol name to an IRMP protocol ID (0 = unknown).   */
/* Must stay in sync with flipper_ir.c ir_proto_table[].                       */
/*============================================================================*/
uint8_t uremote_map_flipper_protocol(const char *name)
{
	if (strcmp(name, "NEC") == 0 || strcmp(name, "NECext") == 0)
		return IRMP_NEC_PROTOCOL;
	if (strcmp(name, "Samsung48") == 0)
		return IRMP_SAMSUNG48_PROTOCOL;
	if (strcmp(name, "Samsung32") == 0 || strcmp(name, "Samsung") == 0)
		return IRMP_SAMSUNG32_PROTOCOL;
	if (strcmp(name, "RC5") == 0 || strcmp(name, "RC5X") == 0)
		return IRMP_RC5_PROTOCOL;
	if (strcmp(name, "RC6") == 0)
		return IRMP_RC6_PROTOCOL;
	if (strcmp(name, "Sony12") == 0 || strcmp(name, "Sony15") == 0 || strcmp(name, "Sony20") == 0 ||
	    strcmp(name, "SIRC") == 0 || strcmp(name, "SIRC15") == 0 || strcmp(name, "SIRC20") == 0)
		return IRMP_SIRCS_PROTOCOL;
	if (strcmp(name, "Kaseikyo") == 0 || strcmp(name, "Panasonic") == 0)
		return IRMP_KASEIKYO_PROTOCOL;
	if (strcmp(name, "NEC42") == 0 || strcmp(name, "NEC42ext") == 0)
		return IRMP_NEC42_PROTOCOL;
	if (strcmp(name, "Denon") == 0 || strcmp(name, "Sharp") == 0)
		return IRMP_DENON_PROTOCOL;
	if (strcmp(name, "JVC") == 0)
		return IRMP_JVC_PROTOCOL;
	if (strcmp(name, "LG") == 0)
		return IRMP_LGAIR_PROTOCOL;
	if (strcmp(name, "Pioneer") == 0)
		return IRMP_NEC_PROTOCOL;
	if (strcmp(name, "Apple") == 0)
		return IRMP_APPLE_PROTOCOL;
	if (strcmp(name, "Bose") == 0)
		return IRMP_BOSE_PROTOCOL;
	if (strcmp(name, "Nokia") == 0)
		return IRMP_NOKIA_PROTOCOL;
	if (strcmp(name, "RCA") == 0)
		return IRMP_RCCAR_PROTOCOL;
	if (strcmp(name, "RCMM") == 0)
		return IRMP_RCMM32_PROTOCOL;
	return 0;
}

/*============================================================================*/
/* Parse a single IR signal block from a positioned flipper_file cursor.      */
/*============================================================================*/
bool uremote_parse_signal_block(flipper_file_t *ff, ir_universal_cmd_t *cmd)
{
	bool got_name = false;
	bool got_type = false;
	bool is_parsed = false;
	bool is_raw_type = false;

	memset(cmd, 0, sizeof(ir_universal_cmd_t));

	while (ff_read_line(ff))
	{
		if (ff_is_separator(ff))
			break;

		if (!ff_parse_kv(ff))
			continue;

		if (strcmp(ff_get_key(ff), "name") == 0)
		{
			strncpy(cmd->name, ff_get_value(ff), IR_UNIVERSAL_NAME_MAX_LEN - 1);
			cmd->name[IR_UNIVERSAL_NAME_MAX_LEN - 1] = '\0';
			got_name = true;
		}
		else if (strcmp(ff_get_key(ff), "type") == 0)
		{
			got_type = true;
			if (strcmp(ff_get_value(ff), "parsed") == 0)
			{
				is_parsed = true;
				is_raw_type = false;
			}
			else if (strcmp(ff_get_value(ff), "raw") == 0)
			{
				is_parsed = false;
				is_raw_type = true;
			}
		}
		else if (strcmp(ff_get_key(ff), "protocol") == 0 && is_parsed)
		{
			cmd->protocol = uremote_map_flipper_protocol(ff_get_value(ff));
		}
		else if (strcmp(ff_get_key(ff), "address") == 0 && is_parsed)
		{
			uint8_t hex_buf[4];
			uint8_t n = ff_parse_hex_bytes(ff_get_value(ff), hex_buf, 4);
			cmd->address = (n >= 2) ? (uint16_t)(hex_buf[0] | ((uint16_t)hex_buf[1] << 8))
			                        : (uint16_t)hex_buf[0];
		}
		else if (strcmp(ff_get_key(ff), "command") == 0 && is_parsed)
		{
			uint8_t hex_buf[4];
			uint8_t n = ff_parse_hex_bytes(ff_get_value(ff), hex_buf, 4);
			cmd->command = (n >= 2) ? (uint16_t)(hex_buf[0] | ((uint16_t)hex_buf[1] << 8))
			                        : (uint16_t)hex_buf[0];
		}
		else if (strcmp(ff_get_key(ff), "frequency") == 0 && is_raw_type)
		{
			cmd->raw_freq = (uint32_t)strtoul(ff_get_value(ff), NULL, 10);
		}
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
				count++;
				while (*p && *p != ' ')
					p++;
			}
			cmd->raw_count = count;
		}
	}

	if (got_name && got_type)
	{
		if (is_parsed && cmd->protocol != 0)
		{
			cmd->is_raw = false;
			cmd->flags = 0;
			cmd->valid = true;
			return true;
		}
		else if (is_raw_type && cmd->raw_freq > 0)
		{
			cmd->is_raw = true;
			cmd->valid = true;
			return true;
		}
	}
	return false;
}

/*============================================================================*/
/* Stream a library/signals file, handling every record named `record_name`.  */
/*============================================================================*/
uint16_t uremote_bf_stream(const char *path, const char *record_name,
                           uremote_bf_cb_t cb, void *ctx)
{
	flipper_file_t ff;
	ir_universal_cmd_t cmd;
	uint16_t matched = 0;

	if (path == NULL || record_name == NULL)
		return 0;

	if (!ff_open(&ff, path))
		return 0;

	/* Accept either the aggregated library header or a normal signals file. */
	if (!ff_validate_header(&ff, "IR library file", 1))
	{
		ff_close(&ff);
		if (!ff_open(&ff, path))
			return 0;
		if (!ff_validate_header(&ff, "IR signals file", 1))
		{
			ff_close(&ff);
			return 0;
		}
	}

	while (!ff.eof)
	{
		if (!uremote_parse_signal_block(&ff, &cmd))
		{
			if (ff.eof)
				break;
			continue;   /* skip invalid / unknown-protocol block */
		}
		if (strcmp(cmd.name, record_name) != 0)
			continue;

		if (cb != NULL)
		{
			bool go_on = cb(ctx, &cmd, matched);
			matched++;
			if (!go_on)
				break;
		}
		else
		{
			matched++;
		}
	}

	ff_close(&ff);
	return matched;
}

/*============================================================================*/
/* TV category table (v1).                                                    */
/*============================================================================*/
static const uremote_function_t s_tv_functions[] = {
	{ "Power",  "Power"   },
	{ "Vol +",  "Vol_up"  },
	{ "Vol -",  "Vol_dn"  },
	{ "Ch +",   "Ch_next" },
	{ "Ch -",   "Ch_prev" },
	{ "Mute",   "Mute"    },
};

const uremote_category_t uremote_category_tv = {
	.menu_label     = "Universal TV",
	.ir_file_path   = IR_UNIVERSAL_IRDB_ROOT "/Universal/tv.ir",
	.functions      = s_tv_functions,
	.function_count = (uint8_t)(sizeof(s_tv_functions) / sizeof(s_tv_functions[0])),
};
```

- [ ] **Step 5: Run the host suite (expect PASS)**

Run: `sh tools/host_test/run_tests.sh`
Expected: all prior tests still pass, and `universal-remote brute-force stream test` prints `N checks, 0 failures`.

- [ ] **Step 6: Commit**

```bash
git add m1_csrc/m1_uremote_bf.h m1_csrc/m1_uremote_bf.c \
        tools/host_test/test_uremote_bf.c tools/host_test/run_tests.sh
git commit -m "uremote: streaming name-filtered brute-force core + host test"
```

---

## Task 2: De-duplicate m1_ir_universal.c onto the shared parser

**Files:**
- Modify: `m1_csrc/m1_ir_universal.c`

- [ ] **Step 1: Replace the two static helpers with the shared module**

In `m1_csrc/m1_ir_universal.c`:

1. Add near the other includes: `#include "m1_uremote_bf.h"`.
2. Delete the entire `static uint8_t map_flipper_protocol(...)` function body and the whole `static bool parse_ir_signal_block(...)` function body.
3. Remove their two forward declarations from the prototype block:
   `static bool parse_ir_signal_block(flipper_file_t *ff, ir_universal_cmd_t *cmd);`
   `static uint8_t map_flipper_protocol(const char *name);`
4. In `parse_ir_file()`, change the call `parse_ir_signal_block(&ff, &s_commands[count])` to `uremote_parse_signal_block(&ff, &s_commands[count])`.
5. Search the file for any other `parse_ir_signal_block(` or `map_flipper_protocol(` call and repoint to the `uremote_` names. (Expected: only the one in `parse_ir_file`.)

- [ ] **Step 2: Build the firmware (Windows toolchain; skip on Linux host)**

Run the canonical build (see `do_build.ps1` / CLAUDE.md). Expected: compiles clean; `m1_uremote_bf.c` is picked up by the existing glob or must be added to the source list if the build enumerates files explicitly — verify `m1_uremote_bf.c` is in the build.

> On this Linux host the firmware cannot be built; record "build deferred to Windows toolchain" and rely on Step 3 for a Linux-verifiable check that the shared parser is intact.

- [ ] **Step 3: Run the host suite (regression on the shared parser)**

Run: `sh tools/host_test/run_tests.sh`
Expected: `test_flipper_ir` and `test_uremote_bf` both green (proves the moved parser still round-trips).

- [ ] **Step 4: Commit**

```bash
git add m1_csrc/m1_ir_universal.c
git commit -m "uremote: reuse shared block parser, drop duplicated statics"
```

---

## Task 3: Bundle the aggregated TV data

**Files:**
- Create: `ir_database/Universal/tv.ir`

- [ ] **Step 1: Copy Flipper's tv.ir into the repo data tree**

Run:
```bash
mkdir -p ir_database/Universal
cp ../../flipper_firmwares/Momentum-Firmware/applications/main/infrared/resources/infrared/assets/tv.ir \
   ir_database/Universal/tv.ir
```

- [ ] **Step 2: Verify header and function coverage**

Run:
```bash
head -2 ir_database/Universal/tv.ir
grep -c '^name:' ir_database/Universal/tv.ir
grep '^name:' ir_database/Universal/tv.ir | sort | uniq -c
```
Expected: header `Filetype: IR library file` / `Version: 1`; 815 total; the six functions `Power`(324) `Vol_up`(99) `Vol_dn`(104) `Ch_next`(94) `Ch_prev`(101) `Mute`(93).

- [ ] **Step 3: Commit**

```bash
git add ir_database/Universal/tv.ir
git commit -m "uremote: bundle aggregated TV universal IR library (tv.ir)"
```

---

## Task 4: Function-list + brute-force screens + dashboard entry

**Files:**
- Modify: `m1_csrc/m1_ir_universal.c`

- [ ] **Step 1: Add the dashboard item**

In `m1_ir_universal.c`:

1. Change `#define DASHBOARD_ITEM_COUNT 7` to `8`.
2. Append `"Universal TV"` as the last element of `s_dashboard_items[]`:

```c
static const char *s_dashboard_items[DASHBOARD_ITEM_COUNT] = {
	"Browse IRDB",
	"Learned",
	"Favorites",
	"Recent",
	"Remote Mode",
	"Power Off TVs",
	"Power Off A/V",
	"Universal TV"
};
```

3. Add forward declarations near the other prototypes:

```c
static void uremote_tv_screen(void);
static void uremote_bf_run(const char *file_path, const uremote_function_t *fn);
static bool uremote_fire_cmd(const ir_universal_cmd_t *cmd);
```

4. In `dashboard_screen()`'s OK-press `switch (selection)`, add before `default:`:

```c
					case 7: /* Universal TV — Flipper-style brute-force remote */
						uremote_tv_screen();
						break;
```

- [ ] **Step 2: Implement the function-list screen**

Add this function to `m1_ir_universal.c` (reuses `draw_list_screen` by loading the function labels into `s_browse_names`):

```c
/*============================================================================*/
/*
 * Flipper-style universal TV screen: list the fixed function buttons
 * (Power / Vol / Ch / Mute); OK brute-forces the selected function across
 * every brand in tv.ir; BACK returns to the dashboard.
 */
/*============================================================================*/
static void uremote_tv_screen(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	const uremote_category_t *cat = &uremote_category_tv;
	uint16_t selection = 0;
	uint8_t i;

	/* Load function labels into the shared list-name buffer. */
	for (i = 0; i < cat->function_count && i < BROWSE_NAMES_MAX; i++)
	{
		strncpy(s_browse_names[i], cat->functions[i].label, BROWSE_NAME_MAX_LEN - 1);
		s_browse_names[i][BROWSE_NAME_MAX_LEN - 1] = '\0';
	}
	s_browse_count = cat->function_count;

	draw_list_screen(cat->menu_label, s_browse_count, selection);

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD)
			continue;

		ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
		{
			xQueueReset(main_q_hdl);
			return;
		}
		else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection > 0) ? (selection - 1) : (s_browse_count - 1);
		}
		else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection < s_browse_count - 1) ? (selection + 1) : 0;
		}
		else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (selection < cat->function_count)
				uremote_bf_run(cat->ir_file_path, &cat->functions[selection]);
		}

		draw_list_screen(cat->menu_label, s_browse_count, selection);
	}
}
```

- [ ] **Step 3: Implement the fire helper and the brute-force run loop**

`uremote_fire_cmd()` factors the parsed/raw encode+transmit out of `transmit_command()` **without** drawing a per-code screen. The brute-force loop owns the progress UI, the BACK-abort poll, and the pacing delay. Add:

```c
/*============================================================================*/
/*
 * Encode and transmit one universal-remote command. No per-code screen — the
 * brute-force loop owns the display. Mirrors transmit_command()'s TX path.
 * Returns true if a frame was actually kicked off.
 */
/*============================================================================*/
static bool uremote_fire_cmd(const ir_universal_cmd_t *cmd)
{
	if (cmd == NULL || !cmd->valid)
		return false;

	if (cmd->is_raw)
	{
		transmit_raw_command(cmd);   /* uses s_raw_tx_filepath, set by caller */
		return true;
	}

	if (cmd->protocol == IRMP_UNKNOWN_PROTOCOL || cmd->protocol == 0)
		return false;

	s_tx_irmp_data.protocol = cmd->protocol;
	s_tx_irmp_data.address  = cmd->address;
	s_tx_irmp_data.command  = cmd->command;
	s_tx_irmp_data.flags    = cmd->flags;

	infrared_encode_sys_init();
	irsnd_generate_tx_data(s_tx_irmp_data);
	infrared_transmit(1);
	infrared_transmit(0);
	return true;
}

/* Shared state for the brute-force callback (progress + abort). */
typedef struct {
	const char *label;
	uint16_t    total;
	uint16_t    sent;
	bool        aborted;
} uremote_bf_ctx_t;

/*============================================================================*/
/*
 * Brute-force callback: fire one code, draw progress "<label>  n/total",
 * pace ~200 ms, and stop the sweep on a BACK press.
 */
/*============================================================================*/
static bool uremote_bf_fire_cb(void *vctx, const ir_universal_cmd_t *cmd, uint16_t match_index)
{
	uremote_bf_ctx_t *c = (uremote_bf_ctx_t *)vctx;
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	char line[28];

	(void)match_index;

	/* Non-blocking BACK/LEFT abort check. */
	if (xQueueReceive(main_q_hdl, &q_item, 0) == pdTRUE &&
	    q_item.q_evt_type == Q_EVENT_KEYPAD)
	{
		xQueueReceive(button_events_q_hdl, &this_button_status, 0);
		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
		    this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
		{
			c->aborted = true;
			return false;
		}
	}

	/* Progress screen. */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 12, 16, c->label);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	snprintf(line, sizeof(line), "Sending %u/%u",
	         (unsigned)(c->sent + 1), (unsigned)c->total);
	u8g2_DrawStr(&m1_u8g2, 6, 34, line);
	u8g2_DrawStr(&m1_u8g2, 6, 60, "BACK to stop");
	m1_u8g2_nextpage();

	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);

	if (uremote_fire_cmd(cmd))
		c->sent++;

	vTaskDelay(pdMS_TO_TICKS(220));   /* let the frame finish before the next code */
	return true;
}

/*============================================================================*/
/*
 * Brute-force one function across every brand in `file_path`: pre-count the
 * matching records, then stream + fire them with progress. BACK stops.
 */
/*============================================================================*/
static void uremote_bf_run(const char *file_path, const uremote_function_t *fn)
{
	uremote_bf_ctx_t c;
	char line[28];

	/* Point raw TX (if any record is raw) at this library file. */
	strncpy(s_raw_tx_filepath, file_path, IR_UNIVERSAL_PATH_MAX_LEN - 1);
	s_raw_tx_filepath[IR_UNIVERSAL_PATH_MAX_LEN - 1] = '\0';

	c.label   = fn->label;
	c.total   = uremote_bf_stream(file_path, fn->record_name, NULL, NULL);
	c.sent    = 0;
	c.aborted = false;

	if (c.total == 0)
	{
		u8g2_FirstPage(&m1_u8g2);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
		u8g2_DrawStr(&m1_u8g2, 12, 18, "No codes");
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 4, 36, "Copy tv.ir to");
		u8g2_DrawStr(&m1_u8g2, 4, 48, "0:/IR/Universal/");
		m1_u8g2_nextpage();
		vTaskDelay(pdMS_TO_TICKS(1800));
		xQueueReset(main_q_hdl);
		return;
	}

	uremote_bf_stream(file_path, fn->record_name, uremote_bf_fire_cb, &c);

	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
	infrared_encode_sys_deinit();

	/* Result screen. */
	u8g2_FirstPage(&m1_u8g2);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 18, 28, c.aborted ? "Stopped" : "Done");
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	snprintf(line, sizeof(line), "%u codes sent", (unsigned)c.sent);
	u8g2_DrawStr(&m1_u8g2, 18, 44, line);
	m1_u8g2_nextpage();
	m1_buzzer_notification();
	vTaskDelay(pdMS_TO_TICKS(1200));

	xQueueReset(main_q_hdl);
}
```

- [ ] **Step 4: Build, flash, bench (Windows toolchain / owner)**

Build (canonical command from `do_build.ps1`), flash, then on-device:
1. Open IR → Universal Remote; the dashboard shows a new `Universal TV` item.
2. Select it → the six functions list.
3. Select `Power` → progress counts `Sending n/324`; a real TV powers off mid-sweep; **BACK stops immediately**; result screen shows `Stopped` / `Done`.

> Cannot be run on this Linux host — real IR emission is owner bench. If nothing is emitted, suspect the orthogonal TIM1/IR-silence issue, not this feature.

- [ ] **Step 5: Commit**

```bash
git add m1_csrc/m1_ir_universal.c
git commit -m "uremote: Universal TV dashboard item + brute-force screen"
```

---

## Task 5: Documentation

**Files:**
- Create: `documentation/universal_remotes.md`

- [ ] **Step 1: Write the feature doc + bench checklist**

Create `documentation/universal_remotes.md`:

```markdown
# Universal Remotes (Flipper-style, TV v1)

IR → Universal Remote → **Universal TV** runs a Flipper-style brute-force:
pick a function (Power / Vol +/− / Ch +/− / Mute) and the M1 auto-fires every
brand's version of that function from `0:/IR/Universal/tv.ir`, ~200 ms apart,
until the target device reacts. **BACK** stops the sweep.

## Data
- `0:/IR/Universal/tv.ir` — aggregated `IR library file` (815 signals; functions
  Power, Vol_up, Vol_dn, Ch_next, Ch_prev, Mute). Bundled from
  `ir_database/Universal/tv.ir`.

## Code
- `m1_csrc/m1_uremote_bf.{c,h}` — shared Flipper `.ir` block parser + the
  streaming, name-filtered brute-force iterator (`uremote_bf_stream`) + the TV
  category table. Host-tested by `tools/host_test/test_uremote_bf.c`.
- `m1_csrc/m1_ir_universal.c` — `Universal TV` dashboard item, function-list
  screen, and the brute-force progress screen.

## Add another category later
1. Bundle its aggregated file at `ir_database/Universal/<cat>.ir`.
2. Add a `uremote_function_t[]` + `uremote_category_t` in `m1_uremote_bf.c`
   (export it in the header).
3. Add a dashboard item + `switch` case that calls a screen pointed at the new
   category. No new parsing/TX code.

## Bench checklist (owner)
- [ ] `Universal TV` appears on the Universal Remote dashboard.
- [ ] Function list shows all six buttons.
- [ ] `Power` sweeps `n/324`; a real TV powers off; BACK stops immediately.
- [ ] Vol/Ch/Mute each fire their function only (progress total matches the
      per-function counts).
- [ ] If nothing emits, check the TIM1/IR-silence issue (orthogonal).
```

- [ ] **Step 2: Commit**

```bash
git add documentation/universal_remotes.md
git commit -m "docs: universal remotes (Flipper-style TV brute-force)"
```

---

## Self-review notes

- **Spec coverage:** data bundling (T3), streaming brute-force accepting the library header (T1), TV function list + auto-fire/BACK-stop screen (T4), dashboard-alongside integration (T4 Step 1), table-driven extensibility (T1 table + T5 doc), host testing (T1), reuse of existing parser/TX/list renderers (T2/T4). All present.
- **Types consistent:** `uremote_bf_stream`, `uremote_bf_cb_t` (`ctx, cmd, match_index`), `uremote_parse_signal_block`, `uremote_map_flipper_protocol`, `uremote_category_t`/`uremote_function_t` (`label`/`record_name`), `uremote_category_tv`, `uremote_bf_run`, `uremote_fire_cmd` — all used consistently across tasks.
- **No placeholders:** every code/step is concrete.
- **Cross-task dependency:** T2 removes the statics T1 promoted; the host suite in T1/T2 covers the parser move; firmware build is Windows-only and called out at each firmware task.
```
