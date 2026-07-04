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
#include "flipper_ir.h"

/*************************** D E F I N E S ************************************/

/* Root directory for user-built custom remotes on the SD card. */
#define IR_CUSTOM_DIR    "0:/IR"

//****************************** V A R I A B L E S *****************************/

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void ir_custom_draw_placeholder(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief  Draw the (placeholder) Create Remote screen.
 */
static void ir_custom_draw_placeholder(void)
{
	m1_u8g2_firstpage();

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	/* Title bar */
	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 2, 10, "Create Remote");
	u8g2_DrawHLine(&m1_u8g2, 0, 12, 128);

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	u8g2_DrawStr(&m1_u8g2, 4, 30, "Coming soon");

	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "", arrowright_8x8);

	m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief  Custom-remote entry point ("Create Remote" Infrared submenu item).
 *         Placeholder skeleton: draws a screen and returns to the menu on
 *         BACK/LEFT. The create/learn/replay/edit flows land in later slices.
 */
void ir_custom_run(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t       q_item;
	BaseType_t          ret;

	ir_custom_draw_placeholder();

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
			break; /* Exit to caller */
		}
	}
} /* void ir_custom_run(void) */

#endif /* M1_APP_FILE_IMPORT_ENABLE */
