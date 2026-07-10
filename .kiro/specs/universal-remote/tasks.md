# Implementation Plan: Universal Remote

## Overview

The four core modules (`flipper_ir.c`, `m1_ir_universal.c`, `m1_ir_custom.c`,
`m1_infrared.c`) already exist and carry substantial implementations. This task list
closes the remaining gaps identified by comparing the requirements and design against
the live source code. Work is organized in four groups:

1. **Parser / protocol layer** — close remaining correctness gaps in `flipper_ir.c`
   (corrupt-append guard, header validation hardening, `flipper_ir_open_append`
   semantics check) and extend the host-side property test harness.
2. **Universal Remote UI** — fix the Remote Mode SOUTHPAW preservation bug,
   wire the missing "Add to Favorites" action, verify IRDB root-escape guard
   in `path_go_up`, add watchdog feed to the Power Blast loop, verify
   `xQueueReset` discipline, and extend host tests.
3. **Custom Remote UI** — close the `flipper_ir_open_append`-on-corrupt-header
   guard test; verify button-count invariant and confirm `ir_custom_run` queue
   discipline on every exit path.
4. **IR Core (learn & TX hardware)** — confirm the `infrared_learn_new_remote`
   "Save failed" / "Check SD card" timing and the raw signal `irsnd_set_carrier_freq`
   restore path; add smoke-test build verification after every code change.

All code is C11, stack-only, no heap.  Every code change must be followed by
`./build.sh` (or the equivalent cmake invocation) with a clean exit.

---

## Tasks

- [ ] 1. Harden `flipper_ir.c` parser correctness gaps
  - [ ] 1.1 Guard `flipper_ir_open_append()` against corrupt header
    - `ff_open_append` currently does NOT validate the header before positioning
      at EOF; a corrupt file would silently receive appended data.
    - Modify `flipper_ir_open_append()` in `m1_csrc/flipper_ir.c`: open the file
      for reading first, call `ff_validate_header()`, close, then reopen for append.
      Return `false` immediately if validation fails.
    - Add a test case in `tools/host_test/test_flipper_ir.c`:
      write a file with an invalid `Filetype:` line, then call
      `flipper_ir_open_append()` and assert it returns `false`.
    - Build; run `tools/host_test/run_tests.sh`.
    - _Requirements: 16.3_

  - [ ]* 1.2 Write property test — parsed and raw signal write/read round-trip (Property 1)
    - Extend `tools/host_test/test_flipper_ir.c` with a property driver that
      generates ≥ 100 random `flipper_ir_signal_t` values (both parsed and raw
      types) via a seeded PRNG, writes each to a temp `.ir` file via
      `flipper_ir_write_signal()`, reads it back via `flipper_ir_read_signal()`,
      and asserts field-for-field equality.
    - Tag: `// Feature: universal-remote, Property 1: parsed and raw signal write/read round-trip`
    - **Property 1: Parsed and raw signal write/read round-trip**
    - **Validates: Requirements 1.1, 1.2, 1.5, 6.4**

  - [ ]* 1.3 Write property test — protocol name ↔ IRMP ID round-trip (Properties 2, 3, 4)
    - Add a property driver in `tools/host_test/test_flipper_ir.c` (or a
      dedicated `test_ir_proto_map.c`) that iterates all 24 known Flipper protocol
      names and asserts `flipper_ir_proto_to_irmp(n) != IRMP_UNKNOWN_PROTOCOL`
      (Property 2), and that `flipper_ir_irmp_to_proto(flipper_ir_proto_to_irmp(n))`
      does not return `"Unknown"` (Property 4).
    - Also generate ≥ 100 random strings not in the table and assert each returns
      `IRMP_UNKNOWN_PROTOCOL` (Property 3).
    - Tag: `// Feature: universal-remote, Property 2/3/4: protocol mapping`
    - **Property 2: Protocol name to IRMP ID mapping completeness**
    - **Property 3: Unknown protocol returns IRMP_UNKNOWN_PROTOCOL**
    - **Property 4: Protocol round-trip does not return "Unknown"**
    - **Validates: Requirements 2.1, 2.2, 2.3, 2.4**

  - [ ]* 1.4 Write property test — raw accumulator frame-complete / noise threshold (Property 5)
    - Add a property driver that generates random N (0 … 520) edge events and a
      random `min_samples` (1 … 16), feeds them into `flipper_ir_raw_feed()` with
      `frame_end = true` at the end, and asserts:
      - `N >= min_samples` → result `FLIPPER_IR_RAW_FRAME_COMPLETE`, `sig.valid == true`
      - `N < min_samples` → result `FLIPPER_IR_RAW_FRAME_NOISE`, `sig.valid == false`,
        `sig.raw.sample_count == 0`
    - ≥ 100 iterations.
    - Tag: `// Feature: universal-remote, Property 5: raw accumulator threshold`
    - **Property 5: Raw accumulator frame-complete/noise threshold**
    - **Validates: Requirements 6.2, 6.3**

  - [ ]* 1.5 Write property test — raw buffer overflow (Property 6)
    - Extend `tools/host_test/test_flipper_ir.c`: pre-fill a signal to exactly
      `FLIPPER_IR_RAW_MAX_SAMPLES` (512) edges, then attempt to add one more edge
      and assert `flipper_ir_raw_add_edge()` returns `false` and
      `sig.raw.sample_count` remains 512.
    - Tag: `// Feature: universal-remote, Property 6: raw buffer overflow`
    - **Property 6: Raw sample buffer overflow returns EDGE_DROPPED without corruption**
    - **Validates: Requirement 6.5**

- [ ] 2. Checkpoint — parser layer complete
  - Run `tools/host_test/run_tests.sh`; all tests pass.
  - `./build.sh` exits 0.

- [ ] 3. Fix Remote Mode SOUTHPAW preservation in `m1_ir_universal.c`
  - [ ] 3.1 Capture pre-entry orientation before entering `ir_universal_run()`
    - In `m1_csrc/m1_ir_universal.c`, add a module-static variable
      `static uint8_t s_pre_entry_orient;` to `ir_universal_init()`, storing
      `m1_screen_orientation` at init time.
    - In `dashboard_screen()`, when BACK is pressed and the orientation is
      `M1_ORIENT_REMOTE`, restore to `s_pre_entry_orient` instead of
      unconditionally restoring to `M1_ORIENT_NORMAL`:
      - If `s_pre_entry_orient == M1_ORIENT_SOUTHPAW`: set
        `m1_screen_orientation = M1_ORIENT_SOUTHPAW`,
        `m1_southpaw_mode = 1`, `u8g2_SetDisplayRotation(U8G2_R0)`.
      - Otherwise: set `M1_ORIENT_NORMAL`, `m1_southpaw_mode = 0`, `U8G2_R2`.
    - Apply the same restore logic to the "Normal Mode" toggle (case 4 in
      `dashboard_screen`): selecting "Normal Mode" when already `M1_ORIENT_NORMAL`
      or `M1_ORIENT_SOUTHPAW` should be a no-op (idempotent).
    - Build.
    - _Requirements: 10.2, 10.3, 10.5_

  - [ ]* 3.2 Write property test — Remote Mode orientation round-trip (Property 9)
    - Add a host-testable pure function
      `ir_orientation_restore(uint8_t pre_entry, uint8_t current)`
      (or adapt the existing logic into a testable helper) in a new file
      `tools/host_test/test_ir_orient.c`.
    - Property driver: for each initial orientation `{NORMAL, SOUTHPAW}`, simulate
      toggling to `REMOTE` then toggling back; assert the final orientation equals
      the initial value.
    - ≥ 100 iterations (vary the intermediate sequence of toggles).
    - Tag: `// Feature: universal-remote, Property 9: orientation round-trip`
    - **Property 9: Remote Mode orientation round-trip (idempotence)**
    - **Validates: Requirements 10.4, 10.5**

- [ ] 4. Fix IRDB path-escape guard in `path_go_up()` and wire IRDB root invariant
  - [ ] 4.1 Add IRDB root boundary check to `path_go_up()`
    - In `m1_csrc/m1_ir_universal.c`, modify `path_go_up()`: after computing the
      new path (removing the last path component), check whether the result is
      still prefixed by `IR_UNIVERSAL_IRDB_ROOT` ("0:/IR"). If the resulting path
      would be shorter than or equal to `strlen(IR_UNIVERSAL_IRDB_ROOT)`, clamp the
      path to `IR_UNIVERSAL_IRDB_ROOT` (no-op at root).
    - Also verify `browse_directory()`: when BACK is pressed and
      `strcmp(s_current_path, root_arg) == 0` (we are at the initial browse root),
      return rather than calling `path_go_up()`.
    - Build.
    - _Requirements: 7.3, 7.4, 7.7_

  - [ ]* 4.2 Write property test — IRDB path invariant (Property 14)
    - Add a host-testable pure path helper in `tools/host_test/test_ir_paths.c`
      (compile only against `m1_ir_universal.c`'s path helpers via a shim, or
      extract pure path logic into a testable header).
    - Property driver: generate random sequences of ≤ 20 descend/ascend operations
      starting from `IR_UNIVERSAL_IRDB_ROOT`; after each operation assert
      `strncmp(path, IR_UNIVERSAL_IRDB_ROOT, strlen(IR_UNIVERSAL_IRDB_ROOT)) == 0`.
    - ≥ 100 random sequences.
    - Tag: `// Feature: universal-remote, Property 14: IRDB path invariant`
    - **Property 14: IRDB browser path is always below or equal to root**
    - **Validates: Requirement 7.7**

- [ ] 5. Add "Add to Favorites" action in the command-list view
  - [ ] 5.1 Wire "Add to Favorites" button action in `show_commands()`
    - In `m1_csrc/m1_ir_universal.c`, modify `show_commands()` to handle the
      LEFT button (or an additional OK-hold / dedicated key — match the design's
      bottom bar slot) to call a new `add_to_favorites(s_raw_tx_filepath)`:
      - Add `static void add_to_favorites(const char *path)` that mirrors
        `add_to_recent()` but writes to `s_favorites[]` / `s_favorite_count`.
      - Enforce the cap of `IR_UNIVERSAL_MAX_FAVORITES` (10): evict the oldest
        entry (index 9) when the list is full before prepending.
      - Show a brief on-screen confirmation ("Added to Favorites") for 800 ms.
    - Update `draw_list_screen()`'s bottom bar to show a "Fav" hint.
    - Build.
    - _Requirements: 11.1, 11.3, 11.4_

  - [ ]* 5.2 Write property test — Favorites and Recent count invariants (Property 7)
    - Add a host-testable driver in `tools/host_test/test_ir_lists.c` that
      simulates random sequences of ≤ 50 `add_to_favorites()` and `add_to_recent()`
      calls and asserts:
      - `s_favorite_count <= IR_UNIVERSAL_MAX_FAVORITES` after every add.
      - `s_recent_count <= IR_UNIVERSAL_MAX_RECENT` after every add.
    - ≥ 100 random sequences.
    - Tag: `// Feature: universal-remote, Property 7: list count invariants`
    - **Property 7: Favorites and Recent list count invariants**
    - **Validates: Requirements 11.3, 12.2**

  - [ ]* 5.3 Write property test — Recent deduplication invariant (Property 8)
    - Extend `tools/host_test/test_ir_lists.c`: seed the Recent list with up to
      10 random paths, then call `add_to_recent()` with a path already in the list
      and assert: count is unchanged, the path appears at index 0.
    - ≥ 100 iterations.
    - Tag: `// Feature: universal-remote, Property 8: recent deduplication`
    - **Property 8: Recent list deduplication invariant**
    - **Validates: Requirement 12.3**

- [ ] 6. Harden Power Blast — watchdog feed and missing-file message
  - [ ] 6.1 Add `m1_watchdog_feed()` between signals in `ir_power_blast()`
    - In `m1_csrc/m1_ir_universal.c`, in `ir_power_blast()`, call
      `m1_watchdog_feed()` in the per-signal loop immediately after
      `vTaskDelay(pdMS_TO_TICKS(220))`.
    - When `parse_ir_file(paths[fi])` returns 0 for a given path (file absent or
      unreadable), display "File not found" for 1 000 ms before moving on to the
      next file in the sequence; do not stop the blast.
    - Build.
    - _Requirements: 9.4, 9.5, 18.2_

  - [ ] 6.2 Verify `xQueueReset()` is called on all exit paths in `ir_power_blast()`
    - Audit `ir_power_blast()`: confirm `xQueueReset(main_q_hdl)` is called
      before every `return` (abort and normal completion). Add any missing calls.
    - Build.
    - _Requirements: 18.4_

- [ ] 7. Checkpoint — Universal Remote UI hardening complete
  - Run `tools/host_test/run_tests.sh`; all tests pass.
  - `./build.sh` exits 0.
  - Manually verify: enter Universal Remote, toggle Remote Mode, exit — display
    returns to correct orientation (NORMAL or SOUTHPAW as pre-entry state).

- [ ] 8. Harden `m1_ir_custom.c` — button-count invariant and queue discipline
  - [ ] 8.1 Verify button-count invariant after `ir_custom_append_parsed/raw()`
    - In `m1_csrc/m1_ir_custom.c`, after each successful append in
      `ir_custom_learn_button()` (both the `IR_CAP_PARSED` and `IR_CAP_RAW`
      branches), assert (in debug mode via `assert_param`) that
      `flipper_ir_count_signals(path)` equals `before_count + 1`.
    - Store `before_count = flipper_ir_count_signals(path)` before the save
      prompt, and check after successful `ir_custom_append_*()`.
    - Build.
    - _Requirements: 14.5_

  - [ ]* 8.2 Write property test — name sanitization idempotence (Property 10)
    - Add a host-testable driver in `tools/host_test/test_ir_sanitize.c` that
      calls `ir_custom_sanitize_name()` twice on random strings (including
      strings with control characters, `/`, `\`, FAT-reserved chars, trailing
      spaces/dots) and asserts the second call produces the same result as the
      first.
    - Also assert the result contains no control chars (< 0x20), no path
      separators, no FAT-reserved chars, and no trailing space or dot.
    - ≥ 100 random strings per iteration; ≥ 100 iterations.
    - Note: `ir_custom_sanitize_name()` is currently `static`; expose it via a
      dedicated `#ifdef M1_HOST_TEST` declaration in `m1_ir_custom.c` or extract
      the pure logic to a header.
    - Tag: `// Feature: universal-remote, Property 10: sanitize idempotence`
    - **Property 10: Name sanitization idempotence**
    - **Validates: Requirements 13.2, 13.3**

  - [ ] 8.3 Verify `xQueueReset()` called on all exit paths in `ir_custom_run()`
    - Audit `ir_custom_run()`, `ir_custom_learn_button()`, `ir_custom_edit_buttons()`,
      and `ir_custom_open_remote()`: confirm `xQueueReset(main_q_hdl)` is called
      before every `break` / `return`. Add any missing calls.
    - Build.
    - _Requirements: 18.4_

- [ ] 9. Extend host property tests — scroll window, parse-count cap, create-then-open,
         rename and delete metamorphic tests
  - [ ]* 9.1 Write property test — scroll window invariant (Property 15)
    - Add a pure `scroll_start_idx(sel, count)` function (extracted from
      `draw_list_screen()`'s inline formula) in `m1_csrc/m1_ir_universal.c`
      under `#ifdef M1_HOST_TEST`, then write a host driver in
      `tools/host_test/test_ir_scroll.c`:
      for random `sel` in `[0, N)` and `N` in `[1, 256]`, assert
      `start_idx <= sel && sel < start_idx + LIST_VISIBLE_ITEMS`.
    - ≥ 100 random `(sel, N)` pairs.
    - Tag: `// Feature: universal-remote, Property 15: scroll window invariant`
    - **Property 15: Scroll window invariant**
    - **Validates: Requirement 17.3**

  - [ ]* 9.2 Write property test — parse-count cap at `IR_UNIVERSAL_MAX_CMDS` (Property 11)
    - In `tools/host_test/test_flipper_ir.c`, add a test that writes a `.ir` file
      with N > 64 signals (use a loop writing 80 parsed signals), then calls
      `parse_ir_file()` (or the equivalent `flipper_ir_count_signals()` plus a
      direct parse loop capped at `IR_UNIVERSAL_MAX_CMDS`) and asserts the
      returned count is exactly 64.
    - ≥ 10 iterations varying N from 65 to 80.
    - Tag: `// Feature: universal-remote, Property 11: parse-count cap`
    - **Property 11: Parse-count cap at IR_UNIVERSAL_MAX_CMDS**
    - **Validates: Requirements 8.4, 8.5**

  - [ ]* 9.3 Write property test — rename metamorphic (Property 12)
    - In `tools/host_test/test_flipper_ir.c`, add a property driver that creates
      `.ir` files with 1..64 randomly generated signals, picks a random rename
      target index, calls `flipper_ir_rename_signal()`, reads back all signals,
      and asserts: target has `new_name`; all others have unchanged name, type,
      and payload.
    - ≥ 100 iterations.
    - Tag: `// Feature: universal-remote, Property 12: rename metamorphic`
    - **Property 12: Rename metamorphic — untouched signals unchanged**
    - **Validates: Requirement 15.3**

  - [ ]* 9.4 Write property test — delete metamorphic (Property 13)
    - In `tools/host_test/test_flipper_ir.c`, add a property driver that creates
      `.ir` files with 1..64 randomly generated signals, picks a random delete
      target index `i`, calls `flipper_ir_delete_signal()`, reads back all signals,
      and asserts: N-1 signals remain; signals at `j < i` are unchanged at their
      original index; signals at `j > i` appear at `j-1` with unchanged data;
      deleting the last signal leaves a file where `flipper_ir_open()` returns
      `true`.
    - ≥ 100 iterations.
    - Tag: `// Feature: universal-remote, Property 13: delete metamorphic`
    - **Property 13: Delete metamorphic — order and data of remaining signals preserved**
    - **Validates: Requirements 15.5, 15.6**

  - [ ]* 9.5 Write property test — create-then-open round-trip (Property 16)
    - In `tools/host_test/test_flipper_ir.c`, add a property driver that calls
      the file-level `ir_custom_create_empty()` equivalent (write header to a new
      path, close, then `flipper_ir_open()`) for ≥ 100 random sanitized names and
      asserts `flipper_ir_open()` returns `true` and `flipper_ir_count_signals()`
      returns 0.
    - Tag: `// Feature: universal-remote, Property 16: create-then-open round-trip`
    - **Property 16: Custom remote create-then-open round-trip**
    - **Validates: Requirements 13.5, 13.6**

  - [ ]* 9.6 Write property test — append increments signal count (Property 17)
    - In `tools/host_test/test_flipper_ir.c`, add a property driver that creates
      a `.ir` file with 0..63 randomly generated signals, records `before =
      flipper_ir_count_signals(path)`, appends one more signal (alternating parsed
      and raw), and asserts `flipper_ir_count_signals(path) == before + 1`.
    - ≥ 100 iterations.
    - Tag: `// Feature: universal-remote, Property 17: append count invariant`
    - **Property 17: Append increments signal count by exactly one**
    - **Validates: Requirement 14.5**

- [ ] 10. Register new host tests in `run_tests.sh` and final build verification
  - [ ] 10.1 Register all new host test binaries in `tools/host_test/run_tests.sh`
    - Append compilation and invocation steps for each new test:
      `test_ir_proto_map`, `test_ir_orient`, `test_ir_paths`, `test_ir_lists`,
      `test_ir_sanitize`, `test_ir_scroll`. Run in scratch directories.
    - Ensure `run_tests.sh` exits non-zero if any test fails.
    - _Requirements: 1.x, 2.x, 6.x, 7.x, 10.x, 12.x, 13.x, 15.x, 17.x_

  - [ ] 10.2 Final clean build and full test suite pass
    - Run `tools/host_test/run_tests.sh` — all suites green.
    - Run `./build.sh` — firmware builds cleanly (zero errors, zero warnings from
      project sources).
    - Confirm the `.bin` / `.elf` artefacts are emitted in `build/`.

- [ ] 11. Final checkpoint — Ensure all tests pass
  - All property-based tests pass (≥ 100 iterations each, zero failures).
  - All unit tests in `test_flipper_ir` pass.
  - `./build.sh` exits 0 with a clean firmware artefact.
  - Ask the user if questions arise.

---

## Notes

- Tasks marked with `*` are optional and can be skipped for a faster MVP; however,
  the correctness properties they cover are formally part of the spec.
- The design uses C throughout — no pseudocode, no language selection needed.
- Stack-only constraint: no `malloc`/`m1_malloc` in any new code path.
- `S_M1_FW_CONFIG_t` (exactly 20 bytes) is never touched by this feature.
- All file I/O must use FatFs (`ff.h`) — no POSIX file functions in firmware code.
- Build command: `./build.sh` (or `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`).
- Host tests compile with `-I tools/host_test -I m1_csrc` and link against
  `tools/host_test/ff_shim.c` (no FreeRTOS, no HAL).
- Each property test must be tagged with:
  `// Feature: universal-remote, Property N: <text>`

## Task Dependency Graph

```json
{
  "waves": [
    { "id": 0, "tasks": ["1.1", "1.2", "1.3", "1.4", "1.5"] },
    { "id": 1, "tasks": ["3.1", "4.1", "6.1", "6.2", "8.1", "8.3"] },
    { "id": 2, "tasks": ["3.2", "4.2", "5.1", "8.2"] },
    { "id": 3, "tasks": ["5.2", "5.3", "9.1", "9.2", "9.3", "9.4", "9.5", "9.6"] },
    { "id": 4, "tasks": ["10.1"] },
    { "id": 5, "tasks": ["10.2"] }
  ]
}
```
