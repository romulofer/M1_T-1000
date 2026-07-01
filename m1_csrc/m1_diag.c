/* See COPYING.txt for license details. */

/*
 * m1_diag.c - On-device self-diagnostics (lean port).
 *
 * Ported from da-pingwing's "Monstatek M1 RFID Patch"
 * (github.com/da-pingwing/M1_T-1000_RFID, GPL-3.0). Keeps the reset-cause
 * readout (RCC->RSR) and the .noinit write-phase breadcrumb that survives a
 * watchdog/brownout/fault reset, and adds an on-screen readout so the cause of
 * "nothing from RFID" (a silent reset / brownout during the coil write) is
 * visible without a serial adapter. The original's HardFault trampoline and
 * PVD ISR are intentionally omitted from this port.
 *
 * M1 Project
 */

#include "stm32h5xx_hal.h"
#include "main.h"
#include "app_freertos.h"
#include "m1_diag.h"
#include "m1_log_debug.h"
#include "m1_display.h"
#include "m1_fw_update_bl.h"

#define M1_DIAG_TAG  "M1DIAG"

/* No-init RAM (NOLOAD in the linker): not zeroed by startup, so it survives a
 * software / watchdog / brownout reset. magic guards against stale RAM. */
volatile M1DiagBlock m1_diag __attribute__((section(".noinit")));

static const char *diag_phase_name(uint8_t phase)
{
    switch ((M1DiagWritePhase)phase) {
        case WRITE_PHASE_NONE:    return "NONE";
        case WRITE_PHASE_START:   return "START";
        case WRITE_PHASE_BLOCK:   return "BLOCK";
        case WRITE_PHASE_PROGRAM: return "PROGRAM";
        case WRITE_PHASE_GAP:     return "GAP";
        case WRITE_PHASE_RESET:   return "RESET";
        case WRITE_PHASE_STOP:    return "STOP";
        case WRITE_PHASE_DONE:    return "DONE";
        case WRITE_PHASE_UI_CREATE:       return "UI_CREATE";
        case WRITE_PHASE_WORKER_ENTER:    return "WRK_ENTER";
        case WRITE_PHASE_WORKER_TEARDOWN: return "WRK_TEARDN";
        case WRITE_PHASE_WORKER_PREWRITE: return "WRK_PREWR";
        case WRITE_PHASE_READBACK:        return "READBACK";
        case WRITE_PHASE_VERIFY:          return "VERIFY";
        default:                          return "UNK";
    }
}

/* Decode RCC->RSR reset flags into a short string. */
static void diag_rsr_str(uint32_t rsr, char *out, int n)
{
    out[0] = '\0';
    int used = 0;
#define DRSR_APPEND(bit, name) do { \
        if ((rsr) & (bit)) { \
            int _w = snprintf(out + used, (used < n) ? (n - used) : 0, \
                              "%s%s", (used ? "|" : ""), (name)); \
            if (_w > 0) used += _w; \
        } } while (0)
    DRSR_APPEND(RCC_RSR_BORRSTF,  "BOR");
    DRSR_APPEND(RCC_RSR_PINRSTF,  "PIN");
    DRSR_APPEND(RCC_RSR_SFTRSTF,  "SFT");
    DRSR_APPEND(RCC_RSR_IWDGRSTF, "IWDG");
    DRSR_APPEND(RCC_RSR_WWDGRSTF, "WWDG");
    DRSR_APPEND(RCC_RSR_LPWRRSTF, "LPWR");
#undef DRSR_APPEND
    if (used == 0) snprintf(out, n, "POR");
}

static const char *diag_boot_recovery_name(uint32_t code)
{
    switch ((S_M1_BOOT_DIAG_CODE_t)code) {
        case BOOT_DIAG_EMPTY_BANK_SWAP:      return "EMPTY_BANK_SWAP";
        case BOOT_DIAG_EMPTY_BANK_DFU:       return "EMPTY_BANK_DFU";
        case BOOT_DIAG_CRC_SWAP:             return "CRC_SWAP";
        case BOOT_DIAG_CRC_DFU:              return "CRC_DFU";
        case BOOT_DIAG_OB_RESET_FALLBACK:    return "OB_RESET_FALLBACK";
        case BOOT_DIAG_NONE:
        default:                             return "NONE";
    }
}

/*============================================================================*/
/* Read RCC->RSR, snapshot the previous boot's breadcrumb into prev_* (which
 * survive future re-inits), log a boot report, re-init the live fields. */
/*============================================================================*/
void m1_diag_boot_report(void)
{
    uint32_t rsr = RCC->RSR;
    uint32_t boot_diag = TAMP->BKP2R;

    uint8_t  snap_valid = (m1_diag.magic == M1_DIAG_MAGIC) ? 1 : 0;
    uint8_t  snap_phase = snap_valid ? m1_diag.last_write_phase : 0xFF;
    uint8_t  snap_blk   = snap_valid ? m1_diag.last_write_block : 0;
    uint8_t  snap_pvd   = snap_valid ? m1_diag.pvd_flag : 0;
    uint8_t  snap_rsrc  = snap_valid ? m1_diag.reset_src : 0;
    uint32_t snap_boots = snap_valid ? m1_diag.boot_count : 0;

    char flags[48];
    diag_rsr_str(rsr, flags, sizeof(flags));
    M1_LOG_I(M1_DIAG_TAG, "BOOT: reset=%s raw=0x%08lX\r\n", flags, (unsigned long)rsr);
    if ((boot_diag & BOOT_DIAG_MAGIC_MASK) == BOOT_DIAG_MAGIC) {
        uint32_t code = boot_diag & BOOT_DIAG_CODE_MASK;
        M1_LOG_I(M1_DIAG_TAG, "BOOTREC: %s (%lu)\r\n",
                 diag_boot_recovery_name(code), (unsigned long)code);
        TAMP->BKP2R = 0;
    }
    if (snap_valid) {
        M1_LOG_I(M1_DIAG_TAG, "LAST: phase=%s(%u) blk=%u writes=%lu pvd=%u\r\n",
                 diag_phase_name(snap_phase), (unsigned)snap_phase, (unsigned)snap_blk,
                 (unsigned long)m1_diag.write_count, (unsigned)snap_pvd);
    }

    /* Re-init live fields for this boot. */
    m1_diag.magic            = M1_DIAG_MAGIC;
    m1_diag.write_count      = 0;
    m1_diag.last_write_phase = (uint8_t)WRITE_PHASE_NONE;
    m1_diag.last_write_block = 0;
    m1_diag.pvd_flag         = 0;
    m1_diag.fault_valid      = 0;
    m1_diag.reset_src        = 0;

    /* Persist previous-boot snapshot (survives future re-inits). */
    m1_diag.prev_rsr       = rsr;
    m1_diag.prev_phase     = snap_phase;
    m1_diag.prev_blk       = snap_blk;
    m1_diag.prev_pvd       = snap_pvd;
    m1_diag.prev_reset_src = snap_rsrc;
    m1_diag.boot_count     = snap_boots + 1;

    __HAL_RCC_CLEAR_RESET_FLAGS();
}

void m1_diag_dump(void)
{
    char flags[48];
    diag_rsr_str(m1_diag.prev_rsr, flags, sizeof(flags));
    M1_LOG_I(M1_DIAG_TAG, "LASTRESET: boot#%lu cause=%s phase=%s(%u) blk=%u pvd=%u\r\n",
             (unsigned long)m1_diag.boot_count, flags,
             (m1_diag.prev_phase == 0xFF) ? "UNK" : diag_phase_name(m1_diag.prev_phase),
             (unsigned)m1_diag.prev_phase, (unsigned)m1_diag.prev_blk,
             (unsigned)m1_diag.prev_pvd);
}

/*============================================================================*/
/* On-screen diagnostics: shows why the device last reset and how far the RFID
 * write got, so "nothing from RFID" (a brownout/watchdog reset mid-write) is
 * visible on the device itself. Blocks until BACK. */
/*============================================================================*/
void m1_diag_screen(void)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t       q_item;
    BaseType_t          ret;
    bool                running = true;
    bool                redraw = true;

    while (running) {
        if (redraw) {
            redraw = false;
            char cause[48], l[32];
            diag_rsr_str(m1_diag.prev_rsr, cause, sizeof(cause));

            u8g2_FirstPage(&m1_u8g2);
            do {
                u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
                u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
                u8g2_DrawStr(&m1_u8g2, 2, 10, "RFID Diagnostics");
                u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

                snprintf(l, sizeof(l), "Boot #%lu  reset:", (unsigned long)m1_diag.boot_count);
                u8g2_DrawStr(&m1_u8g2, 2, 22, l);
                u8g2_DrawStr(&m1_u8g2, 2, 32, cause);

                snprintf(l, sizeof(l), "WrPhase: %s",
                         (m1_diag.prev_phase == 0xFF) ? "UNK" : diag_phase_name(m1_diag.prev_phase));
                u8g2_DrawStr(&m1_u8g2, 2, 44, l);

                snprintf(l, sizeof(l), "blk:%u pvd:%u flt:%u",
                         (unsigned)m1_diag.prev_blk, (unsigned)m1_diag.prev_pvd,
                         (unsigned)m1_diag.prev_fault);
                u8g2_DrawStr(&m1_u8g2, 2, 54, l);

                u8g2_DrawStr(&m1_u8g2, 2, 64, "BACK to exit");
            } while (u8g2_NextPage(&m1_u8g2));
        }

        ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(200));
        if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD) {
            ret = xQueueReceive(button_events_q_hdl, &btn, 0);
            if (ret == pdTRUE && btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
                running = false;
            }
        }
    }
    xQueueReset(main_q_hdl);
}
