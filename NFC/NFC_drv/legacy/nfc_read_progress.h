/* See COPYING.txt for license details. */

#ifndef NFC_READ_PROGRESS_H_
#define NFC_READ_PROGRESS_H_

/*
 * Shared NFC read-lifecycle progress state (poller -> read-view feedback).
 *
 * Deliberately dependency-free (only <stdint.h>) so it can be included by the
 * poller, the UI, and the host test harness alike. The poller updates a single
 * shared instance during a MIFARE Classic read and wakes the read view with a
 * Q_EVENT_NFC_READ_PROGRESS event; the view snapshots it via
 * nfc_poller_get_read_progress() and renders the matching status line.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>   /* snprintf for the completion-status builder */

typedef enum {
    NFC_RD_STAGE_SCANNING = 0,   /* looking for a card (== the Ready screen) */
    NFC_RD_STAGE_CARD_FOUND,     /* NFC-A MIFARE Classic detected            */
    NFC_RD_STAGE_TRYING_KEYS,    /* dictionary auth sweep, sector n/m        */
    NFC_RD_STAGE_READING,        /* reading blocks of an authenticated sector*/
    NFC_RD_STAGE_DONE,           /* sweep finished; see .result              */
} nfc_read_stage_t;

typedef enum {
    NFC_RD_RESULT_NONE = 0,      /* not finished yet                          */
    NFC_RD_RESULT_UID_ONLY,      /* 0 sectors authed — identity floor only    */
    NFC_RD_RESULT_PARTIAL,       /* some but not all sectors authed           */
    NFC_RD_RESULT_FULL,          /* every sector authed                       */
} nfc_read_result_t;

typedef struct {
    uint8_t  stage;          /* nfc_read_stage_t                              */
    uint8_t  result;         /* nfc_read_result_t (meaningful at DONE)        */
    uint16_t sector;         /* 1-based current sector; at DONE = authed count*/
    uint16_t total_sectors;  /* sector count for the detected card layout     */
} nfc_read_progress_t;

/*
 * Classify a MIFARE Classic dump outcome from sector counts. Pure function so
 * both the poller and the host test share one source of truth.
 */
static inline nfc_read_result_t
mfc_classify_result(uint16_t success_sectors, uint16_t total_sectors)
{
    if (success_sectors == 0)              return NFC_RD_RESULT_UID_ONLY;
    if (success_sectors >= total_sectors)  return NFC_RD_RESULT_FULL;
    return NFC_RD_RESULT_PARTIAL;
}

/*
 * Build the read-view completion status line from the classified result:
 *   UID_ONLY      -> "Read (UID only)"
 *   FULL/PARTIAL  -> "Sectors N/M"
 *   NONE          -> "" (non-MIFARE-Classic read: caller keeps its layout)
 * Writes into @p out (bounded by snprintf) and returns it. Short enough for
 * the 128x64 read-done screen.
 */
static inline const char *
nfc_read_completion_status(uint8_t result, uint16_t authed_sectors,
                           uint16_t total_sectors, char *out, unsigned out_sz)
{
    if (!out || out_sz == 0) return "";
    switch (result) {
        case NFC_RD_RESULT_UID_ONLY:
            snprintf(out, out_sz, "Read (UID only)");
            break;
        case NFC_RD_RESULT_PARTIAL:
        case NFC_RD_RESULT_FULL:
            snprintf(out, out_sz, "Sectors %u/%u",
                     (unsigned)authed_sectors, (unsigned)total_sectors);
            break;
        default:
            out[0] = '\0';
            break;
    }
    return out;
}

/*
 * True if a SAK byte indicates any MIFARE Classic (or Plus SL2) variant, i.e.
 * a card the dictionary-auth dump path should attempt. Pure; shared by the
 * poller's card-type gate and the host test.
 */
static inline bool mfc_is_classic_sak(uint8_t sak)
{
    switch (sak) {
    case 0x01: /* Classic 1K (TNP3xxx) */
    case 0x08: /* Classic 1K */
    case 0x09: /* MIFARE Mini */
    case 0x10: /* Plus 2K SL2 */
    case 0x11: /* Plus 4K SL2 */
    case 0x18: /* Classic 4K */
    case 0x19: /* Classic 2K */
    case 0x28: /* Classic EV1 1K */
    case 0x38: /* Classic EV1 4K */
        return true;
    default:
        return false;
    }
}

/*
 * Map a MIFARE Classic SAK to its (sector, block) layout. Returns true when the
 * SAK matches a known variant; false means the outputs were set to the 1K
 * fallback (16 sectors / 64 blocks) — the caller logs the unknown SAK, keeping
 * this header dependency-free. Layouts: Mini 5/20, 1K 16/64, 2K 32/128,
 * 4K 40/256 (4K's top 8 sectors use 16 blocks each, hence 256 not 160).
 */
static inline bool mfc_layout_from_sak(uint8_t sak, uint16_t *outSectors,
                                       uint16_t *outBlocks)
{
    switch (sak) {
    case 0x09: /* Mini */
        *outSectors = 5;  *outBlocks = 20;  return true;
    case 0x01: /* Classic 1K TNP3xxx */
    case 0x08: /* Classic 1K */
    case 0x28: /* Classic EV1 1K */
        *outSectors = 16; *outBlocks = 64;  return true;
    case 0x10: /* Plus 2K SL2 */
    case 0x19: /* Classic 2K */
        *outSectors = 32; *outBlocks = 128; return true;
    case 0x11: /* Plus 4K SL2 */
    case 0x18: /* Classic 4K */
    case 0x38: /* Classic EV1 4K */
        *outSectors = 40; *outBlocks = 256; return true;
    default:   /* unknown -> 1K fallback; caller logs the SAK */
        *outSectors = 16; *outBlocks = 64;  return false;
    }
}

#endif /* NFC_READ_PROGRESS_H_ */
