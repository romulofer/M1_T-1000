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
#include "m1_ir_universal.h"
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
#define IR_CUSTOM_DIR           "0:/IR"
#define IR_CUSTOM_NAME_MAX_LEN  32
#define IR_CUSTOM_PATH_MAX_LEN  256
#define IR_CUSTOM_MAX_DEDUP     99

/* My-Remotes manager list geometry (mirrors m1_ir_universal draw_list_screen). */
#define IR_CUSTOM_MAX_REMOTES   24
#define IR_CUSTOM_DISP_NAME_LEN 32
#define IR_CUSTOM_LIST_VISIBLE  4
#define IR_CUSTOM_LIST_HEADER_H 12
#define IR_CUSTOM_LIST_ITEM_H   9
#define IR_CUSTOM_LIST_START_Y  (IR_CUSTOM_LIST_HEADER_H + 2)

#define IR_CUSTOM_NEW_LABEL     "[+ New Remote]"

//****************************** V A R I A B L E S *****************************/

static char     s_remote_names[IR_CUSTOM_MAX_REMOTES][IR_CUSTOM_DISP_NAME_LEN];
static uint16_t s_remote_count;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void        ir_custom_sanitize_name(const char *in, char *out, size_t out_len);
static bool        ir_custom_unique_path(const char *name, char *out, size_t out_len);
static bool        ir_custom_create_empty(const char *name, char *out_path, size_t out_len);
static void        ir_custom_draw_result(bool ok, const char *name);
static void        ir_custom_wait_key(void);
static void        ir_custom_create_flow(void);
static void        ir_custom_scan_remotes(void);
static const char *ir_custom_item_text(uint16_t idx);
static void        ir_custom_draw_list(uint16_t selection, uint16_t total);

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
	FILINFO  fno;
	int      n;
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
 * @brief  Prompt for a name and create a new empty remote. Escaping the
 *         keyboard writes nothing.
 */
static void ir_custom_create_flow(void)
{
	char    default_name[IR_CUSTOM_NAME_MAX_LEN] = "Remote";
	char    entered[IR_CUSTOM_NAME_MAX_LEN];
	char    name[IR_CUSTOM_NAME_MAX_LEN];
	char    path[IR_CUSTOM_PATH_MAX_LEN];
	uint8_t got;
	bool    ok;

	entered[0] = '\0';

	got = m1_vkb_get_filename("Remote name:", default_name, entered);
	if (got == 0)
		return;   /* cancelled — nothing written */

	ir_custom_sanitize_name(entered, name, sizeof(name));
	ok = ir_custom_create_empty(name, path, sizeof(path));

	ir_custom_draw_result(ok, name);
	ir_custom_wait_key();
}

/*============================================================================*/
/**
 * @brief  Scan 0:/IR/ for user-built remote files (*.ir at the root, not the
 *         shipped category sub-directories) into s_remote_names[] (extension
 *         stripped for display).
 */
static void ir_custom_scan_remotes(void)
{
	DIR     dir;
	FILINFO fno;
	FRESULT res;

	s_remote_count = 0;

	f_mkdir(IR_CUSTOM_DIR);

	res = f_opendir(&dir, IR_CUSTOM_DIR);
	if (res != FR_OK)
		return;

	while (s_remote_count < IR_CUSTOM_MAX_REMOTES)
	{
		size_t len;
		size_t copy;

		res = f_readdir(&dir, &fno);
		if (res != FR_OK || fno.fname[0] == '\0')
			break;

		/* Skip hidden entries and category sub-directories. */
		if (fno.fname[0] == '.')
			continue;
		if (fno.fattrib & AM_DIR)
			continue;

		/* Only ".ir" files. */
		len = strlen(fno.fname);
		if (len < 4 || strcmp(&fno.fname[len - 3], ".ir") != 0)
			continue;

		/* Store the name without the ".ir" extension for display. */
		copy = len - 3;
		if (copy > IR_CUSTOM_DISP_NAME_LEN - 1)
			copy = IR_CUSTOM_DISP_NAME_LEN - 1;
		memcpy(s_remote_names[s_remote_count], fno.fname, copy);
		s_remote_names[s_remote_count][copy] = '\0';
		s_remote_count++;
	}

	f_closedir(&dir);
}

/*============================================================================*/
/**
 * @brief  Text for manager list item idx (0 = the "new remote" action).
 */
static const char *ir_custom_item_text(uint16_t idx)
{
	if (idx == 0)
		return IR_CUSTOM_NEW_LABEL;
	return s_remote_names[idx - 1];
}

/*============================================================================*/
/**
 * @brief  Draw the My-Remotes manager list ("[+ New Remote]" then remotes).
 * @param  selection  highlighted item index
 * @param  total      total item count (remotes + 1)
 */
static void ir_custom_draw_list(uint16_t selection, uint16_t total)
{
	uint16_t start_idx;
	uint16_t visible;
	uint16_t i;
	uint8_t  y;

	if (selection < IR_CUSTOM_LIST_VISIBLE)
		start_idx = 0;
	else
		start_idx = selection - IR_CUSTOM_LIST_VISIBLE + 1;

	visible = total - start_idx;
	if (visible > IR_CUSTOM_LIST_VISIBLE)
		visible = IR_CUSTOM_LIST_VISIBLE;

	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 2, 10, "Create Remote");
	u8g2_DrawHLine(&m1_u8g2, 0, IR_CUSTOM_LIST_HEADER_H, 128);

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	for (i = 0; i < visible; i++)
	{
		uint16_t idx = start_idx + i;
		y = IR_CUSTOM_LIST_START_Y + (i * IR_CUSTOM_LIST_ITEM_H);

		if (idx == selection)
		{
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawBox(&m1_u8g2, 0, y, 128, IR_CUSTOM_LIST_ITEM_H);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 4, y + 8, ir_custom_item_text(idx));
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		}
		else
		{
			u8g2_DrawStr(&m1_u8g2, 4, y + 8, ir_custom_item_text(idx));
		}
	}

	{
		const char *action = (selection == 0) ? "New" : "Open";
		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", action, arrowright_8x8);
	}

	m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief  Custom-remote entry point ("Create Remote" Infrared submenu item).
 *         Shows a manager list: "[+ New Remote]" plus every user-built
 *         0:/IR/*.ir remote. New -> name+create; opening a remote runs the
 *         shared scrolling button-list replay screen (ir_replay_file). Learn
 *         and edit actions land in later slices.
 */
void ir_custom_run(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t       q_item;
	BaseType_t          ret;
	uint16_t            selection = 0;
	uint16_t            total;

	ir_custom_scan_remotes();
	total = (uint16_t)(s_remote_count + 1);   /* +1 for "[+ New Remote]" */
	ir_custom_draw_list(selection, total);

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
		    this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
		{
			xQueueReset(main_q_hdl);
			break;   /* return to the Infrared menu */
		}
		else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection > 0) ? (uint16_t)(selection - 1)
			                            : (uint16_t)(total - 1);
			ir_custom_draw_list(selection, total);
		}
		else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection < total - 1) ? (uint16_t)(selection + 1) : 0;
			ir_custom_draw_list(selection, total);
		}
		else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (selection == 0)
			{
				ir_custom_create_flow();
			}
			else
			{
				char path[IR_CUSTOM_PATH_MAX_LEN];
				snprintf(path, sizeof(path), "%s/%s.ir",
				         IR_CUSTOM_DIR, s_remote_names[selection - 1]);
				ir_replay_file(path);
			}

			/* A remote may have been created; re-scan and clamp selection. */
			ir_custom_scan_remotes();
			total = (uint16_t)(s_remote_count + 1);
			if (selection >= total)
				selection = (uint16_t)(total - 1);
			ir_custom_draw_list(selection, total);
		}
	}
} /* void ir_custom_run(void) */

#endif /* M1_APP_FILE_IMPORT_ENABLE */
