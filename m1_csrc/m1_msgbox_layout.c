/* See COPYING.txt for license details. */

/*
 * m1_msgbox_layout.c
 *
 * Pure layout core for the readable message box (see m1_msgbox_layout.h).
 * No firmware headers, no heap.
 *
 * M1 Project
 */

#include "m1_msgbox_layout.h"

#include <string.h>

/* Emit one line span [off, off+len) into out[count]; returns count+1. */
static int emit_line(m1_msgbox_line_t *out, int count,
                     const char *base, int off, int len)
{
	out[count].base = base;
	out[count].off = off;
	out[count].len = len;
	return count + 1;
}

int m1_msgbox_wrap(const char *s, int max_width,
                   m1_msgbox_measure_fn measure, void *ctx,
                   m1_msgbox_line_t *out, int start_count, int max_lines)
{
	int count = start_count;
	if (s == NULL || measure == NULL)
		return count;

	int n = (int)strlen(s);
	int i = 0;

	while (count < max_lines) {
		/* Start a fresh line: consume leading / separating spaces. */
		while (i < n && s[i] == ' ')
			i++;
		if (i >= n)
			break;

		int line_start = i;
		int line_end = i; /* exclusive end of committed content on this line */

		/* Greedily extend the line one word at a time. */
		for (;;) {
			int w = line_end;
			while (w < n && s[w] == ' ') /* skip spaces before the next word */
				w++;
			if (w >= n)
				break; /* no more words */

			int word_end = w;
			while (word_end < n && s[word_end] != ' ')
				word_end++;

			if (measure(s + line_start, word_end - line_start, ctx) <= max_width) {
				line_end = word_end; /* word (and preceding spaces) fits */
				continue;
			}

			if (line_end == line_start) {
				/* First word alone overflows: hard-break it. Keep the largest
				 * prefix that fits, but always at least one char so we make
				 * progress even if a single glyph exceeds the width. */
				int fit = 1;
				while (line_start + fit < word_end &&
				       measure(s + line_start, fit + 1, ctx) <= max_width)
					fit++;
				line_end = line_start + fit;
			}
			break;
		}

		count = emit_line(out, count, s, line_start, line_end - line_start);
		i = line_end;
	}

	return count;
}

/* TODO(Task 2): flow the three segments. Stub returns 0 so the host test
 * starts RED. */
int m1_msgbox_layout(const char *title1, const char *title2, const char *title3,
                     int max_width, m1_msgbox_measure_fn measure, void *ctx,
                     m1_msgbox_line_t *out, int max_lines)
{
	(void)title1;
	(void)title2;
	(void)title3;
	(void)max_width;
	(void)measure;
	(void)ctx;
	(void)out;
	(void)max_lines;
	return 0;
}
