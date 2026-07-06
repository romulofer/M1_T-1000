/* See COPYING.txt for license details. */

/*
 * flipper_file.h
 *
 * Generic Flipper Zero file format parser (key-value line based)
 *
 * M1 Project
 */

#ifndef FLIPPER_FILE_H_
#define FLIPPER_FILE_H_

#include "ff.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define FF_LINE_BUF_LEN    512
#define FF_KEY_MAX_LEN     64
#define FF_VALUE_MAX_LEN   448

/*
 * Read-ahead chunk for the buffered line reader. ff_read_line() refills its
 * window with a single f_read() of this many bytes instead of FatFs f_gets()'s
 * one-f_read-per-character (~8.5k 1-byte reads for an 8.5 KB .ir file — the
 * source of the Universal Remotes panel stall). Kept modest at 256 because
 * flipper_file_t is stack-allocated at several call sites and already ~1 KB
 * (line[512] + value[448] + key[64]); 256 is a bounded bump that stays within
 * task-stack headroom while collapsing the per-character read overhead.
 */
#define FF_READ_CHUNK      256

typedef struct {
	FIL    fh;
	char   line[FF_LINE_BUF_LEN];
	char   key[FF_KEY_MAX_LEN];
	char   value[FF_VALUE_MAX_LEN];
	bool   is_open;
	bool   eof;
	/* Buffered-reader state: ff_read_line() assembles lines out of buf[],
	 * refilled one FF_READ_CHUNK block at a time. buf_pos..buf_len is the
	 * unconsumed window; both are 0 after ff_open*'s memset, which reads as
	 * "empty -> refill on the first read". */
	BYTE   buf[FF_READ_CHUNK];
	UINT   buf_len;   /* valid bytes currently in buf                 */
	UINT   buf_pos;   /* next unread byte in buf (== buf_len => empty) */
} flipper_file_t;

/* Open a file for reading */
bool ff_open(flipper_file_t *ctx, const char *path);

/* Open a file for writing */
bool ff_open_write(flipper_file_t *ctx, const char *path);

/* Open a file for appending (seek to end; create if absent) */
bool ff_open_append(flipper_file_t *ctx, const char *path);

/* Close file */
void ff_close(flipper_file_t *ctx);

/* Read next non-comment, non-empty line. Returns false at EOF */
bool ff_read_line(flipper_file_t *ctx);

/* Parse current line as "key: value". Returns false if not key-value format */
bool ff_parse_kv(flipper_file_t *ctx);

/* After ff_parse_kv(), get the key string */
const char *ff_get_key(const flipper_file_t *ctx);

/* After ff_parse_kv(), get the value string */
const char *ff_get_value(const flipper_file_t *ctx);

/* Check if current line is a separator (#) */
bool ff_is_separator(const flipper_file_t *ctx);

/* Validate header: reads first two lines and checks Filetype and Version */
bool ff_validate_header(flipper_file_t *ctx, const char *expected_filetype, uint8_t min_version);

/* Write functions */
bool ff_write_kv_str(flipper_file_t *ctx, const char *key, const char *value);
bool ff_write_kv_uint32(flipper_file_t *ctx, const char *key, uint32_t val);
bool ff_write_kv_float(flipper_file_t *ctx, const char *key, float val);
bool ff_write_kv_hex(flipper_file_t *ctx, const char *key, const uint8_t *data, uint8_t len);
bool ff_write_separator(flipper_file_t *ctx);
bool ff_write_comment(flipper_file_t *ctx, const char *comment);

/* Utility: parse hex bytes from a value string like "07 00 00 00" */
uint8_t ff_parse_hex_bytes(const char *str, uint8_t *out, uint8_t max_len);

/* Utility: parse space-separated int32 values from a data line */
uint16_t ff_parse_int32_array(const char *str, int32_t *out, uint16_t max_count);

#endif /* FLIPPER_FILE_H_ */
