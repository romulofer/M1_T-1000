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
 * Returns true and fills *cmd on a valid parsed or raw block.
 * raw_out/raw_cap optional: for a raw record, up to raw_cap timing samples are
 * written to raw_out and cmd->raw_count is set to the stored count. Pass NULL/0
 * to keep count-only behaviour (parsed records ignore these). */
bool uremote_parse_signal_block(flipper_file_t *ff, ir_universal_cmd_t *cmd,
                                int32_t *raw_out, uint16_t raw_cap);

/* Callback invoked once per matching record. Return false to stop early. */
typedef bool (*uremote_bf_cb_t)(void *ctx, const ir_universal_cmd_t *cmd, uint16_t match_index);

/* Stream `path`, invoking `cb` for every record (parsed or raw) whose name ==
 * `record_name`. If `cb` is NULL, records are only counted (no transmission).
 * raw_out/raw_cap are forwarded to uremote_parse_signal_block() for raw
 * records; pass NULL/0 if raw samples are not needed. Accepts both
 * "IR library file" and "IR signals file" headers. O(1) memory.
 * Returns the number of matching records handled (or counted). */
uint16_t uremote_bf_stream(const char *path, const char *record_name,
                           uremote_bf_cb_t cb, void *ctx,
                           int32_t *raw_out, uint16_t raw_cap);

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
