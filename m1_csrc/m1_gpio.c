/* See COPYING.txt for license details. */

/*
*
*  m1_gpio.c
*
*  M1 GPIO functions
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_gpio.h"
#include "m1_usb_cdc_msc.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"GPIO"

#define THIS_LCD_MENU_TEXT_FIRST_ROW_Y			11
#define THIS_LCD_MENU_TEXT_FRAME_FIRST_ROW_Y	1
#define THIS_LCD_MENU_TEXT_ROW_SPACE			10

/* Layout for the new 2-item scrollable menu */
#define THIS_GPIO_WINDOW_SIZE   2
#define THIS_GPIO_ROW_H         12
#define THIS_GPIO_ROW_X         6
#define THIS_GPIO_ROW_W         113
#define THIS_GPIO_LIST_TOP_Y    13
#define THIS_GPIO_SCROLLBAR_X   121
#define THIS_GPIO_SCROLLBAR_W   5

//************************** C O N S T A N T **********************************/

const char *m1_ext_gpio_label[M1_EXT_GPIO_LIST_N] = {	"Power 3.3V",
														"Power 5.0V",
														"",
														"Pin PE2",
														"Pin PE4",
														"Pin PE5",
														"Pin PE6",
														"Pin PD12",
														"Pin PD13",
														"Pin PA14",
														"Pin PA13",
														/*"Pin PA9",*/
														/*"Pin PA10",*/
														"Pin PC2",
														"Pin PC3",
														"Pin PD0",
														"Pin PD1"
													};

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

S_GPIO_IO_t m1_ext_gpio[M1_EXT_GPIO_LIST_N] = {	{.gpio_port = EN_EXT_3V3_GPIO_Port, .gpio_pin = EN_EXT_3V3_Pin},
												{.gpio_port = EN_EXT_5V_GPIO_Port, .gpio_pin = EN_EXT_5V_Pin},
												{.gpio_port = EN_EXT2_5V_GPIO_Port, .gpio_pin = EN_EXT2_5V_Pin},
												{.gpio_port = PE2_GPIO_Port, .gpio_pin = PE2_Pin},
												{.gpio_port = PE2_GPIO_Port, .gpio_pin = PE4_Pin},
												{.gpio_port = PE2_GPIO_Port, .gpio_pin = PE5_Pin},
												{.gpio_port = PE2_GPIO_Port, .gpio_pin = PE6_Pin},
												{.gpio_port = PD12_GPIO_Port, .gpio_pin = PD12_Pin},
												{.gpio_port = PD13_GPIO_Port, .gpio_pin = PD13_Pin},
												{.gpio_port = SWCLK_GPIO_Port, .gpio_pin = SWCLK_Pin},
												{.gpio_port = SWDIO_GPIO_Port, .gpio_pin = SWDIO_Pin},
												/*{.gpio_port = UART_1_TX_GPIO_Port, .gpio_pin = UART_1_TX_Pin},*/
												/*{.gpio_port = UART_1_RX_GPIO_Port, .gpio_pin = UART_1_RX_Pin},*/
												{.gpio_port = PC2_GPIO_Port, .gpio_pin = PC2_Pin},
												{.gpio_port = PC3_GPIO_Port, .gpio_pin = PC3_Pin},
												{.gpio_port = PD0_GPIO_Port, .gpio_pin = PD0_Pin},
												{.gpio_port = PD1_GPIO_Port, .gpio_pin = PD1_Pin}
											};

static uint8_t m1_ext_gpio_stat[M1_EXT_GPIO_LIST_N] = {0};
static uint8_t m1_ext_gpio_id = M1_EXT_GPIO_FIRST_ID; // Default to the first ext. GPIO [PE2_GPIO_Port, PE2_Pin]

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_gpio_init(void);
void menu_gpio_exit(void);

void gpio_manual_control(void);
void gpio_5v_on_gpio(void);
void gpio_3_3v_on_gpio(void);
void gpio_usb_uart_bridge(void);
void ext_power_5V_set(uint8_t set_mode);
void ext_power_3V_set(uint8_t set_mode);
void gpio_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item);
void gpio_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t sel_item);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/******************************************************************************/
/**
  * @brief Initializes display for this sub-menu item.
  * @param
  * @retval
  */
/******************************************************************************/
void menu_gpio_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t i;

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    for(i=0; i<M1_EXT_GPIO_LIST_N; i++)
    {
    	if ( i >= M1_EXT_GPIO_FIRST_ID ) // Do not reinitialize power control pins
    	{
    		GPIO_InitStruct.Pin = m1_ext_gpio[i].gpio_pin;
    		HAL_GPIO_Init(m1_ext_gpio[i].gpio_port, &GPIO_InitStruct);
    	}
    	HAL_GPIO_WritePin(m1_ext_gpio[i].gpio_port, m1_ext_gpio[i].gpio_pin, GPIO_PIN_RESET);
    	m1_ext_gpio_stat[i] = 0;
    }

    m1_ext_gpio_id = M1_EXT_GPIO_FIRST_ID; // Default to the first ext. GPIO [PE2_GPIO_Port, PE2_Pin]
} // void menu_gpio_init(void)


/******************************************************************************/
/**
  * @brief Exits this sub-menu and return to the upper level menu.
  * @param
  * @retval
  */
/******************************************************************************/
void menu_gpio_exit(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t i;

    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    for(i=M1_EXT_GPIO_FIRST_ID; i<M1_EXT_GPIO_LIST_N; i++)
    {
    	GPIO_InitStruct.Pin = m1_ext_gpio[i].gpio_pin;
    	HAL_GPIO_Init(m1_ext_gpio[i].gpio_port, &GPIO_InitStruct);
    }

    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate = GPIO_AF0_SWJ;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN; // Pulldown for SWCLK
    GPIO_InitStruct.Pin = SWCLK_Pin;
    HAL_GPIO_Init(SWCLK_GPIO_Port, &GPIO_InitStruct); // SWCLK
    GPIO_InitStruct.Pull = GPIO_PULLUP; // Pullup for SWDIO
    GPIO_InitStruct.Pin = SWDIO_Pin;
    HAL_GPIO_Init(SWDIO_GPIO_Port, &GPIO_InitStruct); // SWDIO

    for(i=0; i<M1_EXT_GPIO_FIRST_ID; i++) // Reset power control pins
    {
    	HAL_GPIO_WritePin(m1_ext_gpio[i].gpio_port, m1_ext_gpio[i].gpio_pin, GPIO_PIN_RESET);
    }
} // void menu_gpio_exit(void)



/******************************************************************************/
/**
  * @brief
  * @param
  * @retval
  */
/******************************************************************************/
void gpio_manual_control(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	uint8_t prn_name[GUI_DISP_LINE_LEN_MAX + 1] = {0};

    m1_ext_gpio_stat[m1_ext_gpio_id] ^= 1; // Toggle

    HAL_GPIO_WritePin(m1_ext_gpio[m1_ext_gpio_id].gpio_port, m1_ext_gpio[m1_ext_gpio_id].gpio_pin, m1_ext_gpio_stat[m1_ext_gpio_id]);

	sprintf(prn_name, "%s: %s", m1_ext_gpio_label[m1_ext_gpio_id], (m1_ext_gpio_stat[m1_ext_gpio_id]==1)?"ON":"OFF");
	m1_info_box_display_draw(INFO_BOX_ROW_1, prn_name);
	u8g2_NextPage(&m1_u8g2); // Update display RAM

	xQueueReset(main_q_hdl); // Reset main q before return
} // void gpio_manual_control(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void gpio_3_3v_on_gpio(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	uint8_t prn_name[GUI_DISP_LINE_LEN_MAX + 1] = {0};

    m1_ext_gpio_stat[0] ^= 1; // Toggle
    if ( m1_ext_gpio_stat[0] )
    {
    	m1_ext_gpio_stat[1] = 0; // 3.3V and 5.0V must not be turned ON at the same time
        HAL_GPIO_WritePin(m1_ext_gpio[1].gpio_port, m1_ext_gpio[1].gpio_pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(m1_ext_gpio[2].gpio_port, m1_ext_gpio[2].gpio_pin, GPIO_PIN_RESET);
    }

    HAL_GPIO_WritePin(m1_ext_gpio[0].gpio_port, m1_ext_gpio[0].gpio_pin, m1_ext_gpio_stat[0]);

	sprintf(prn_name, "%s: %s", m1_ext_gpio_label[0], (m1_ext_gpio_stat[0]==1)?"ON":"OFF");
	m1_info_box_display_draw(INFO_BOX_ROW_1, prn_name);
	u8g2_NextPage(&m1_u8g2); // Update display RAM

	xQueueReset(main_q_hdl); // Reset main q before return
} // void gpio_3_3v_on_gpio(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void gpio_5v_on_gpio(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	uint8_t prn_name[GUI_DISP_LINE_LEN_MAX + 1] = {0};

    m1_ext_gpio_stat[1] ^= 1; // Toggle
    if ( m1_ext_gpio_stat[1] )
    {
    	m1_ext_gpio_stat[0] = 0; // 3.3V and 5.0V must not be turned ON at the same time
    	HAL_GPIO_WritePin(m1_ext_gpio[0].gpio_port, m1_ext_gpio[0].gpio_pin, GPIO_PIN_RESET);
    }

    HAL_GPIO_WritePin(m1_ext_gpio[1].gpio_port, m1_ext_gpio[1].gpio_pin, m1_ext_gpio_stat[1]);
    HAL_GPIO_WritePin(m1_ext_gpio[2].gpio_port, m1_ext_gpio[2].gpio_pin, m1_ext_gpio_stat[1]);

	sprintf(prn_name, "%s: %s", m1_ext_gpio_label[1], (m1_ext_gpio_stat[1]==1)?"ON":"OFF");
	m1_info_box_display_draw(INFO_BOX_ROW_1, prn_name);
	u8g2_NextPage(&m1_u8g2); // Update display RAM

	xQueueReset(main_q_hdl); // Reset main q before return
} // void gpio_5v_on_gpio(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void gpio_usb_uart_bridge(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	enCdcMode prev_mode = m1_usbcdc_mode;
	uint32_t last_bitrate = 0;
	uint32_t last_tx = 0xFFFFFFFF;
	uint32_t last_rx = 0xFFFFFFFF;
	uint32_t last_draw_tick = 0;

	/* Reset traffic counters */
	bridge_tx_bytes = 0;
	bridge_rx_bytes = 0;

	/* Enable external 3.3V power to the target board */
	ext_power_3V_set(1);

	/* Switch to UART bridge routing mode */
	m1_usbcdc_mode = CDC_MODE_UART_BRIDGE;
	m1_usb_cdc_comconfig();

	bool running = true;
	while (running)
	{
		uint32_t now = HAL_GetTick();
		/* Redraw screen when host changes baud rate or data is transferred (rate-limited to 100ms) */
		if ((linecoding.bitrate != last_bitrate || bridge_tx_bytes != last_tx || bridge_rx_bytes != last_rx) &&
			(now - last_draw_tick >= 100 || linecoding.bitrate != last_bitrate))
		{
			last_bitrate = linecoding.bitrate;
			last_tx = bridge_tx_bytes;
			last_rx = bridge_rx_bytes;
			last_draw_tick = now;

			u8g2_FirstPage(&m1_u8g2);
			do {
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
				u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
				u8g2_DrawStr(&m1_u8g2, 2, 11, "USB-UART Bridge");

				u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
				char buf[32];
				snprintf(buf, sizeof(buf), "Baud: %lu bps", last_bitrate);
				u8g2_DrawStr(&m1_u8g2, 2, 23, buf);

				snprintf(buf, sizeof(buf), "TX: %lu B  RX: %lu B", last_tx, last_rx);
				u8g2_DrawStr(&m1_u8g2, 2, 35, buf);

				u8g2_DrawStr(&m1_u8g2, 2, 47, "Pins: 12 (TX) / 13 (RX)");
				u8g2_DrawStr(&m1_u8g2, 2, 59, "Press BACK to exit");
			} while (u8g2_NextPage(&m1_u8g2));
		}

		/* Poll input to check for exit */
		ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(50));
		if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
			{
				running = false;
			}
		}
	}

	/* Restore CDC mode and default console UART speed */
	m1_usbcdc_mode = prev_mode;
	linecoding.bitrate = 460800; // Restore default console baud rate
	m1_usb_cdc_comconfig();

	/* Power off external 3.3V */
	ext_power_3V_set(0);

	xQueueReset(main_q_hdl);
	m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT);
}

static uint8_t read_ext_pin_state(uint8_t physical_pin)
{
	switch (physical_pin)
	{
		case 1:  return (HAL_GPIO_ReadPin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin) == GPIO_PIN_SET) ? 1 : 0;
		case 2:  return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_2) == GPIO_PIN_SET) ? 1 : 0;
		case 3:  return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) == GPIO_PIN_SET) ? 1 : 0;
		case 4:  return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_5) == GPIO_PIN_SET) ? 1 : 0;
		case 5:  return (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_6) == GPIO_PIN_SET) ? 1 : 0;
		case 6:  return (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_12) == GPIO_PIN_SET) ? 1 : 0;
		case 7:  return (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_13) == GPIO_PIN_SET) ? 1 : 0;
		case 8:  return 0; // GND
		case 9:  return (HAL_GPIO_ReadPin(EN_EXT_3V3_GPIO_Port, EN_EXT_3V3_Pin) == GPIO_PIN_SET) ? 1 : 0;
		case 10: return (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_14) == GPIO_PIN_SET) ? 1 : 0;
		case 11: return (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_13) == GPIO_PIN_SET) ? 1 : 0;
		case 12: return (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_SET) ? 1 : 0;
		case 13: return (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10) == GPIO_PIN_SET) ? 1 : 0;
		case 14: return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_2) == GPIO_PIN_SET) ? 1 : 0;
		case 15: return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_3) == GPIO_PIN_SET) ? 1 : 0;
		case 16: return (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_0) == GPIO_PIN_SET) ? 1 : 0;
		case 17: return (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_1) == GPIO_PIN_SET) ? 1 : 0;
		case 18: return 0; // GND
		default: return 0;
	}
}

static void set_ext_pins_to_input(uint32_t pull)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = pull;

	/* PE2, PE4, PE5, PE6 */
	GPIO_InitStruct.Pin = PE2_Pin | PE4_Pin | PE5_Pin | PE6_Pin;
	HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

	/* PD12, PD13, PD0, PD1 */
	GPIO_InitStruct.Pin = PD12_Pin | PD13_Pin | PD0_Pin | PD1_Pin;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

	/* PC2, PC3 */
	GPIO_InitStruct.Pin = PC2_Pin | PC3_Pin;
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

void gpio_pin_map_monitor(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint32_t pull_mode = GPIO_PULLDOWN;
	bool running = true;

	/* Set testable pins to Input with weak pull-down by default */
	set_ext_pins_to_input(pull_mode);

	while (running)
	{
		u8g2_FirstPage(&m1_u8g2);
		do {
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_SetFont(&m1_u8g2, u8g2_font_u8glib_4_tr);

			/* Title + Pull Mode indicator */
			char title_str[40];
			snprintf(title_str, sizeof(title_str), "GPIO Monitor (%s)", (pull_mode == GPIO_PULLDOWN) ? "PD" : ((pull_mode == GPIO_PULLUP) ? "PU" : "Float"));
			u8g2_DrawStr(&m1_u8g2, 0, 6, title_str);

			/* Visual Layout dividers */
			u8g2_DrawHLine(&m1_u8g2, 0, 8, 128);
			u8g2_DrawVLine(&m1_u8g2, 63, 8, 56);

			/* Left Column (Pins 1-9) */
			static const char* left_labels[] = {
				"1:5.0V", "2:PE2", "3:PE4", "4:PE5", "5:PE6",
				"6:PD12", "7:PD13", "8:GND", "9:3.3V"
			};
			for (uint8_t i = 0; i < 9; i++)
			{
				uint8_t pin = i + 1;
				uint8_t y_val = 14 + i * 6;
				uint8_t state = read_ext_pin_state(pin);

				if (state)
					u8g2_DrawDisc(&m1_u8g2, 5, y_val - 2, 2, U8G2_DRAW_ALL);
				else
					u8g2_DrawCircle(&m1_u8g2, 5, y_val - 2, 2, U8G2_DRAW_ALL);

				u8g2_DrawStr(&m1_u8g2, 11, y_val, left_labels[i]);
			}

			/* Right Column (Pins 10-18) */
			static const char* right_labels[] = {
				"10:SWCLK", "11:SWDIO", "12:TX(PA9)", "13:RX(PA10)", "14:PC2",
				"15:PC3", "16:PD0", "17:PD1", "18:GND"
			};
			for (uint8_t i = 0; i < 9; i++)
			{
				uint8_t pin = 10 + i;
				uint8_t y_val = 14 + i * 6;
				uint8_t state = read_ext_pin_state(pin);

				if (state)
					u8g2_DrawDisc(&m1_u8g2, 68, y_val - 2, 2, U8G2_DRAW_ALL);
				else
					u8g2_DrawCircle(&m1_u8g2, 68, y_val - 2, 2, U8G2_DRAW_ALL);

				u8g2_DrawStr(&m1_u8g2, 74, y_val, right_labels[i]);
			}
		} while (u8g2_NextPage(&m1_u8g2));

		/* Non-blocking read */
		ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(100));
		if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
			{
				running = false;
			}
			else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
			{
				/* Toggle pull configuration */
				if (pull_mode == GPIO_PULLDOWN)
					pull_mode = GPIO_PULLUP;
				else if (pull_mode == GPIO_PULLUP)
					pull_mode = GPIO_NOPULL;
				else
					pull_mode = GPIO_PULLDOWN;

				set_ext_pins_to_input(pull_mode);
				m1_buzzer_set(BUZZER_FREQ_01_KHZ, 20);
			}
		}
	}

	/* Restore pins to output-low state */
	menu_gpio_init();

	xQueueReset(main_q_hdl);
	m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT);
}



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void ext_power_5V_set(uint8_t set_mode)
{
	HAL_GPIO_WritePin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin, set_mode);
	HAL_GPIO_WritePin(EN_EXT2_5V_GPIO_Port, EN_EXT2_5V_Pin, set_mode);
} // void ext_power_5V_set(uint8_t set_mode)


/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void ext_power_3V_set(uint8_t set_mode)
{
	  HAL_GPIO_WritePin(EN_EXT_3V3_GPIO_Port, EN_EXT_3V3_Pin, set_mode);
} // void ext_power_5V_set(uint8_t set_mode)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void gpio_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item)
{
	uint8_t n_items, row, top_row, bottom_row;
	uint8_t row_y, row_text_y;
	uint8_t prn_name[GUI_DISP_LINE_LEN_MAX + 1] = {0};

	n_items = phmenu->num_submenu_items;

	/* Scroll window: keep sel_item visible inside a window of size
	 * THIS_GPIO_WINDOW_SIZE (2). */
	if (n_items <= THIS_GPIO_WINDOW_SIZE)
		top_row = 0;
	else if (sel_item == 0)
		top_row = 0;
	else if (sel_item >= n_items - 1)
		top_row = n_items - THIS_GPIO_WINDOW_SIZE;
	else
		top_row = sel_item - (THIS_GPIO_WINDOW_SIZE - 1);

	bottom_row = top_row + THIS_GPIO_WINDOW_SIZE;
	if (bottom_row > n_items)
		bottom_row = n_items;

	/* Graphic work starts here */
	m1_u8g2_firstpage();
	do
	{
		/* Shared header (battery + SD) */
		m1_draw_header_bar(&m1_u8g2, "GPIO", NULL);

		/* Scrollable 2-row list */
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
		for (row = top_row; row < bottom_row; row++)
		{
			row_y = THIS_GPIO_LIST_TOP_Y + ((row - top_row) * THIS_GPIO_ROW_H);
			row_text_y = row_y + 9;

			if (row == sel_item)
			{
				u8g2_DrawBox(&m1_u8g2, THIS_GPIO_ROW_X, row_y,
				             THIS_GPIO_ROW_W, THIS_GPIO_ROW_H - 1);
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
				u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
				u8g2_DrawStr(&m1_u8g2, THIS_GPIO_ROW_X + 4, row_text_y,
				             phmenu->submenu[row]->title);
				if (row == 0) // GPIO Control — show pin-cycle arrows on the row
				{
					u8g2_DrawXBMP(&m1_u8g2, THIS_GPIO_ROW_X + 78, row_y + 1,
					              10, 10, arrowleft_10x10);
					u8g2_DrawXBMP(&m1_u8g2, THIS_GPIO_ROW_X + 91, row_y + 1,
					              10, 10, arrowright_10x10);
				}
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
				u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
			}
			else
			{
				u8g2_DrawFrame(&m1_u8g2, THIS_GPIO_ROW_X, row_y,
				               THIS_GPIO_ROW_W, THIS_GPIO_ROW_H - 1);
				u8g2_DrawStr(&m1_u8g2, THIS_GPIO_ROW_X + 4, row_text_y,
				             phmenu->submenu[row]->title);
			}
		}

		/* Scrollbar (only when there is more to scroll) */
		if (n_items > THIS_GPIO_WINDOW_SIZE)
		{
			uint8_t track_y = THIS_GPIO_LIST_TOP_Y;
			uint8_t track_h = (THIS_GPIO_WINDOW_SIZE * THIS_GPIO_ROW_H) - 1;
			uint8_t handle_h, handle_y;
			u8g2_DrawFrame(&m1_u8g2, THIS_GPIO_SCROLLBAR_X, track_y,
			               THIS_GPIO_SCROLLBAR_W, track_h);
			handle_h = (uint8_t)((track_h * THIS_GPIO_WINDOW_SIZE) / n_items);
			if (handle_h < 4) handle_h = 4;
			handle_y = track_y +
			           (uint8_t)(((track_h - handle_h) * top_row) /
			                     (n_items - THIS_GPIO_WINDOW_SIZE));
			u8g2_DrawBox(&m1_u8g2, THIS_GPIO_SCROLLBAR_X + 1, handle_y + 1,
			             THIS_GPIO_SCROLLBAR_W - 2, handle_h - 2);
		}

		/* Detail frame for currently-selected row */
		m1_draw_content_frame(&m1_u8g2, 2, 38, 124, 13);

		switch (sel_item)
		{
			case 0: // GPIO (cycled pin state)
				sprintf(prn_name, "%s: %s",
				        m1_ext_gpio_label[m1_ext_gpio_id],
				        (m1_ext_gpio_stat[m1_ext_gpio_id] == 1) ? "ON" : "OFF");
				m1_draw_text(&m1_u8g2, 6, 47, 116, prn_name, TEXT_ALIGN_LEFT);
				break;

			case 1: // Power 3.3V
				sprintf(prn_name, "%s: %s", m1_ext_gpio_label[0],
				        (m1_ext_gpio_stat[0] == 1) ? "ON" : "OFF");
				m1_draw_text(&m1_u8g2, 6, 47, 116, prn_name, TEXT_ALIGN_LEFT);
				break;

			case 2: // Power 5.0V
				sprintf(prn_name, "%s: %s", m1_ext_gpio_label[1],
				        (m1_ext_gpio_stat[1] == 1) ? "ON" : "OFF");
				m1_draw_text(&m1_u8g2, 6, 47, 116, prn_name, TEXT_ALIGN_LEFT);
				break;

			case 3:
				m1_draw_text(&m1_u8g2, 6, 47, 116, "Detect NFC & RFID fields",
				             TEXT_ALIGN_LEFT);
				break;

			case 4:
				m1_draw_text(&m1_u8g2, 6, 47, 116, "Bridge USB to UART1 (PA9/10)",
				             TEXT_ALIGN_LEFT);
				break;

			case 5:
				m1_draw_text(&m1_u8g2, 6, 47, 116, "Live map of physical pin states",
				             TEXT_ALIGN_LEFT);
				break;

			default:
				break;
		}

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Toggle",
		                   arrowright_8x8);
	} while (m1_u8g2_nextpage());
} // void gpio_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void gpio_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t sel_item)
{
	uint8_t prn_name[GUI_DISP_LINE_LEN_MAX + 1] = {0};

	if ( sel_item != 0) // Not the index of GPIO Control
		return;

	if ( event==BUTTON_EVENT_CLICK )
	{
		if ( button_id==BUTTON_LEFT_KP_ID ) // Left arrow key
		{
			m1_ext_gpio_id--;
			if ( m1_ext_gpio_id < M1_EXT_GPIO_FIRST_ID )
				m1_ext_gpio_id = M1_EXT_GPIO_LIST_N - 1;
		} // if ( button_id==BUTTON_LEFT_KP_ID )
		else if ( button_id==BUTTON_RIGHT_KP_ID ) // Right arrow key
		{
			m1_ext_gpio_id++;
			if ( m1_ext_gpio_id >= M1_EXT_GPIO_LIST_N )
				m1_ext_gpio_id = M1_EXT_GPIO_FIRST_ID;
		}

		sprintf(prn_name, "%s: %s", m1_ext_gpio_label[m1_ext_gpio_id], (m1_ext_gpio_stat[m1_ext_gpio_id]==1)?"ON":"OFF");
    	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    	u8g2_DrawBox(&m1_u8g2, 4, 45, 120, 15);
    	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		m1_draw_text(&m1_u8g2, 8, 55, 114, prn_name, TEXT_ALIGN_LEFT);

		m1_u8g2_nextpage(); // Update LCD display RAM
	} // if ( event==BUTTON_EVENT_CLICK )
} // void gpio_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t)
