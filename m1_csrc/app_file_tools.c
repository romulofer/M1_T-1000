/* See COPYING.txt for license details. */

/*
*
* app_file_tools.c
*
* File tools utility app
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
#include "m1_storage.h"
#include "m1_sdcard.h"

/*************************** D E F I N E S ************************************/

#define FILE_TOOLS_ITEM_COUNT  4U
#define FILE_TOOLS_POLL_MS     120U
#define FILE_TOOLS_VISIBLE_ITEMS 2U
#define FILE_TOOLS_ROW_HEIGHT    12U
#define FILE_TOOLS_LIST_Y        38U

//************************** S T R U C T U R E S *******************************/

typedef enum
{
    FILE_TOOLS_MANAGE = 0,
    FILE_TOOLS_INFO,
    FILE_TOOLS_MOUNT,
    FILE_TOOLS_UNMOUNT
} file_tools_item_t;

/***************************** V A R I A B L E S ******************************/

static const char *file_tools_labels[FILE_TOOLS_ITEM_COUNT] = {
    "Manage Files",
    "Card Info",
    "Mount SD",
    "Unmount SD"
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void file_tools_draw(uint8_t sel);
static void file_tools_status_text(char *status_text, size_t status_len, char *capacity_text, size_t capacity_len);
static void file_tools_show_info(void);
static const char *file_tools_status_label(S_M1_SDCard_Access_Status status);
static const char *file_tools_fs_label(S_M1_SDCard_FAT_Sys fs_type);
static void file_tools_format_capacity(uint32_t kb, char *out, size_t out_len);
static uint8_t file_tools_free_percent(const S_M1_SDCard_Info *info);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static void file_tools_draw(uint8_t sel)
{
    char badge[12];
    char status_text[24];
    char capacity_text[20];
    uint8_t visible_start;
    uint8_t y = FILE_TOOLS_LIST_Y;

    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1U), (unsigned)FILE_TOOLS_ITEM_COUNT);
    file_tools_status_text(status_text, sizeof(status_text), capacity_text, sizeof(capacity_text));
    visible_start = (sel >= FILE_TOOLS_VISIBLE_ITEMS) ?
        (uint8_t)(sel - FILE_TOOLS_VISIBLE_ITEMS + 1U) : 0U;

    m1_u8g2_firstpage();
    m1_draw_header_bar(&m1_u8g2, "File Tools", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 36);

    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_text(&m1_u8g2, 8, 22, 108, status_text, TEXT_ALIGN_LEFT);
    m1_draw_text(&m1_u8g2, 8, 30, 108, capacity_text, TEXT_ALIGN_LEFT);

    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    for (uint8_t vi = 0; vi < FILE_TOOLS_VISIBLE_ITEMS && (visible_start + vi) < FILE_TOOLS_ITEM_COUNT; vi++)
    {
        uint8_t i = (uint8_t)(visible_start + vi);

        if (i == sel)
        {
            u8g2_DrawBox(&m1_u8g2, 6, (u8g2_uint_t)(y - 7U), 114, 11);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
            m1_draw_text(&m1_u8g2, 10, y, 108, file_tools_labels[i], TEXT_ALIGN_LEFT);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        }
        else
        {
            u8g2_DrawFrame(&m1_u8g2, 6, (u8g2_uint_t)(y - 7U), 114, 11);
            m1_draw_text(&m1_u8g2, 10, y, 108, file_tools_labels[i], TEXT_ALIGN_LEFT);
        }
        y = (uint8_t)(y + FILE_TOOLS_ROW_HEIGHT);
    }

    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Open", arrowright_8x8);
    m1_u8g2_nextpage();
}


static void file_tools_status_text(char *status_text, size_t status_len, char *capacity_text, size_t capacity_len)
{
    S_M1_SDCard_Access_Status sd_status = m1_sdcard_get_status();
    S_M1_SDCard_Info *info;
    unsigned long free_gb;
    unsigned long free_mb;
    uint8_t free_pct;

    snprintf(capacity_text, capacity_len, " ");

    if (sd_status == SD_access_OK)
    {
        info = m1_sdcard_get_info();
        snprintf(status_text, status_len, "SD card mounted");

        if (info != NULL)
        {
            free_gb = (unsigned long)(info->free_cap_kb / 1024UL / 1024UL);
            free_mb = (unsigned long)(info->free_cap_kb / 1024UL);
            free_pct = file_tools_free_percent(info);

            if (free_gb > 0UL)
            {
                snprintf(capacity_text, capacity_len, "%luG free %u%%", free_gb, (unsigned)free_pct);
            }
            else
            {
                snprintf(capacity_text, capacity_len, "%luM free %u%%", free_mb, (unsigned)free_pct);
            }
        }
        else
        {
            snprintf(capacity_text, capacity_len, "Mounted");
        }
    }
    else if (sd_status == SD_access_NotReady)
    {
        snprintf(status_text, status_len, "No SD card detected");
        snprintf(capacity_text, capacity_len, "No card");
    }
    else if (sd_status == SD_access_NoFS)
    {
        snprintf(status_text, status_len, "Card needs formatting");
        snprintf(capacity_text, capacity_len, "No FS");
    }
    else if (sd_status == SD_access_UnMounted)
    {
        snprintf(status_text, status_len, "Card is unmounted");
        snprintf(capacity_text, capacity_len, "Offline");
    }
    else
    {
        snprintf(status_text, status_len, "Card access error");
        snprintf(capacity_text, capacity_len, "Error");
    }
}


static void file_tools_show_info(void)
{
    S_M1_SDCard_Access_Status sd_status;
    S_M1_SDCard_Info *info = NULL;
    game_button_t btn;
    char line1[28];
    char line2[28];
    char line3[28];
    char line4[28];
    char total_text[12];
    char free_text[12];
    char label_text[16];
    char badge[12];
    uint8_t free_pct;
    uint8_t label_idx;

    for (;;)
    {
        sd_status = m1_sdcard_get_status();
        info = (sd_status == SD_access_OK) ? m1_sdcard_get_info() : NULL;

        if (info != NULL && sd_status == SD_access_OK)
        {
            file_tools_format_capacity(info->total_cap_kb, total_text, sizeof(total_text));
            file_tools_format_capacity(info->free_cap_kb, free_text, sizeof(free_text));
            free_pct = file_tools_free_percent(info);
            snprintf(badge, sizeof(badge), "%s", file_tools_fs_label(info->fs_type));
            snprintf(line1, sizeof(line1), "Total: %s", total_text);
            snprintf(line2, sizeof(line2), "Free:  %s %u%%", free_text, (unsigned)free_pct);
            snprintf(line3, sizeof(line3), "Cluster:%u Sec:%u",
                     (unsigned)info->cluster_size,
                     (unsigned)info->sector_size);
            label_idx = 0U;
            if (info->vol_label[0])
            {
                while (label_idx < (sizeof(label_text) - 1U)
                       && info->vol_label[label_idx] != '\0')
                {
                    label_text[label_idx] = info->vol_label[label_idx];
                    label_idx++;
                }
                label_text[label_idx] = '\0';
            }
            else
            {
                strcpy(label_text, "none");
            }
            snprintf(line4, sizeof(line4), "Label:%s", label_text);
        }
        else
        {
            snprintf(badge, sizeof(badge), "Info");
            snprintf(line1, sizeof(line1), "Status: %s", file_tools_status_label(sd_status));
            snprintf(line2, sizeof(line2), "Total: N/A");
            snprintf(line3, sizeof(line3), "Free:  N/A");
            snprintf(line4, sizeof(line4), "Mount SD to refresh");
        }

        m1_u8g2_firstpage();
        m1_draw_header_bar(&m1_u8g2, "Card Info", badge);
        m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 36);
        u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
        m1_draw_text(&m1_u8g2, 8, 22, 114, line1, TEXT_ALIGN_LEFT);
        m1_draw_text(&m1_u8g2, 8, 30, 114, line2, TEXT_ALIGN_LEFT);
        m1_draw_text(&m1_u8g2, 8, 38, 114, line3, TEXT_ALIGN_LEFT);
        m1_draw_text(&m1_u8g2, 8, 46, 114, line4, TEXT_ALIGN_LEFT);
        m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Refresh", arrowright_8x8);
        m1_u8g2_nextpage();

        btn = game_poll_button(FILE_TOOLS_POLL_MS);
        if (btn == GAME_BTN_BACK || btn == GAME_BTN_LEFT)
        {
            return;
        }
        if (btn == GAME_BTN_OK || btn == GAME_BTN_RIGHT)
        {
            continue;
        }
    }
}


static const char *file_tools_status_label(S_M1_SDCard_Access_Status status)
{
    switch (status)
    {
        case SD_access_OK:
            return "Mounted";
        case SD_access_NotReady:
            return "No card";
        case SD_access_UnMounted:
            return "Unmounted";
        case SD_access_NoFS:
            return "No FS";
        case SD_access_NotOK:
            return "Error";
        default:
            return "Unknown";
    }
}


static const char *file_tools_fs_label(S_M1_SDCard_FAT_Sys fs_type)
{
    switch (fs_type)
    {
        case FATSYS_12:
            return "FAT12";
        case FATSYS_16:
            return "FAT16";
        case FATSYS_32:
            return "FAT32";
        case FATSYS_EXT:
            return "exFAT";
        case FATSYS_ANY:
        default:
            return "FAT";
    }
}


static void file_tools_format_capacity(uint32_t kb, char *out, size_t out_len)
{
    uint32_t gb_x10;
    uint32_t mb;

    if (out == NULL || out_len == 0U)
    {
        return;
    }

    if (kb >= (1024UL * 1024UL))
    {
        gb_x10 = (uint32_t)(((uint64_t)kb * 10ULL) / (1024ULL * 1024ULL));
        snprintf(out, out_len, "%lu.%luG",
                 (unsigned long)(gb_x10 / 10UL),
                 (unsigned long)(gb_x10 % 10UL));
    }
    else
    {
        mb = kb / 1024UL;
        snprintf(out, out_len, "%luM", (unsigned long)mb);
    }
}


static uint8_t file_tools_free_percent(const S_M1_SDCard_Info *info)
{
    uint32_t pct;

    if (info == NULL || info->total_cap_kb == 0U)
    {
        return 0U;
    }

    pct = (uint32_t)(((uint64_t)info->free_cap_kb * 100ULL) / info->total_cap_kb);
    return (pct > 100U) ? 100U : (uint8_t)pct;
}


void app_file_tools_run(void)
{
    uint8_t sel = 0;
    game_button_t btn;

    for (;;)
    {
        file_tools_draw(sel);
        btn = game_poll_button(FILE_TOOLS_POLL_MS);

        if (btn == GAME_BTN_BACK)
        {
            return;
        }
        if (btn == GAME_BTN_UP)
        {
            sel = (sel == 0U) ? (FILE_TOOLS_ITEM_COUNT - 1U) : (uint8_t)(sel - 1U);
        }
        else if (btn == GAME_BTN_DOWN)
        {
            sel = (uint8_t)((sel + 1U) % FILE_TOOLS_ITEM_COUNT);
        }
        else if (btn == GAME_BTN_OK || btn == GAME_BTN_RIGHT)
        {
            switch ((file_tools_item_t)sel)
            {
                case FILE_TOOLS_MANAGE:
                    storage_explore();
                    break;
                case FILE_TOOLS_INFO:
                    file_tools_show_info();
                    break;
                case FILE_TOOLS_MOUNT:
                    storage_mount();
                    break;
                case FILE_TOOLS_UNMOUNT:
                    storage_unmount();
                    break;
                default:
                    break;
            }
        }
    }
}
