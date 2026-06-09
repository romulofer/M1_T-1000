/* See COPYING.txt for license details. */

/*
 *
 * app_rgb_backlight.c
 *
 * RGB Backlight settings menu for M1
 *
 * M1 Project
 *
 */

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "m1_builtin_apps.h"
#include "m1_games.h"
#include "m1_rgb_backlight.h"
#include "m1_display.h"
#include "m1_settings.h"

/*************************** D E F I N E S ************************************/

#define RGB_MENU_ITEMS     6U
#define RGB_MENU_ONOFF     0U
#define RGB_MENU_COLOR     1U
#define RGB_MENU_EFFECT    2U
#define RGB_MENU_BRIGHT    3U
#define RGB_MENU_CUSTOM    4U
#define RGB_MENU_REACTIVE  5U

#define RGB_POLL_MS      120U
#define RGB_VISIBLE_ITEMS 2U

/* Custom color editor */
#define RGB_CUSTOM_CHANNELS 3U
#define RGB_CUSTOM_STEP    16U

/***************************** V A R I A B L E S ******************************/

static const char *s_brightness_labels[] = {
    "Off", "10%", "25%", "50%", "75%", "100%"
};
static const uint8_t s_brightness_values[] = {
    0, 26, 64, 128, 192, 255
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void rgb_menu_draw(uint8_t sel, uint8_t bright_idx);
static const char *rgb_menu_item_label(uint8_t item);
static const char *rgb_menu_item_value(uint8_t item, uint8_t bright_idx);
static uint8_t rgb_brightness_index(uint8_t brightness);
static void rgb_custom_editor(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static const char *rgb_menu_item_label(uint8_t item)
{
    switch (item)
    {
        case RGB_MENU_ONOFF:   return "Backlight";
        case RGB_MENU_COLOR:   return "Color";
        case RGB_MENU_EFFECT:  return "Animation";
        case RGB_MENU_BRIGHT:  return "Brightness";
        case RGB_MENU_CUSTOM:  return "Custom";
        case RGB_MENU_REACTIVE: return "Reactive";
        default:               return "";
    }
}

static const char *rgb_menu_item_value(uint8_t item, uint8_t bright_idx)
{
    static char val_buf[12];

    switch (item)
    {
        case RGB_MENU_ONOFF:   return rgb_bl_is_on() ? "On" : "Off";
        case RGB_MENU_COLOR:   return rgb_bl_mode_name(rgb_bl_get_mode());
        case RGB_MENU_EFFECT:  return rgb_bl_effect_name(rgb_bl_get_effect());
        case RGB_MENU_BRIGHT:  return s_brightness_labels[bright_idx];
        case RGB_MENU_REACTIVE: return rgb_bl_reactive_is_on() ? "On" : "Off";
        case RGB_MENU_CUSTOM:
        {
            uint8_t r, g, b;
            rgb_bl_get_custom(&r, &g, &b);
            snprintf(val_buf, sizeof(val_buf), "%02X%02X%02X", r, g, b);
            return val_buf;
        }
        default:               return "";
    }
}

static uint8_t rgb_brightness_index(uint8_t brightness)
{
    uint8_t best = 0U;
    uint16_t best_diff = 255U;

    for (uint8_t i = 0; i < 6U; i++)
    {
        uint16_t diff = (brightness > s_brightness_values[i]) ?
            (uint16_t)(brightness - s_brightness_values[i]) :
            (uint16_t)(s_brightness_values[i] - brightness);
        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }
    }

    return best;
}

static void rgb_menu_draw(uint8_t sel, uint8_t bright_idx)
{
    char badge[8];
    uint8_t visible_start = 0;

    if (sel >= RGB_VISIBLE_ITEMS && RGB_MENU_ITEMS > RGB_VISIBLE_ITEMS)
        visible_start = (uint8_t)(sel - RGB_VISIBLE_ITEMS + 1U);

    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1U), (unsigned)RGB_MENU_ITEMS);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "Backlight", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    for (uint8_t vi = 0; vi < RGB_VISIBLE_ITEMS && (visible_start + vi) < RGB_MENU_ITEMS; vi++)
    {
        uint8_t item = visible_start + vi;
        uint8_t y = 30 + vi * 12;
        const char *label = rgb_menu_item_label(item);
        const char *value = rgb_menu_item_value(item, bright_idx);

        if (item == sel)
        {
            u8g2_DrawBox(&m1_u8g2, 6, y - 7, 114, 11);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
            m1_draw_text(&m1_u8g2, 10, y, 64, label, TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 78, y, 38, value, TEXT_ALIGN_RIGHT);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        }
        else
        {
            u8g2_DrawFrame(&m1_u8g2, 6, y - 7, 114, 11);
            m1_draw_text(&m1_u8g2, 10, y, 64, label, TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 78, y, 38, value, TEXT_ALIGN_RIGHT);
        }
    }

    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Change", arrowright_8x8);
    m1_u8g2_nextpage();
}


void app_rgb_backlight_run(void)
{
    game_button_t btn;
    uint8_t sel = 0U;
    uint8_t needs_redraw = 1U;
    uint8_t bright_idx;

    /* Init RGB hardware on entry (safe to call again, sets up PD3) */
    rgb_bl_init();
    bright_idx = rgb_brightness_index(rgb_bl_get_brightness());

    for (;;)
    {
        if (needs_redraw)
        {
            needs_redraw = 0U;
            rgb_menu_draw(sel, bright_idx);
        }

        /* Run effects at ~8fps */
        btn = game_poll_button(RGB_POLL_MS);
        rgb_bl_update();

        if (btn == GAME_BTN_BACK)
        {
            /* Keep backlight on — this is the system backlight now.
             * Persist mode/effect/brightness/custom so changes survive a
             * reboot whether we were entered from System>Backlight or the
             * main-menu "RGB Backlight" shortcut. */
            settings_save_to_sd();
            return;
        }

        if (btn == GAME_BTN_UP)
        {
            sel = (sel == 0U) ? (RGB_MENU_ITEMS - 1U) : (sel - 1U);
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_DOWN)
        {
            sel = (sel + 1U) % RGB_MENU_ITEMS;
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_LEFT)
        {
            if (sel == RGB_MENU_COLOR)
            {
                rgb_bl_mode_t m = rgb_bl_get_mode();
                rgb_bl_set_mode((m == 0U) ? (RGB_MODE_COUNT - 1U) : (rgb_bl_mode_t)(m - 1U));
            }
            else if (sel == RGB_MENU_EFFECT)
            {
                rgb_bl_effect_t e = rgb_bl_get_effect();
                rgb_bl_set_effect((e == 0U) ? (RGB_EFFECT_COUNT - 1U) : (rgb_bl_effect_t)(e - 1U));
            }
            else if (sel == RGB_MENU_BRIGHT)
            {
                bright_idx = (bright_idx == 0U) ? 5U : (bright_idx - 1U);
                rgb_bl_set_brightness(s_brightness_values[bright_idx]);
            }
            else if (sel == RGB_MENU_ONOFF)
            {
                if (rgb_bl_is_on()) rgb_bl_off();
                else rgb_bl_on();
            }
            else if (sel == RGB_MENU_REACTIVE)
            {
                rgb_bl_reactive_set(!rgb_bl_reactive_is_on());
            }
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_RIGHT || btn == GAME_BTN_OK)
        {
            if (sel == RGB_MENU_COLOR)
            {
                rgb_bl_mode_t m = rgb_bl_get_mode();
                rgb_bl_set_mode((rgb_bl_mode_t)((m + 1U) % RGB_MODE_COUNT));
            }
            else if (sel == RGB_MENU_EFFECT)
            {
                rgb_bl_effect_t e = rgb_bl_get_effect();
                rgb_bl_set_effect((rgb_bl_effect_t)((e + 1U) % RGB_EFFECT_COUNT));
            }
            else if (sel == RGB_MENU_BRIGHT)
            {
                bright_idx = (bright_idx + 1U) % 6U;
                rgb_bl_set_brightness(s_brightness_values[bright_idx]);
            }
            else if (sel == RGB_MENU_ONOFF)
            {
                if (rgb_bl_is_on()) rgb_bl_off();
                else rgb_bl_on();
            }
            else if (sel == RGB_MENU_CUSTOM)
            {
                rgb_custom_editor();
            }
            else if (sel == RGB_MENU_REACTIVE)
            {
                rgb_bl_reactive_set(!rgb_bl_reactive_is_on());
            }
            needs_redraw = 1U;
        }
    }
}


/*============================================================================*/
/**
  * @brief  Custom RGB color editor — adjust R/G/B channels with live preview.
  *         Up/Down select a channel, Left/Right adjust it, OK/Back saves.
  */
/*============================================================================*/
static void rgb_custom_editor(void)
{
    static const char *ch_names[RGB_CUSTOM_CHANNELS] = { "R", "G", "B" };
    uint8_t vals[RGB_CUSTOM_CHANNELS];
    uint8_t ch = 0U;
    uint8_t needs_redraw = 1U;
    game_button_t btn;

    rgb_bl_get_custom(&vals[0], &vals[1], &vals[2]);

    for (;;)
    {
        if (needs_redraw)
        {
            char badge[8];

            needs_redraw = 0U;
            snprintf(badge, sizeof(badge), "%02X%02X%02X", vals[0], vals[1], vals[2]);

            m1_u8g2_firstpage();
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            m1_draw_header_bar(&m1_u8g2, "Custom", badge);
            m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 37);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

            for (uint8_t i = 0; i < RGB_CUSTOM_CHANNELS; i++)
            {
                uint8_t y = (uint8_t)(26U + i * 11U);   /* baselines 26, 37, 48 */
                uint8_t bar_w = (uint8_t)((uint16_t)vals[i] * 60U / 255U);
                char num[5];

                snprintf(num, sizeof(num), "%u", (unsigned)vals[i]);

                if (i == ch)
                {
                    u8g2_DrawBox(&m1_u8g2, 6, y - 7, 114, 10);
                    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
                }
                else
                {
                    u8g2_DrawFrame(&m1_u8g2, 6, y - 7, 114, 10);
                }

                m1_draw_text(&m1_u8g2, 10, y, 10, ch_names[i], TEXT_ALIGN_LEFT);
                u8g2_DrawFrame(&m1_u8g2, 24, y - 5, 62, 7);
                if (bar_w > 0U)
                    u8g2_DrawBox(&m1_u8g2, 25, y - 4, bar_w, 5);
                m1_draw_text(&m1_u8g2, 92, y, 26, num, TEXT_ALIGN_RIGHT);

                u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            }

            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Adjust", arrowright_8x8);
            m1_u8g2_nextpage();
        }

        btn = game_poll_button(RGB_POLL_MS);
        rgb_bl_update();

        if (btn == GAME_BTN_BACK || btn == GAME_BTN_OK)
            return;

        if (btn == GAME_BTN_UP)
        {
            ch = (ch == 0U) ? (RGB_CUSTOM_CHANNELS - 1U) : (uint8_t)(ch - 1U);
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_DOWN)
        {
            ch = (uint8_t)((ch + 1U) % RGB_CUSTOM_CHANNELS);
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_LEFT)
        {
            vals[ch] = (vals[ch] < RGB_CUSTOM_STEP) ?
                0U : (uint8_t)(vals[ch] - RGB_CUSTOM_STEP);
            rgb_bl_set_custom(vals[0], vals[1], vals[2]);
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_RIGHT)
        {
            vals[ch] = (vals[ch] > (uint8_t)(255U - RGB_CUSTOM_STEP)) ?
                255U : (uint8_t)(vals[ch] + RGB_CUSTOM_STEP);
            rgb_bl_set_custom(vals[0], vals[1], vals[2]);
            needs_redraw = 1U;
        }
    }
}
