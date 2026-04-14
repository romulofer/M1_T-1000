/* See COPYING.txt for license details. */

/*
*
* app_esp32_link.c
*
* ESP32 SPI/AT diagnostics utility app
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m1_builtin_apps.h"
#include "m1_games.h"
#include "m1_esp32_hal.h"
#include "esp_app_main.h"
#include "esp_at_list.h"

/*************************** D E F I N E S ************************************/

#define ESP32_LINK_PAGE_COUNT       3U
#define ESP32_LINK_ACTION_COUNT     8U
#define ESP32_LINK_STATUS_COUNT     6U
#define ESP32_LINK_POLL_MS          120U
#define ESP32_LINK_VISIBLE_ACTIONS  2U
#define ESP32_LINK_VISIBLE_STATUS   4U
#define ESP32_LINK_LOG_WIDTH        20U
#define ESP32_LINK_LOG_LINES        8U
#define ESP32_LINK_VISIBLE_LOG      2U

//************************** S T R U C T U R E S *******************************/

typedef enum
{
    ESP32_LINK_PAGE_STATUS = 0,
    ESP32_LINK_PAGE_ACTIONS,
    ESP32_LINK_PAGE_LOG
} esp32_link_page_t;

typedef enum
{
    ESP32_LINK_ACT_INIT_HAL = 0,
    ESP32_LINK_ACT_INIT_AT,
    ESP32_LINK_ACT_AT_PING,
    ESP32_LINK_ACT_GET_VERSION,
    ESP32_LINK_ACT_WIFI_MODE,
    ESP32_LINK_ACT_WIFI_STATS,
    ESP32_LINK_ACT_SCAN_APS,
    ESP32_LINK_ACT_RESET
} esp32_link_action_t;

typedef struct
{
    char last_action[20];
    char last_status[20];
    char last_response[96];
    uint8_t last_rc;
    uint16_t runs;
} esp32_link_state_t;

/***************************** V A R I A B L E S ******************************/

static const char *esp32_link_action_labels[ESP32_LINK_ACTION_COUNT] = {
    "Init HAL",
    "Init AT Link",
    "AT Ping",
    "Get Version",
    "WiFi Mode?",
    "WiFi Stats",
    "Scan APs",
    "Reset ESP32"
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void esp32_link_set_result(esp32_link_state_t *state,
                                  const char *action,
                                  const char *status,
                                  uint8_t rc,
                                  const char *response);
bool get_esp32_ready_status(void);
static void esp32_link_sanitize_text(const char *src, char *dst, size_t dst_len);
static uint8_t esp32_link_wrap_lines(const char *src,
                                     char lines[][ESP32_LINK_LOG_WIDTH + 1U],
                                     uint8_t max_lines,
                                     size_t line_len);
static void esp32_link_draw_status_page(const esp32_link_state_t *state, uint8_t status_offset);
static void esp32_link_draw_actions_page(uint8_t selected);
static void esp32_link_draw_log_page(const esp32_link_state_t *state, uint8_t log_offset);
static void esp32_link_run_action(esp32_link_state_t *state, esp32_link_action_t action);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static void esp32_link_set_result(esp32_link_state_t *state,
                                  const char *action,
                                  const char *status,
                                  uint8_t rc,
                                  const char *response)
{
    if (state == NULL)
    {
        return;
    }

    snprintf(state->last_action, sizeof(state->last_action), "%s", action ? action : "Idle");
    snprintf(state->last_status, sizeof(state->last_status), "%s", status ? status : "Idle");
    state->last_rc = rc;
    state->runs++;
    esp32_link_sanitize_text(response ? response : "No response", state->last_response, sizeof(state->last_response));
}


static void esp32_link_sanitize_text(const char *src, char *dst, size_t dst_len)
{
    size_t si = 0;
    size_t di = 0;
    bool last_was_space = true;

    if (dst == NULL || dst_len == 0U)
    {
        return;
    }

    dst[0] = '\0';
    if (src == NULL)
    {
        return;
    }

    while (src[si] != '\0' && di < (dst_len - 1U))
    {
        char ch = src[si++];
        bool is_space = (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ');

        if (is_space)
        {
            if (!last_was_space)
            {
                dst[di++] = ' ';
                last_was_space = true;
            }
            continue;
        }

        dst[di++] = ch;
        last_was_space = false;
    }

    while (di > 0U && dst[di - 1U] == ' ')
    {
        di--;
    }
    dst[di] = '\0';
}


static uint8_t esp32_link_wrap_lines(const char *src,
                                     char lines[][ESP32_LINK_LOG_WIDTH + 1U],
                                     uint8_t max_lines,
                                     size_t line_len)
{
    const char *cursor = src;
    uint8_t used_lines = 0U;

    for (uint8_t i = 0; i < max_lines; i++)
    {
        size_t len = 0;
        size_t last_space = 0;

        if (line_len > 0U)
        {
            lines[i][0] = '\0';
        }
        if (cursor == NULL || *cursor == '\0' || line_len == 0U)
        {
            continue;
        }

        while (*cursor == ' ')
        {
            cursor++;
        }

        while (cursor[len] != '\0' && len < line_len)
        {
            if (cursor[len] == ' ')
            {
                last_space = len;
            }
            len++;
        }

        if (cursor[len] != '\0' && last_space > 0U)
        {
            len = last_space;
        }

        memcpy(lines[i], cursor, len);
        lines[i][len] = '\0';
        used_lines++;

        cursor += len;
        while (*cursor == ' ')
        {
            cursor++;
        }
    }

    if (used_lines == 0U)
    {
        snprintf(lines[0], line_len + 1U, "%s", "No response");
        used_lines = 1U;
    }

    return used_lines;
}


static void esp32_link_draw_status_page(const esp32_link_state_t *state, uint8_t status_offset)
{
    char badge[8];
    char status_lines[ESP32_LINK_STATUS_COUNT][32];
    uint8_t end_line;
    static const uint8_t line_y[ESP32_LINK_VISIBLE_STATUS] = {22U, 30U, 38U, 46U};

    snprintf(badge, sizeof(badge), "1/%u", (unsigned)ESP32_LINK_PAGE_COUNT);
    snprintf(status_lines[0], sizeof(status_lines[0]), "HAL: %s",
             m1_esp32_get_init_status() ? "Ready" : "Off");
    snprintf(status_lines[1], sizeof(status_lines[1]), "AT: %s",
             get_esp32_main_init_status() ? "Ready" : "Idle");
    snprintf(status_lines[2], sizeof(status_lines[2]), "Host: %s",
             get_esp32_ready_status() ? "Up" : "Wait");
    snprintf(status_lines[3], sizeof(status_lines[3]), "Last: %s",
             state->last_action[0] ? state->last_action : "Idle");
    snprintf(status_lines[4], sizeof(status_lines[4]), "State: %s",
             state->last_status[0] ? state->last_status : "Idle");
    snprintf(status_lines[5], sizeof(status_lines[5]), "rc=%u  runs=%u",
             (unsigned)state->last_rc,
             (unsigned)state->runs);

    if (status_offset >= ESP32_LINK_STATUS_COUNT)
    {
        status_offset = 0U;
    }
    end_line = (uint8_t)(status_offset + ESP32_LINK_VISIBLE_STATUS - 1U);
    if (end_line >= ESP32_LINK_STATUS_COUNT)
    {
        end_line = (uint8_t)(ESP32_LINK_STATUS_COUNT - 1U);
    }

    m1_u8g2_firstpage();
    m1_draw_header_bar(&m1_u8g2, "ESP32 Link", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 36);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    for (uint8_t i = 0U; i < ESP32_LINK_VISIBLE_STATUS; i++)
    {
        uint8_t line_idx = (uint8_t)(status_offset + i);

        if (line_idx >= ESP32_LINK_STATUS_COUNT)
        {
            break;
        }
        m1_draw_text(&m1_u8g2, 8, line_y[i], 114, status_lines[line_idx], TEXT_ALIGN_LEFT);
    }
    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Tests", arrowright_8x8);
    m1_u8g2_nextpage();
}


static void esp32_link_draw_actions_page(uint8_t selected)
{
    char badge[8];
    char info[18];
    uint8_t first_item;
    uint8_t visible_count;
    uint8_t y = 34U;

    snprintf(badge, sizeof(badge), "2/%u", (unsigned)ESP32_LINK_PAGE_COUNT);

    if (selected >= (ESP32_LINK_ACTION_COUNT - 1U))
    {
        first_item = (uint8_t)(ESP32_LINK_ACTION_COUNT - ESP32_LINK_VISIBLE_ACTIONS);
    }
    else
    {
        first_item = selected;
    }
    visible_count = ESP32_LINK_VISIBLE_ACTIONS;
    snprintf(info, sizeof(info), "%u-%u/%u",
             (unsigned)(first_item + 1U),
             (unsigned)(first_item + visible_count),
             (unsigned)ESP32_LINK_ACTION_COUNT);

    m1_u8g2_firstpage();
    m1_draw_header_bar(&m1_u8g2, "ESP32 Link", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 36);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_text(&m1_u8g2, 8, 22, 114, info, TEXT_ALIGN_LEFT);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    for (uint8_t idx = 0; idx < visible_count; idx++)
    {
        uint8_t item = (uint8_t)(first_item + idx);

        if (item == selected)
        {
            u8g2_DrawBox(&m1_u8g2, 6, (u8g2_uint_t)(y - 7U), 114, 11);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
            m1_draw_text(&m1_u8g2, 10, y, 108, esp32_link_action_labels[item], TEXT_ALIGN_LEFT);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        }
        else
        {
            u8g2_DrawFrame(&m1_u8g2, 6, (u8g2_uint_t)(y - 7U), 114, 11);
            m1_draw_text(&m1_u8g2, 10, y, 108, esp32_link_action_labels[item], TEXT_ALIGN_LEFT);
        }
        y = (uint8_t)(y + 12U);
    }

    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Status", "Run", arrowright_8x8);
    m1_u8g2_nextpage();
}


static void esp32_link_draw_log_page(const esp32_link_state_t *state, uint8_t log_offset)
{
    char badge[8];
    char status_line[28];
    char lines[ESP32_LINK_LOG_LINES][ESP32_LINK_LOG_WIDTH + 1U];
    uint8_t line_count;
    uint8_t end_line;

    line_count = esp32_link_wrap_lines(state->last_response, lines, ESP32_LINK_LOG_LINES, ESP32_LINK_LOG_WIDTH);
    if (log_offset >= line_count)
    {
        log_offset = 0U;
    }
    end_line = (uint8_t)(log_offset + ESP32_LINK_VISIBLE_LOG - 1U);
    if (end_line >= line_count)
    {
        end_line = (uint8_t)(line_count - 1U);
    }

    snprintf(badge, sizeof(badge), "%u-%u",
             (unsigned)(log_offset + 1U),
             (unsigned)(end_line + 1U));
    snprintf(status_line, sizeof(status_line), "%s  rc=%u",
             state->last_status[0] ? state->last_status : "Idle",
             (unsigned)state->last_rc);

    m1_u8g2_firstpage();
    m1_draw_header_bar(&m1_u8g2, "ESP32 Link", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 36);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_text(&m1_u8g2, 8, 22, 114, state->last_action[0] ? state->last_action : "No command run", TEXT_ALIGN_LEFT);
    m1_draw_text(&m1_u8g2, 8, 31, 114, status_line, TEXT_ALIGN_LEFT);
    m1_draw_text(&m1_u8g2, 8, 40, 114, lines[log_offset], TEXT_ALIGN_LEFT);
    m1_draw_text(&m1_u8g2, 8, 49, 114,
                 lines[(log_offset + 1U < line_count) ? (log_offset + 1U) : log_offset],
                 TEXT_ALIGN_LEFT);
    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Tests", "Status", arrowright_8x8);
    m1_u8g2_nextpage();
}


static void esp32_link_run_action(esp32_link_state_t *state, esp32_link_action_t action)
{
    ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
    char resp[512];
    uint8_t ret = ERROR;

    app_req.cmd_timeout_sec = 12;
    resp[0] = '\0';

    switch (action)
    {
        case ESP32_LINK_ACT_INIT_HAL:
            if (!m1_esp32_get_init_status())
            {
                m1_esp32_init();
            }
            esp32_link_set_result(state,
                                  "Init HAL",
                                  m1_esp32_get_init_status() ? "HAL ready" : "HAL failed",
                                  m1_esp32_get_init_status() ? SUCCESS : ERROR,
                                  m1_esp32_get_init_status() ?
                                      "STM32 SPI host and ESP32 power path are up." :
                                      "HAL init did not stick.");
            break;

        case ESP32_LINK_ACT_INIT_AT:
            if (!m1_esp32_get_init_status())
            {
                m1_esp32_init();
            }
            if (m1_esp32_get_init_status() && !get_esp32_main_init_status())
            {
                esp32_main_init();
            }
            esp32_link_set_result(state,
                                  "Init AT",
                                  get_esp32_main_init_status() ? "AT ready" : "AT failed",
                                  get_esp32_main_init_status() ? SUCCESS : ERROR,
                                  get_esp32_main_init_status() ?
                                      "SPI AT transport is ready for commands." :
                                      "Main init did not reach ready state.");
            break;

        case ESP32_LINK_ACT_AT_PING:
            if (!m1_esp32_get_init_status())
            {
                m1_esp32_init();
            }
            if (m1_esp32_get_init_status() && !get_esp32_main_init_status())
            {
                esp32_main_init();
            }
            ret = spi_AT_send_recv("AT\r\n", resp, sizeof(resp), 3);
            esp32_link_set_result(state,
                                  "AT Ping",
                                  (ret == SUCCESS) ? "AT OK" : "AT failed",
                                  ret,
                                  (ret == SUCCESS) ? resp : (resp[0] ? resp : "No reply"));
            break;

        case ESP32_LINK_ACT_GET_VERSION:
            if (!m1_esp32_get_init_status())
            {
                m1_esp32_init();
            }
            if (m1_esp32_get_init_status() && !get_esp32_main_init_status())
            {
                esp32_main_init();
            }
            ret = esp_get_version(&app_req);
            esp32_link_set_result(state,
                                  "Get Version",
                                  (ret == SUCCESS) ? "Version OK" : "Version fail",
                                  ret,
                                  (ret == SUCCESS && app_req.u.wifi_ap_config.status[0]) ?
                                      app_req.u.wifi_ap_config.status :
                                      "Version query failed.");
            break;

        case ESP32_LINK_ACT_WIFI_MODE:
            if (!m1_esp32_get_init_status())
            {
                m1_esp32_init();
            }
            if (m1_esp32_get_init_status() && !get_esp32_main_init_status())
            {
                esp32_main_init();
            }
            app_req.cmd_timeout_sec = 8;
            ret = wifi_get_mode(&app_req);
            esp32_link_set_result(state,
                                  "WiFi Mode?",
                                  (ret == SUCCESS) ? "Mode OK" : "Mode fail",
                                  ret,
                                  (ret == SUCCESS && app_req.u.wifi_ap_config.status[0]) ?
                                      app_req.u.wifi_ap_config.status :
                                      "Mode query failed.");
            break;

        case ESP32_LINK_ACT_WIFI_STATS:
            if (!m1_esp32_get_init_status())
            {
                m1_esp32_init();
            }
            if (m1_esp32_get_init_status() && !get_esp32_main_init_status())
            {
                esp32_main_init();
            }
            app_req.cmd_timeout_sec = 8;
            ret = wifi_get_stats(&app_req);
            if (ret == SUCCESS)
            {
                snprintf(resp, sizeof(resp), "%s %s %d dBm ch%d ip %s bssid %s",
                         (app_req.u.wifi_ap_config.band_mode != 0) ? "Connected" : "Idle",
                         app_req.u.wifi_ap_config.status[0] ? app_req.u.wifi_ap_config.status : "UNK",
                         app_req.u.wifi_ap_config.rssi,
                         app_req.u.wifi_ap_config.channel,
                         app_req.u.wifi_ap_config.out_mac[0] ? app_req.u.wifi_ap_config.out_mac : "0.0.0.0",
                         app_req.u.wifi_ap_config.bssid[0] ? (const char *)app_req.u.wifi_ap_config.bssid : "none");
            }
            esp32_link_set_result(state,
                                  "WiFi Stats",
                                  (ret == SUCCESS) ? "Stats OK" : "Stats fail",
                                  ret,
                                  (ret == SUCCESS) ? resp : "Stats query failed.");
            break;

        case ESP32_LINK_ACT_SCAN_APS:
        {
            const char *first_ssid = "<hidden>";

            if (!m1_esp32_get_init_status())
            {
                m1_esp32_init();
            }
            if (m1_esp32_get_init_status() && !get_esp32_main_init_status())
            {
                esp32_main_init();
            }
            app_req.msg_type = CTRL_REQ;
            app_req.resp_event_status = ERROR;
            app_req.msg_id = CTRL_RESP_GET_AP_SCAN_LIST;
            app_req.cmd_timeout_sec = 30;
            ret = wifi_ap_scan_list(&app_req);
            if (ret == SUCCESS)
            {
                if (app_req.u.wifi_ap_scan.count > 0 && app_req.u.wifi_ap_scan.out_list != NULL)
                {
                    if (app_req.u.wifi_ap_scan.out_list[0].ssid[0] != '\0')
                    {
                        first_ssid = (const char *)app_req.u.wifi_ap_scan.out_list[0].ssid;
                    }
                    snprintf(resp, sizeof(resp), "%u APs. First: %s",
                             (unsigned)app_req.u.wifi_ap_scan.count,
                             first_ssid);
                }
                else
                {
                    snprintf(resp, sizeof(resp), "Scan OK but no APs found.");
                }
            }
            else
            {
                snprintf(resp, sizeof(resp), "WiFi scan failed.");
            }
            esp32_link_set_result(state,
                                  "Scan APs",
                                  (ret == SUCCESS) ? "Scan OK" : "Scan fail",
                                  ret,
                                  resp);
            if (app_req.u.wifi_ap_scan.out_list != NULL)
            {
                free(app_req.u.wifi_ap_scan.out_list);
                app_req.u.wifi_ap_scan.out_list = NULL;
            }
            break;
        }

        case ESP32_LINK_ACT_RESET:
            ret = esp_dev_reset(&app_req);
            esp32_main_force_reinit();
            esp32_link_set_result(state,
                                  "Reset ESP32",
                                  (ret == SUCCESS) ? "Reset sent" : "Reset fail",
                                  ret,
                                  (ret == SUCCESS) ?
                                      "Reset accepted. Run Init AT after ready." :
                                      "Reset command failed.");
            break;

        default:
            esp32_link_set_result(state, "ESP32 Link", "Unknown", ERROR, "Unsupported action.");
            break;
    }
}


void app_esp32_link_run(void)
{
    esp32_link_state_t state;
    esp32_link_page_t page = ESP32_LINK_PAGE_STATUS;
    uint8_t selected = 0U;
    uint8_t status_offset = 0U;
    uint8_t log_offset = 0U;
    game_button_t btn;

    memset(&state, 0, sizeof(state));
    esp32_link_set_result(&state, "ESP32 Link", "Idle", SUCCESS, "Use Tests to exercise the SPI AT link.");

    for (;;)
    {
        switch (page)
        {
            case ESP32_LINK_PAGE_STATUS:
                esp32_link_draw_status_page(&state, status_offset);
                break;
            case ESP32_LINK_PAGE_ACTIONS:
                esp32_link_draw_actions_page(selected);
                break;
            case ESP32_LINK_PAGE_LOG:
            default:
                esp32_link_draw_log_page(&state, log_offset);
                break;
        }

        btn = game_poll_button(ESP32_LINK_POLL_MS);
        if (btn == GAME_BTN_BACK)
        {
            if (page == ESP32_LINK_PAGE_STATUS)
            {
                return;
            }
            page = (esp32_link_page_t)(page - 1U);
            continue;
        }

        if (page == ESP32_LINK_PAGE_ACTIONS)
        {
            if (btn == GAME_BTN_UP)
            {
                selected = (selected == 0U) ? (ESP32_LINK_ACTION_COUNT - 1U) : (uint8_t)(selected - 1U);
            }
            else if (btn == GAME_BTN_DOWN)
            {
                selected = (uint8_t)((selected + 1U) % ESP32_LINK_ACTION_COUNT);
            }
            else if (btn == GAME_BTN_OK)
            {
                esp32_link_set_result(&state, esp32_link_action_labels[selected], "Running", SUCCESS, "Executing...");
                esp32_link_run_action(&state, (esp32_link_action_t)selected);
                log_offset = 0U;
                page = ESP32_LINK_PAGE_LOG;
            }
            else if (btn == GAME_BTN_LEFT)
            {
                page = ESP32_LINK_PAGE_STATUS;
            }
            else if (btn == GAME_BTN_RIGHT)
            {
                page = ESP32_LINK_PAGE_LOG;
            }
        }
        else if (page == ESP32_LINK_PAGE_STATUS)
        {
            if (btn == GAME_BTN_UP)
            {
                status_offset = (status_offset == 0U) ?
                    (ESP32_LINK_STATUS_COUNT - ESP32_LINK_VISIBLE_STATUS) :
                    (uint8_t)(status_offset - 1U);
            }
            else if (btn == GAME_BTN_DOWN)
            {
                status_offset = (uint8_t)((status_offset + 1U) % (ESP32_LINK_STATUS_COUNT - ESP32_LINK_VISIBLE_STATUS + 1U));
            }
            else if (btn == GAME_BTN_RIGHT || btn == GAME_BTN_OK)
            {
                page = ESP32_LINK_PAGE_ACTIONS;
            }
        }
        else
        {
            if (btn == GAME_BTN_UP)
            {
                if (log_offset > 0U)
                {
                    log_offset--;
                }
            }
            else if (btn == GAME_BTN_DOWN)
            {
                log_offset++;
            }
            else if (btn == GAME_BTN_RIGHT || btn == GAME_BTN_OK)
            {
                page = ESP32_LINK_PAGE_STATUS;
            }
            else if (btn == GAME_BTN_LEFT)
            {
                page = ESP32_LINK_PAGE_ACTIONS;
            }
        }
    }
}
