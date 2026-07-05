/* See COPYING.txt for license details. */

/*
 * m1_msgbox_layout.h
 *
 * Pure, host-testable layout core for the readable message box. Word-wraps and
 * flows the message-box text into scrollable lines. Deliberately contains NO
 * u8g2 / FreeRTOS / FatFs headers and performs NO heap allocation — all output
 * goes into fixed-size, caller-provided buffers (per the project Architecture
 * Rules). The firmware injects u8g2_GetStrWidth as the pixel-measure callback;
 * host tests inject a len*6 stub.
 *
 * M1 Project
 */

#ifndef M1_MSGBOX_LAYOUT_H
#define M1_MSGBOX_LAYOUT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of wrapped lines held at once (a handful of 128x64 screens).
 * Wrapped output is capped here; content past the cap truncates gracefully. */
#define M1_MSGBOX_MAX_LINES 32

/* One wrapped line, expressed as a span into the caller's original const
 * string — never copied, never mutated. Draw s[off .. off+len). */
typedef struct {
	const char *base; /* source string this span points into */
	int off;          /* start offset within base */
	int len;          /* length of the span, in bytes */
} m1_msgbox_line_t;

/* Pixel-width measure callback: returns the rendered width, in pixels, of the
 * substring s[0 .. len). Firmware passes a wrapper over u8g2_GetStrWidth; host
 * tests pass a len*6 stub. */
typedef int (*m1_msgbox_measure_fn)(const char *s, int len, void *ctx);

/*
 * Word-wrap one NUL-terminated string into `out` (capacity `max_lines`),
 * appending lines starting at index `start_count`. Breaks at spaces where a
 * word boundary fits within `max_width`; a single token wider than `max_width`
 * is hard-broken (never dropped) so progress is always made. Leading and
 * word-separating spaces are consumed, not rendered. Stops once the cap is
 * reached (graceful truncation). Returns the new total line count
 * (>= start_count, <= max_lines).
 */
int m1_msgbox_wrap(const char *s, int max_width,
                   m1_msgbox_measure_fn measure, void *ctx,
                   m1_msgbox_line_t *out, int start_count, int max_lines);

/*
 * Flow the three message-box text segments (any of which may be NULL or "")
 * into a single ordered array of wrapped lines. Empty segments are skipped;
 * each non-empty segment is word-wrapped (via m1_msgbox_wrap) and begins on a
 * new line, in title1 -> title2 -> title3 order. Output is capped at
 * `max_lines`. Returns the total wrapped-line count (0 .. max_lines).
 */
int m1_msgbox_layout(const char *title1, const char *title2, const char *title3,
                     int max_width, m1_msgbox_measure_fn measure, void *ctx,
                     m1_msgbox_line_t *out, int max_lines);

#ifdef __cplusplus
}
#endif

#endif /* M1_MSGBOX_LAYOUT_H */
