/* See COPYING.txt for license details. */

/*
*
*  m1_ir_custom.c
*
*  Custom (user-built) IR remotes: create a named remote, learn+name buttons,
*  replay from a scrolling button list, and edit (rename/delete) buttons.
*  Complements m1_ir_universal.c (IRDB browser) and m1_infrared.c (RX/TX core).
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "m1_compile_cfg.h"

#ifdef M1_APP_FILE_IMPORT_ENABLE

#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_ir_custom.h"
#include "m1_system.h"
#include "m1_display.h"
#include "m1_lcd.h"
#include "m1_tasks.h"
#include "m1_virtual_kb.h"
#include "flipper_file.h"
#include "flipper_ir.h"
#include "ff.h"

/*************************** D E F I N E S ************************************/

/* Root directory for user-built custom remotes on the SD card. */
#define IR_CUSTOM_DIR          "0:/IR"
#define IR_CUSTOM_NAME_MAX_LEN 32
#define IR_CUSTOM_PATH_MAX_LEN 256
#define IR_CUSTOM_MAX_DEDUP    99

//****************************** V A R I A B L E S *****************************/

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void ir_custom_sanitize_name(const char *in, char *out, size_t out_len);
static bool ir_custom_unique_path(const char *name, char *out, size_t out_len);
static bool ir_custom_create_empty(const char *name, char *out_path, size_t out_len);
static void ir_custom_draw_result(bool ok, const char *name);
static void ir_custom_wait_key(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief  Sanitize a user-entered name into a FAT-legal filename component.
 *         Path separators, reserved characters, and control characters are
 *         replaced with '_'; trailing spaces/dots are trimmed (FAT dislikes
 *         them). Falls back to "Remote" if nothing usable remains.
 * @param  in       raw name (may be NULL)
 * @param  out      output buffer
 * @param  out_len  size of out (must be > 0)
 */
static void ir_custom_sanitize_name(const char *in, char *out, size_t out_len)
{
	size_t j = 0;

	if (out == NULL || out_len == 0)
		return;

	if (in != NULL)
	{
		size_t i;
		for (i = 0; in[i] != '\0' && j < out_len - 1; i++)
		{
			unsigned char c = (unsigned char)in[i];

			if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' ||
			    c == '?'  || c == '"' || c == '<'  || c == '>' || c == '|')
				out[j++] = '_';
			else
				out[j++] = (char)c;
		}
	}
	out[j] = '\0';

	/* Trim trailing spaces and dots. */
	while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '.'))
		out[--j] = '\0';

	if (j == 0)
	{
		strncpy(out, "Remote", out_len - 1);
		out[out_len - 1] = '\0';
	}
}

/*============================================================================*/
/**
 * @brief  Build a unique "0:/IR/<name>.ir" path, appending _1, _2, ... on
 *         collision with an existing file.
 * @param  name     sanitized base name
 * @param  out      output path buffer
 * @param  out_len  size of out
 * @return true if a free path was produced
 */
static bool ir_custom_unique_path(const char *name, char *out, size_t out_len)
{
	FILINFO fno;
	int     n;
	unsigned i;

	/* Ensure the destination directory exists (ignore "already exists"). */
	f_mkdir(IR_CUSTOM_DIR);

	n = snprintf(out, out_len, "%s/%s.ir", IR_CUSTOM_DIR, name);
	if (n < 0 || n >= (int)out_len)
		return false;
	if (f_stat(out, &fno) != FR_OK)
		return true;   /* no such file -> unique */

	for (i = 1; i <= IR_CUSTOM_MAX_DEDUP; i++)
	{
		n = snprintf(out, out_len, "%s/%s_%u.ir", IR_CUSTOM_DIR, name, i);
		if (n < 0 || n >= (int)out_len)
			return false;
		if (f_stat(out, &fno) != FR_OK)
			return true;
	}

	return false;   /* too many name collisions */
}

/*============================================================================*/
/**
 * @brief  Create an empty (header-only, zero-signal) custom remote file.
 * @param  name      sanitized remote name
 * @param  out_path  receives the full path that was written
 * @param  out_len   size of out_path
 * @return true on success
 */
static bool ir_custom_create_empty(const char *name, char *out_path, size_t out_len)
{
	flipper_file_t ff;

	if (!ir_custom_unique_path(name, out_path, out_len))
		return false;

	if (!ff_open_write(&ff, out_path))
		return false;

	if (!flipper_ir_write_header(&ff))
	{
		ff_close(&ff);
		return false;
	}

	ff_close(&ff);
	return true;
}

/*============================================================================*/
/**
 * @brief  Draw the create-result screen (success shows the remote name).
 */
static void ir_custom_draw_result(bool ok, const char *name)
{
	m1_u8g2_firstpage();

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 2, 10, "Create Remote");
	u8g2_DrawHLine(&m1_u8g2, 0, 12, 128);

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	if (ok)
	{
		u8g2_DrawStr(&m1_u8g2, 4, 28, "Created:");
		u8g2_DrawStr(&m1_u8g2, 4, 40, (name != NULL) ? name : "");
	}
	else
	{
		u8g2_DrawStr(&m1_u8g2, 4, 34, "Create failed");
	}

	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "", arrowright_8x8);

	m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief  Block until a confirm/cancel key is pressed, then return to caller.
 */
static void ir_custom_wait_key(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t       q_item;
	BaseType_t          ret;

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE)
			continue;

		if (q_item.q_evt_type != Q_EVENT_KEYPAD)
			continue;

		ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
		if (ret != pdTRUE)
			continue;

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
		    this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK ||
		    this_button_status.event[BUTTON_OK_KP_ID]   == BUTTON_EVENT_CLICK)
		{
			xQueueReset(main_q_hdl);
			break;
		}
	}
}

/*============================================================================*/
/**
 * @brief  Custom-remote entry point ("Create Remote" Infrared submenu item).
 *         Prompts for a name via the on-screen keyboard, then writes an empty
 *         (header-only) remote file under 0:/IR/. Cancelling the keyboard
 *         writes nothing. Learn/replay/edit of the remote land in later slices.
 */
void ir_custom_run(void)
{
	char    default_name[IR_CUSTOM_NAME_MAX_LEN] = "Remote";
	char    entered[IR_CUSTOM_NAME_MAX_LEN];
	char    name[IR_CUSTOM_NAME_MAX_LEN];
	char    path[IR_CUSTOM_PATH_MAX_LEN];
	uint8_t got;
	bool    ok;

	entered[0] = '\0';

	/* Prompt for a remote name (returns 0 if the user escaped). */
	got = m1_vkb_get_filename("Remote name:", default_name, entered);
	if (got == 0)
		return;   /* cancelled — nothing written */

	ir_custom_sanitize_name(entered, name, sizeof(name));
	ok = ir_custom_create_empty(name, path, sizeof(path));

	ir_custom_draw_result(ok, name);
	ir_custom_wait_key();
} /* void ir_custom_run(void) */

#endif /* M1_APP_FILE_IMPORT_ENABLE */
