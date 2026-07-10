# Requirements Document

## Introduction

This feature ports Flipper Zero's Universal IR Remote functionality to the M1 T-1000 firmware.
The M1 T-1000 already has IR hardware (irmp/irsnd, TIM1/TIM2/TIM16 timers on STM32H573VIT) and
a partial implementation. This spec closes the gap between the current state and a full, Flipper-
compatible universal remote covering: Flipper `.ir` file parsing/writing, IRDB browsing, signal
transmission (parsed + raw), learned-signal capture, custom remote building, favorites, recent
history, universal power-off blasting, and Remote Mode display rotation.

All file I/O uses the FAT filesystem via FatFs (`ff.h`). All Flipper file parsers use stack
allocation only (no heap/malloc). The architecture constraint `S_M1_FW_CONFIG_t` (exactly 20 bytes)
must not be touched.

---

## Glossary

- **IR_App**: The Infrared application module, entered via the main menu `Infrared` item.
- **IRDB**: IR signal database — a directory tree of `.ir` files on the SD card, rooted at `0:/IR`.
- **IR_File**: A Flipper Zero format `.ir` file containing one or more IR signal blocks.
- **IR_Signal**: One IR signal block within an IR_File, either `parsed` (protocol/address/command) or `raw` (timing samples).
- **Parser**: The `flipper_ir_*` functions that read and write IR_Files on the SD card.
- **Transmitter**: The `infrared_transmit()` + `irsnd_*` pipeline that drives the IR LED.
- **Receiver**: The `infrared_decode_sys_init()` + `irmp_*` pipeline that reads from the IR photodiode.
- **Custom_Remote**: A user-built `.ir` file stored in `0:/IR/` at the root level, populated by learning individual buttons.
- **IRDB_Browser**: The hierarchical directory navigator that lets users drill into category/brand/device folders.
- **Favorites**: A persisted list of up to `IR_UNIVERSAL_MAX_FAVORITES` IR_File paths, stored in `0:/System/ir_favorites.txt`.
- **Recent**: A persisted MRU list of up to `IR_UNIVERSAL_MAX_RECENT` IR_File paths, stored in `0:/System/ir_recent.txt`.
- **Power_Blast**: Rapid sequential transmission of every signal in a designated database file (e.g., `Universal_Power.ir`).
- **Remote_Mode**: A landscape display orientation (`U8G2_R1`) that puts the device in a TV-remote-like posture.
- **Virtual_KB**: The on-screen keyboard (`m1_vkb_get_filename`) used to enter names.
- **FreeRTOS_Queue**: The main event queue (`main_q_hdl`) and button queue (`button_events_q_hdl`) used for task communication.

---

## Requirements

### Requirement 1: Flipper .ir File Parsing

**User Story:** As a user, I want the M1 T-1000 to read `.ir` files created on or exported from a
Flipper Zero, so that I can reuse my existing IR signal libraries without conversion.

#### Acceptance Criteria

1. WHEN a valid `.ir` file is provided, THE Parser SHALL parse `parsed`-type signal blocks into
   a `flipper_ir_signal_t` with `protocol`, `address`, `command`, and `flags` fields populated.
2. WHEN a valid `.ir` file is provided, THE Parser SHALL parse `raw`-type signal blocks into a
   `flipper_ir_signal_t` with `frequency`, `duty_cycle`, and a `samples[]` array of up to
   `FLIPPER_IR_RAW_MAX_SAMPLES` (512) integer timing values.
3. WHEN the file header `Filetype` is not `IR signals file` or `Version` is below 1, THE Parser
   SHALL return `false` from `flipper_ir_open()` without reading further signal blocks.
4. WHEN a signal block has an unrecognised `type` field, THE Parser SHALL skip that block and
   continue parsing subsequent blocks.
5. THE Parser SHALL implement a round-trip property: FOR ALL valid `flipper_ir_signal_t` values,
   writing a signal with `flipper_ir_write_signal()` then reading it back with
   `flipper_ir_read_signal()` SHALL produce an equivalent signal (same type, same name, same
   protocol/address/command for parsed, same sample array for raw).
6. WHEN a `parsed` signal block is missing the `command` field, THE Parser SHALL NOT mark the
   signal as valid and SHALL NOT return `true` for that block.
7. WHEN a `raw` signal block has zero samples in the `data` field, THE Parser SHALL NOT mark the
   signal as valid.

---

### Requirement 2: Protocol Mapping (Flipper ↔ IRMP)

**User Story:** As a user, I want the M1 T-1000 to transmit signals from all Flipper-supported
IR protocols, so that files from the community IRDB work with my devices.

#### Acceptance Criteria

1. THE Parser SHALL map the Flipper protocol names `NEC`, `NECext`, `NEC42`, `NEC16`, `Samsung32`,
   `Samsung48`, `RC5`, `RC5X`, `RC6`, `SIRC`, `SIRC15`, `SIRC20`, `Kaseikyo`, `Panasonic`, `RCA`,
   `Pioneer`, `Denon`, `Sharp`, `JVC`, `LG`, `Apple`, `Nokia`, `Bose`, and `RCMM` to their
   corresponding IRMP protocol IDs.
2. WHEN `flipper_ir_proto_to_irmp()` is called with a known protocol name, THE Parser SHALL return
   a non-zero IRMP protocol ID.
3. WHEN `flipper_ir_proto_to_irmp()` is called with an unknown protocol name, THE Parser SHALL
   return `IRMP_UNKNOWN_PROTOCOL`.
4. THE Parser SHALL implement a round-trip property: FOR ALL known Flipper protocol names `n`,
   `flipper_ir_irmp_to_proto(flipper_ir_proto_to_irmp(n))` SHALL return either `n` or the canonical
   name for the same IRMP protocol (aliases like `NECext` mapping to IRMP NEC and back to `NEC`
   are acceptable; returning `Unknown` is NOT acceptable).

---

### Requirement 3: Parsed Signal Transmission

**User Story:** As a user, I want to transmit any `parsed`-type IR signal from a `.ir` file so
that I can control my devices.

#### Acceptance Criteria

1. WHEN a `parsed` IR_Signal is selected and the OK button is pressed, THE Transmitter SHALL
   call `irsnd_generate_tx_data()` with the IRMP_DATA populated from the signal's protocol,
   address, and command fields, and then invoke `infrared_transmit()`.
2. WHILE a transmission is in progress, THE Transmitter SHALL activate the IR LED driver via
   `irsnd_on()` during mark intervals and disable it during space intervals.
3. WHEN `infrared_transmit()` completes, THE Transmitter SHALL set `ir_ota_data_tx_active` to
   `false` and return `IR_TX_COMPLETED`.
4. WHEN the encode timer (`Timerhdl_IrTx`) fires a period-elapsed interrupt, THE Transmitter
   SHALL advance to the next OTA buffer sample, respecting the mark/space bit in the LSB.
5. IF the OTA frame buffer is not ready when transmission is initiated, THEN THE Transmitter
   SHALL skip transmission and transition to `IR_TX_COMPLETED` without asserting the IR LED.

---

### Requirement 4: Raw Signal Transmission

**User Story:** As a user, I want to transmit `raw`-type IR signals that couldn't be decoded by
IRMP, so that I can control devices with non-standard protocols.

#### Acceptance Criteria

1. WHEN a `raw` IR_Signal is selected and the OK button is pressed, THE Transmitter SHALL
   configure the carrier timer (`Timerhdl_IrCarrier`) to the signal's `frequency` before
   transmitting the timing sample array.
2. THE Transmitter SHALL play back the `samples[]` array, treating positive values as mark
   durations (µs) and negative values as space durations (µs).
3. WHEN a raw sample array has an odd number of samples (last sample is a space), THE Transmitter
   SHALL complete the sequence without appending an extra mark.
4. WHEN raw transmission finishes, THE Transmitter SHALL disable the IR carrier and restore the
   carrier timer to the default 38 kHz configuration.

---

### Requirement 5: IR Signal Learning (IRMP-Decoded)

**User Story:** As a user, I want to point any IR remote at the M1 T-1000 and have it learn the
button signal, so that I can build a personal remote library.

#### Acceptance Criteria

1. WHEN the "Learn" menu item is selected, THE Receiver SHALL call `infrared_decode_sys_init()`
   and `irmp_init()` and display a "Reading..." prompt.
2. WHEN the Receiver captures a valid IRMP frame via `irmp_get_data()`, THE IR_App SHALL display
   the decoded protocol name, address (hex), and command (hex) on screen.
3. WHEN a signal has been displayed and the user presses OK, THE IR_App SHALL save the signal
   as a new `.ir` file in `0:/IR/Learned/remote_NNN.ir` where NNN is the next available three-
   digit sequence number.
4. WHEN saving succeeds (the signal has been written to the SD card), THE IR_App SHALL display
   "Saved!" and the target path for 1 500 ms, then return to the "Reading..." prompt.
5. IF saving fails for any reason — including SD card absent, filesystem error, or the file write
   completing but data not flushed — THEN THE IR_App SHALL display "Save failed" and "Check SD
   card" for 1 500 ms, then return to the "Reading..." prompt; the signal SHALL NOT be considered
   saved.
6. WHEN the user presses BACK, THE Receiver SHALL call `infrared_decode_sys_deinit()`, reset the
   FreeRTOS_Queue, and return to the Infrared sub-menu.

---

### Requirement 6: Raw Signal Capture Fallback

**User Story:** As a user, I want the M1 T-1000 to capture IR signals that IRMP cannot decode
(e.g. proprietary protocols), so that no remote is left behind.

#### Acceptance Criteria

1. WHEN IRMP cannot decode a received frame and an inter-frame gap exceeding `IRMP_TIMEOUT_TIME`
   is detected, THE Receiver SHALL close the raw accumulation window.
2. WHEN the closed raw frame contains at least `IR_RAW_MIN_SAMPLES` (8) edge samples, THE
   Receiver SHALL mark the signal valid with `FLIPPER_IR_RAW_FRAME_COMPLETE` and store the
   sample array with the default 38 kHz carrier frequency and 0.33 duty cycle.
3. WHEN the closed raw frame contains fewer than `IR_RAW_MIN_SAMPLES` edges, THE Receiver SHALL
   reset the accumulator (preserving the signal name) and return `FLIPPER_IR_RAW_FRAME_NOISE`.
4. THE Receiver SHALL implement a round-trip property: FOR ALL raw signal accumulators `s` that
   yield `FLIPPER_IR_RAW_FRAME_COMPLETE`, writing `s` with `flipper_ir_write_signal()` then reading
   it back SHALL produce a signal with an identical sample array.
5. WHEN the raw sample buffer is full (`FLIPPER_IR_RAW_MAX_SAMPLES` = 512 samples), THE Receiver
   SHALL return `FLIPPER_IR_RAW_EDGE_DROPPED` for subsequent edges without corrupting captured
   samples.

---

### Requirement 7: IRDB Browser

**User Story:** As a user, I want to browse the IR database hierarchy stored on the SD card, so
that I can find and transmit signals for any device brand and category.

#### Acceptance Criteria

1. WHEN the "Browse IRDB" dashboard item is selected, THE IRDB_Browser SHALL navigate to
   `0:/IR` and display subdirectories and `.ir` files using a scrollable list with a visible
   window of 4 items.
2. WHEN the user presses OK on a directory entry, THE IRDB_Browser SHALL descend into that
   subdirectory and refresh the list.
3. WHEN the user presses BACK at any directory level below the IRDB root, THE IRDB_Browser SHALL
   ascend one level and restore the previous selection state.
4. WHEN the user presses BACK at the IRDB root, THE IRDB_Browser SHALL return to the Universal
   Remote dashboard.
5. WHEN a directory is empty, THE IRDB_Browser SHALL display "No files found" and a "Back to
   return" hint.
6. WHEN the user presses OK on an `.ir` file entry, THE IRDB_Browser SHALL open that file in the
   command-list view and add the file path to the Recent list.
7. THE IRDB_Browser SHALL implement a navigation invariant: the current path SHALL always be a
   descendant of `IR_UNIVERSAL_IRDB_ROOT` (`0:/IR`) or equal to it; navigating up from the root
   SHALL be a no-op.

---

### Requirement 8: Command List View

**User Story:** As a user, I want to see all named buttons in a `.ir` file and fire them one at
a time, so that I can use the M1 T-1000 as a point-and-shoot remote.

#### Acceptance Criteria

1. WHEN a `.ir` file is opened, THE IR_App SHALL parse all signal blocks and display each
   signal's `name` field in a scrollable list (4 items visible, UP/DOWN navigation).
2. WHEN the user presses OK on a command, THE IR_App SHALL transmit that command (parsed via
   `infrared_transmit()`, raw via the raw TX path) and provide LED feedback.
3. WHEN the file contains zero valid signals, THE IR_App SHALL display both "No IR signals" and a
   "Back to return" hint together on the same screen.
4. WHEN the file contains more than `IR_UNIVERSAL_MAX_CMDS` (64) signals, THE IR_App SHALL load
   the first 64 signals; the displayed count MAY reflect the actual number loaded by the
   `parse_ir_file()` call, which SHALL NOT exceed 64.
5. THE IR_App SHALL implement a count invariant: the number of items shown in the command list
   SHALL equal the number of valid signal blocks returned by `flipper_ir_read_signal()` for that
   file, up to `IR_UNIVERSAL_MAX_CMDS`.

---

### Requirement 9: Universal Power-Off Blast

**User Story:** As a user, I want to power off all TVs or A/V receivers in a room with a single
action, so that I can quickly silence a space without knowing which remotes are needed.

#### Acceptance Criteria

1. WHEN "Power Off TVs" is selected, THE IR_App SHALL open `0:/IR/TV/Universal_Power.ir` and
   transmit every parsed signal in the file sequentially, with a short inter-signal delay.
2. WHEN "Power Off A/V" is selected, THE IR_App SHALL open `0:/IR/Audio/Universal_Power.ir` and
   `0:/IR/Projector/Universal_Projector.ir` in sequence, transmitting every signal in each file.
3. WHILE a Power_Blast is in progress, THE IR_App SHALL display both the category title and a
   transmission progress indicator together, and SHALL enable LED blinking.
4. WHEN a Power_Blast database file is absent or unreadable, THE IR_App SHALL skip that file,
   display a brief "File not found" message, and continue with the next file in the sequence
   (if any).
5. WHEN all signals in a Power_Blast sequence have been transmitted, THE IR_App SHALL disable
   LED blinking and return to the dashboard.

---

### Requirement 10: Remote Mode (Landscape Orientation)

**User Story:** As a user, I want to rotate the M1 T-1000 display to landscape orientation while
using it as a remote control, so that the device fits naturally in-hand like a TV remote.

#### Acceptance Criteria

1. WHEN "Remote Mode" is selected from the Universal Remote dashboard, THE IR_App SHALL set
   display rotation to `U8G2_R1` (90° landscape) and set `m1_screen_orientation` to
   `M1_ORIENT_REMOTE`.
2. WHEN the user is in Remote_Mode and selects "Normal Mode", THE IR_App SHALL restore display
   rotation to `U8G2_R2` (portrait) and set `m1_screen_orientation` to `M1_ORIENT_NORMAL`; if
   `m1_screen_orientation` is already `M1_ORIENT_NORMAL` when "Normal Mode" is selected, THE
   IR_App SHALL take no action.
3. WHEN the user exits the Universal Remote app (presses BACK from the dashboard) while in
   Remote_Mode, THE IR_App SHALL restore display rotation to `U8G2_R2` and `m1_screen_orientation`
   to `M1_ORIENT_NORMAL` before returning to the Infrared sub-menu.
4. THE IR_App SHALL implement an idempotence property: toggling Remote_Mode on and then off SHALL
   return `m1_screen_orientation` and `u8g2` display rotation to the values they held before the
   first toggle.
5. WHERE the user has a persistent `M1_ORIENT_SOUTHPAW` orientation set in settings, THE IR_App
   SHALL NOT overwrite that orientation when exiting Remote_Mode; it SHALL restore
   `M1_ORIENT_SOUTHPAW` instead of `M1_ORIENT_NORMAL`.

---

### Requirement 11: Favorites Management

**User Story:** As a user, I want to mark `.ir` files as favorites so that I can reach
frequently-used remotes quickly without navigating the full IRDB hierarchy.

#### Acceptance Criteria

1. WHEN the Favorites screen is opened, THE IR_App SHALL read `0:/System/ir_favorites.txt` and
   display the list of saved `.ir` file paths; if the file is absent the list SHALL be empty.
2. WHEN a user opens a `.ir` file from the Favorites list, THE IR_App SHALL open that file's
   command-list view.
3. THE IR_App SHALL enforce a count invariant: the Favorites list SHALL never contain more than
   `IR_UNIVERSAL_MAX_FAVORITES` (10) entries; when a new entry would exceed this limit, the
   oldest entry SHALL be evicted.
4. WHEN the Universal Remote app exits, THE IR_App SHALL write the current Favorites list back
   to `0:/System/ir_favorites.txt` so the list persists across power cycles.
5. IF writing `ir_favorites.txt` fails, THE IR_App SHALL silently discard the write error and
   return to normal operation without crashing.

---

### Requirement 12: Recent Files Tracking

**User Story:** As a user, I want a "Recent" list that automatically tracks the last IR files I
opened, so that I can quickly re-access files without browsing.

#### Acceptance Criteria

1. WHEN a `.ir` file is opened from the IRDB_Browser or Favorites, THE IR_App SHALL prepend that
   file's path to the Recent list.
2. THE IR_App SHALL enforce a count invariant: the Recent list SHALL never contain more than
   `IR_UNIVERSAL_MAX_RECENT` (10) entries; the oldest entry SHALL be evicted when the limit is
   reached.
3. THE IR_App SHALL enforce a deduplication invariant: if a path already exists in the Recent
   list, THE IR_App SHALL move it to the front instead of creating a duplicate entry.
4. WHEN the Recent screen is opened, THE IR_App SHALL display the list ordered from most-recently-
   opened to least-recently-opened.
5. WHEN the Universal Remote app exits, THE IR_App SHALL write the current Recent list to
   `0:/System/ir_recent.txt` so the list persists across power cycles.

---

### Requirement 13: Custom Remote Creation

**User Story:** As a user, I want to create a named custom remote file on the SD card, so that
I can group learned buttons for a specific device under one file.

#### Acceptance Criteria

1. WHEN the user selects "[+ New Remote]" in the Custom Remotes screen, THE IR_App SHALL invoke
   the Virtual_KB with a default name `Remote` to let the user enter a custom name.
2. WHEN the user confirms a name, THE IR_App SHALL sanitize it to a FAT-legal filename component:
   replacing control characters, path separators (`/`, `\`), and FAT-reserved characters
   (`*`, `?`, `"`, `<`, `>`, `|`, `:`) with `_`, and trimming trailing spaces and dots.
3. THE IR_App SHALL implement a sanitization idempotence property: for all input strings `s`,
   `ir_custom_sanitize_name(ir_custom_sanitize_name(s))` SHALL equal `ir_custom_sanitize_name(s)`.
4. WHEN the sanitized name already exists as a file in `0:/IR/`, THE IR_App SHALL append `_N`
   (N = 1, 2, … up to `IR_CUSTOM_MAX_DEDUP` = 99) to produce a unique path.
5. THE IR_App SHALL write a valid Flipper `.ir` header (Filetype + Version lines) to the new
   file only after the destination directory has been confirmed to exist; IF directory creation
   fails, THE IR_App SHALL skip all file operations and display "Create failed".
6. WHEN the new file has been written, `flipper_ir_open()` on that file SHALL return `true`
   (round-trip property).
7. IF the SD card is absent or the directory cannot be created, THE IR_App SHALL display
   "Create failed" and return to the Custom Remotes list without writing any file.

---

### Requirement 14: Learning Buttons into a Custom Remote

**User Story:** As a user, I want to learn individual IR buttons and append them to an existing
custom remote file, so that I can build a complete device remote incrementally.

#### Acceptance Criteria

1. WHEN the user selects "Learn Button" for a Custom_Remote, THE Receiver SHALL initialize the
   IR decoder and display "Point remote at M1, press a button...".
2. WHEN IRMP successfully decodes a frame, THE IR_App SHALL display the protocol name, address
   (hex), and command (hex); IF IRMP cannot decode a frame, THE IR_App SHALL NOT display address
   or command fields derived from a failed decode.
3. WHEN IRMP cannot decode the frame but a valid raw frame is captured (≥ 8 edges, inter-frame
   gap detected), THE IR_App SHALL display the sample count and offer the user the option to
   save (OK) or discard (BACK).
4. WHEN the user confirms saving, THE IR_App SHALL invoke the Virtual_KB with an auto-suggested
   button name (`<Protocol>_<Command>` for parsed, `Raw` for raw), allow the user to edit it,
   then append the signal to the Custom_Remote file.
5. THE IR_App SHALL implement a button count invariant: the number of signals readable from the
   Custom_Remote file after a successful append SHALL equal the count before the append plus one.
6. WHEN the user presses BACK at any point in the learn flow, THE Receiver SHALL be torn down via
   `infrared_decode_sys_deinit()` and the file SHALL NOT be modified.

---

### Requirement 15: Editing Buttons in a Custom Remote (Rename / Delete)

**User Story:** As a user, I want to rename or delete individual buttons in a custom remote, so
that I can keep my remote library clean and well-labelled.

#### Acceptance Criteria

1. WHEN the user selects "Edit Buttons" for a Custom_Remote, THE IR_App SHALL display a
   scrollable list of all button names in the file.
2. WHEN the user selects "Rename" on a button, THE IR_App SHALL invoke the Virtual_KB with the
   current name pre-filled, then rewrite the file via `flipper_ir_rename_signal()`.
3. THE IR_App SHALL implement a rename metamorphic property: after renaming signal at index `i`,
   all other signals (indices ≠ `i`) in the file SHALL have unchanged names, types, and data.
4. WHEN the user selects "Delete" on a button, THE IR_App SHALL show a confirmation dialog
   ("Cancel" / "Delete") before calling `flipper_ir_delete_signal()`.
5. THE IR_App SHALL implement a delete metamorphic property: after deleting signal at index `i`,
   all signals that were at indices < `i` and > `i` SHALL remain in the file in their original
   order with unchanged data.
6. WHEN the last button in a Custom_Remote is deleted, THE IR_App SHALL leave a valid header-
   only `.ir` file; `flipper_ir_open()` on that file SHALL return `true`.
7. IF the file rewrite fails at any point (including failure to create the temp file, a write
   error during streaming, or failure to rename the temp file atomically), THE IR_App SHALL
   display a brief error message and return to the Edit Buttons list; `flipper_ir_rewrite()`
   SHALL delete the temporary file and leave the original file unchanged in all failure cases.

---

### Requirement 16: File I/O Error Handling

**User Story:** As a user, I want the M1 T-1000 to handle missing or corrupt files gracefully
so that the device never crashes or freezes when SD card content is unexpected.

#### Acceptance Criteria

1. IF `ff_open()` returns `false` for any file operation, THEN THE IR_App SHALL display a user-
   visible error message appropriate to the context (e.g. "No files found", "File not found",
   "Save failed") and return to the previous menu level; this requirement applies to both
   recoverable file I/O errors (handled by returning `false`) and unrecoverable errors.
2. IF `flipper_ir_rewrite()` fails to create the temporary file or rename it atomically, THEN
   THE IR_App SHALL delete the temporary file and preserve the original file unchanged.
3. WHEN `flipper_ir_open_append()` is called on a file that exists but has an invalid header,
   THE IR_App SHALL return `false` and SHALL NOT append data to a corrupt file.
4. THE IR_App SHALL NOT call `Error_Handler()` for recoverable file I/O errors; those SHALL be
   handled by returning `false` / showing an error screen.

---

### Requirement 17: Display and Navigation on 128×64 OLED

**User Story:** As a user, I want all IR app screens to fit and navigate correctly on the 128×64
OLED display so that I can use the feature without reading a manual.

#### Acceptance Criteria

1. THE IR_App SHALL display a title bar at the top of every list screen (using
   `M1_DISP_RUN_MENU_FONT_B`) and a bottom navigation bar showing current context actions.
2. WHEN a list has more items than the 4-item visible window, THE IR_App SHALL render a
   proportional scroll bar on the right edge of the display.
3. THE IR_App SHALL implement a scroll invariant: the selected item SHALL always be within the
   visible 4-item window; navigating to an item outside the window SHALL scroll the window to
   keep the selection visible.
4. WHEN the list has more than 1 item, THE IR_App SHALL wrap navigation immediately: pressing UP
   on the first item SHALL move the selection to the last item, and pressing DOWN on the last
   item SHALL move the selection to the first item.
5. THE IR_App SHALL display position feedback as `<current>/<total>` in the bottom bar of all
   list screens that contain more than `LIST_VISIBLE_ITEMS` (4) entries.

---

### Requirement 18: FreeRTOS Integration and Watchdog Safety

**User Story:** As a developer, I want the IR app to cooperate with FreeRTOS task scheduling and
the IWDG so that the firmware remains stable and watchdog-safe during long operations.

#### Acceptance Criteria

1. WHILE waiting for user input, THE IR_App SHALL block on `xQueueReceive(main_q_hdl, ...,
   portMAX_DELAY)` rather than busy-waiting, yielding the CPU to other tasks.
2. WHILE a Power_Blast sequence is transmitting multiple signals, THE IR_App SHALL call
   `m1_watchdog_feed()` (or equivalent) between signals to prevent IWDG reset during long blasts.
3. WHEN the IR encoder timer ISR (`HAL_TIM_PeriodElapsedCallback_IR`) fires, THE IR_App SHALL
   complete its handler within the IWDG period (≤ 1 ms for the 1 MHz baseband timer) to avoid
   priority inversion.
4. THE IR_App SHALL reset `main_q_hdl` via `xQueueReset()` immediately before returning from any
   screen function that consumed events; this reset SHALL NOT be deferred even if a Power_Blast
   sequence was in progress.
