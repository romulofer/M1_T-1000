/* See COPYING.txt for license details. */

/*
 *
 * m1_uremote_match.h
 *
 * Pure record-name matcher for Universal Remotes single-function blasts.
 * Decides which parsed .ir records a "press one key, brute-force every brand"
 * blast transmits: a record is sent only when its Flipper `name:` equals the
 * target function's record name or one of its aliases, case-insensitively.
 *
 * This is the correctness core of the feature -- a false negative makes a key
 * look "dead". It is deliberately dependency-free (no u8g2, no FreeRTOS, no
 * allocation, string logic only) so it is host-testable on its own.
 *
 * M1 Project
 *
 */

#ifndef M1_UREMOTE_MATCH_H_
#define M1_UREMOTE_MATCH_H_

#include <stdbool.h>
#include <stdint.h>

/* Case-insensitive FULL-string match of a parsed record `name` against a
 * target `record` name and an optional NULL-terminated `aliases` list.
 *
 * Returns true iff `name` equals `record`, or equals any entry in `aliases`,
 * comparing case-insensitively over the whole string (no substring matching,
 * so "Power" never matches "Power_On" in either direction).
 *
 * `aliases` may be NULL (record-only match). NULL `name` or `record` are
 * handled defensively (treated as non-matching). Pure: no side effects. */
bool uremote_name_matches(const char *name,
                          const char *record,
                          const char *const *aliases);

/* True iff every function has been found present, i.e. muted[j] == 0 for all
 * j < n. Vacuously true when n == 0. NULL `muted` is treated defensively as
 * "not all present" (false). Pure: no side effects.
 *
 * Lets the Universal Remotes muted scan (uremote_scan_present) stop early:
 * once this holds, no later record can change muted[], so reading the rest of
 * the file is wasted work -- the second half of the panel-open latency fix. */
bool uremote_all_present(const uint8_t *muted, uint8_t n);

#endif /* M1_UREMOTE_MATCH_H_ */
