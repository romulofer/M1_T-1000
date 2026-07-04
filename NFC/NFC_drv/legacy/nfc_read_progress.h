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

#endif /* NFC_READ_PROGRESS_H_ */
