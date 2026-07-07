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
