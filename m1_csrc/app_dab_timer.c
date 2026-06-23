/* See COPYING.txt for license details. */

/*
*
* app_dab_timer.c
*
* Dab Timer utility app
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "m1_builtin_apps.h"
#include "m1_games.h"
#include "m1_buzzer.h"

/*************************** D E F I N E S ************************************/

#define DAB_TIMER_DEFAULT_SEC      45U
#define DAB_TIMER_MIN_SEC          10U
#define DAB_TIMER_MAX_SEC          180U
#define DAB_TIMER_STEP_SMALL_SEC   5U
#define DAB_TIMER_STEP_LARGE_SEC   15U
#define DAB_TIMER_POLL_MS          100U
#define DAB_TIMER_ALERT_GAP_MS     300U
#define DAB_TIMER_ALERT_BEEPS      4U

//************************** S T R U C T U R E S *******************************/

typedef enum
{
    DAB_TIMER_IDLE = 0,
    DAB_TIMER_RUNNING,
    DAB_TIMER_PAUSED,
    DAB_TIMER_ALERT
} dab_timer_mode_t;

typedef struct
{
    uint16_t duration_sec;
    uint32_t remaining_ms;
    uint32_t deadline_ms;
    uint32_t last_alert_ms;
    uint8_t alert_count;
    uint16_t sessions;
    dab_timer_mode_t mode;
} dab_timer_state_t;

/***************************** V A R I A B L E S ******************************/

static dab_timer_state_t g_dab_timer;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static uint16_t dab_timer_clamp_duration(int32_t value);
static void dab_timer_reset_countdown(dab_timer_state_t *st);
static void dab_timer_adjust(dab_timer_state_t *st, int16_t delta_sec);
static void dab_timer_sync(dab_timer_state_t *st);
static void dab_timer_handle_alert(dab_timer_state_t *st);
static void dab_timer_draw(const dab_timer_state_t *st);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static uint16_t dab_timer_clamp_duration(int32_t value)
{
    if (value < (int32_t)DAB_TIMER_MIN_SEC)
    {
        return DAB_TIMER_MIN_SEC;
    }
    if (value > (int32_t)DAB_TIMER_MAX_SEC)
    {
        return DAB_TIMER_MAX_SEC;
    }
    return (uint16_t)value;
}


static void dab_timer_reset_countdown(dab_timer_state_t *st)
{
    st->remaining_ms = (uint32_t)st->duration_sec * 1000U;
    st->alert_count = 0;
    st->last_alert_ms = 0U;
}


static void dab_timer_adjust(dab_timer_state_t *st, int16_t delta_sec)
{
    st->duration_sec = dab_timer_clamp_duration((int32_t)st->duration_sec + delta_sec);
    st->mode = DAB_TIMER_IDLE;
    dab_timer_reset_countdown(st);
}


static void dab_timer_sync(dab_timer_state_t *st)
{
    uint32_t now;

    if (st->mode != DAB_TIMER_RUNNING)
    {
        return;
    }

    now = HAL_GetTick();
    if (now >= st->deadline_ms)
    {
        st->remaining_ms = 0U;
        st->mode = DAB_TIMER_ALERT;
        st->alert_count = 1U;
        st->last_alert_ms = now;
        if (st->sessions < 999U)
        {
            st->sessions++;
        }
        m1_buzzer_set(BUZZER_FREQ_02_KHZ, 120);
        return;
    }

    st->remaining_ms = st->deadline_ms - now;
}


static void dab_timer_handle_alert(dab_timer_state_t *st)
{
    uint32_t now;

    if (st->mode != DAB_TIMER_ALERT)
    {
        return;
    }

    now = HAL_GetTick();
    if (st->alert_count >= DAB_TIMER_ALERT_BEEPS)
    {
        return;
    }

    if (st->last_alert_ms != 0U && (now - st->last_alert_ms) < DAB_TIMER_ALERT_GAP_MS)
    {
        return;
    }

    m1_buzzer_set((st->alert_count & 1U) ? BUZZER_FREQ_04_KHZ : BUZZER_FREQ_02_KHZ, 90);
    st->last_alert_ms = now;
    st->alert_count++;
}


static void dab_timer_draw(const dab_timer_state_t *st)
{
    char time_buf[8];
    char status_buf[20];
    char progress_buf[24];
    uint16_t seconds_left;
    uint8_t minutes;
    uint8_t seconds;
    uint32_t total_ms;
    uint16_t fill_w = 0U;
    uint8_t status_y = 22U;
    bool flash = false;
    uint8_t text_color = M1_DISP_DRAW_COLOR_TXT;

    seconds_left = (uint16_t)((st->remaining_ms + 999U) / 1000U);
    minutes = (uint8_t)(seconds_left / 60U);
    seconds = (uint8_t)(seconds_left % 60U);
    snprintf(time_buf, sizeof(time_buf), "%02u:%02u", minutes, seconds);

    switch (st->mode)
    {
        case DAB_TIMER_RUNNING:
            snprintf(status_buf, sizeof(status_buf), "Running %us", st->duration_sec);
            break;
        case DAB_TIMER_PAUSED:
            snprintf(status_buf, sizeof(status_buf), "Paused at %us", st->duration_sec);
            break;
        case DAB_TIMER_ALERT:
            snprintf(status_buf, sizeof(status_buf), "DAB READY #%u", st->sessions);
            flash = ((HAL_GetTick() / 250U) & 1U) != 0U;
            break;
        case DAB_TIMER_IDLE:
        default:
            if (st->sessions > 0U)
            {
                snprintf(status_buf, sizeof(status_buf), "Set %us #%u", st->duration_sec, st->sessions);
            }
            else
            {
                snprintf(status_buf, sizeof(status_buf), "Set time %us", st->duration_sec);
            }
            break;
    }

    total_ms = (uint32_t)st->duration_sec * 1000U;
    if (total_ms > 0U && st->remaining_ms <= total_ms)
    {
        fill_w = (uint16_t)((96U * (total_ms - st->remaining_ms)) / total_ms);
    }

    m1_u8g2_firstpage();

    if (flash)
    {
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_DrawBox(&m1_u8g2, 0, 0, 128, 64);
        text_color = M1_DISP_DRAW_COLOR_BG;
    }

    u8g2_SetDrawColor(&m1_u8g2, text_color);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, 24, 12, "DAB TIMER");

    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    u8g2_DrawStr(&m1_u8g2, (128 - u8g2_GetStrWidth(&m1_u8g2, status_buf)) / 2, status_y, status_buf);

    u8g2_SetFont(&m1_u8g2, M1_DISP_LARGE_FONT_2B);
    u8g2_DrawStr(&m1_u8g2, (128 - u8g2_GetStrWidth(&m1_u8g2, time_buf)) / 2, 44, time_buf);

    u8g2_SetDrawColor(&m1_u8g2, text_color);
    u8g2_DrawFrame(&m1_u8g2, 16, 48, 96, 8);
    if (fill_w > 0U)
    {
        u8g2_DrawBox(&m1_u8g2, 18, 50, fill_w > 92U ? 92U : fill_w, 4);
    }

    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    if (st->mode == DAB_TIMER_RUNNING)
    {
        snprintf(progress_buf, sizeof(progress_buf), "OK Pause");
        u8g2_DrawStr(&m1_u8g2, 0, 63, progress_buf);
        u8g2_DrawStr(&m1_u8g2, 70, 63, "BACK Exit");
    }
    else if (st->mode == DAB_TIMER_ALERT)
    {
        u8g2_DrawStr(&m1_u8g2, 0, 63, "OK Reset");
        u8g2_DrawStr(&m1_u8g2, 68, 63, "BACK Exit");
    }
    else
    {
        u8g2_DrawStr(&m1_u8g2, 0, 55, "L/R -/+5");
        u8g2_DrawStr(&m1_u8g2, 72, 55, "U/D 15");
        u8g2_DrawStr(&m1_u8g2, 0, 63, "OK Start");
        u8g2_DrawStr(&m1_u8g2, 68, 63, "BACK Exit");
    }

    m1_u8g2_nextpage();
}


void app_dab_timer_run(void)
{
    game_button_t btn;

    g_dab_timer.duration_sec = DAB_TIMER_DEFAULT_SEC;
    g_dab_timer.sessions = 0U;
    g_dab_timer.mode = DAB_TIMER_IDLE;
    dab_timer_reset_countdown(&g_dab_timer);
    dab_timer_draw(&g_dab_timer);

    for (;;)
    {
        dab_timer_sync(&g_dab_timer);
        dab_timer_handle_alert(&g_dab_timer);
        dab_timer_draw(&g_dab_timer);

        btn = game_poll_button(DAB_TIMER_POLL_MS);
        if (btn == GAME_BTN_NONE)
        {
            continue;
        }

        if (btn == GAME_BTN_BACK)
        {
            return;
        }

        switch (btn)
        {
            case GAME_BTN_OK:
                if (g_dab_timer.mode == DAB_TIMER_RUNNING)
                {
                    dab_timer_sync(&g_dab_timer);
                    g_dab_timer.mode = DAB_TIMER_PAUSED;
                }
                else if (g_dab_timer.mode == DAB_TIMER_ALERT)
                {
                    g_dab_timer.mode = DAB_TIMER_IDLE;
                    dab_timer_reset_countdown(&g_dab_timer);
                }
                else
                {
                    if (g_dab_timer.remaining_ms == 0U)
                    {
                        dab_timer_reset_countdown(&g_dab_timer);
                    }
                    g_dab_timer.mode = DAB_TIMER_RUNNING;
                    g_dab_timer.deadline_ms = HAL_GetTick() + g_dab_timer.remaining_ms;
                }
                break;

            case GAME_BTN_LEFT:
                if (g_dab_timer.mode != DAB_TIMER_RUNNING)
                {
                    dab_timer_adjust(&g_dab_timer, -(int16_t)DAB_TIMER_STEP_SMALL_SEC);
                }
                break;

            case GAME_BTN_RIGHT:
                if (g_dab_timer.mode != DAB_TIMER_RUNNING)
                {
                    dab_timer_adjust(&g_dab_timer, (int16_t)DAB_TIMER_STEP_SMALL_SEC);
                }
                break;

            case GAME_BTN_UP:
                if (g_dab_timer.mode != DAB_TIMER_RUNNING)
                {
                    dab_timer_adjust(&g_dab_timer, (int16_t)DAB_TIMER_STEP_LARGE_SEC);
                }
                break;

            case GAME_BTN_DOWN:
                if (g_dab_timer.mode != DAB_TIMER_RUNNING)
                {
                    dab_timer_adjust(&g_dab_timer, -(int16_t)DAB_TIMER_STEP_LARGE_SEC);
                }
                break;

            case GAME_BTN_NONE:
            case GAME_BTN_BACK:
            default:
                break;
        }
    }
}
