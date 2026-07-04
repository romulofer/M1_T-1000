/* See COPYING.txt for license details. */

/*
 * test_mfc_sak_layout.c  (HOST TEST)
 *
 * Unit test for the MIFARE Classic SAK -> sector/block layout mapping and the
 * SAK "is Classic" gate. Compiles the REAL shared, dependency-free header
 * NFC/NFC_drv/legacy/nfc_read_progress.h (where mfc_is_classic_sak() and
 * mfc_layout_from_sak() live) and exercises every known SAK variant plus the
 * unknown-SAK fallback. Deferred here from Task 1's acceptance.
 *
 * M1 Project — host test harness
 */

#include "nfc_read_progress.h"

#include <stdio.h>
#include <stdint.h>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK_INT(actual, expected, msg)                                       \
    do {                                                                       \
        g_checks++;                                                            \
        long _a = (long)(actual);                                             \
        long _e = (long)(expected);                                           \
        if (_a != _e) {                                                        \
            g_failures++;                                                      \
            printf("  FAIL: %s  (got %ld, want %ld)  (%s:%d)\n",               \
                   (msg), _a, _e, __FILE__, __LINE__);                         \
        }                                                                      \
    } while (0)

/* Assert a SAK maps to the expected (sectors, blocks) and known flag. */
static void check_layout(uint8_t sak, int wantKnown, uint16_t wantSectors,
                         uint16_t wantBlocks, const char *msg)
{
    uint16_t sectors = 0xFFFF, blocks = 0xFFFF;
    int known = mfc_layout_from_sak(sak, &sectors, &blocks) ? 1 : 0;
    CHECK_INT(known, wantKnown, msg);
    CHECK_INT(sectors, wantSectors, msg);
    CHECK_INT(blocks, wantBlocks, msg);
}

int main(void)
{
    /* is-Classic gate: every known variant is Classic. */
    CHECK_INT(mfc_is_classic_sak(0x08), 1, "SAK 08 (1K) is Classic");
    CHECK_INT(mfc_is_classic_sak(0x18), 1, "SAK 18 (4K) is Classic");
    CHECK_INT(mfc_is_classic_sak(0x09), 1, "SAK 09 (Mini) is Classic");
    CHECK_INT(mfc_is_classic_sak(0x19), 1, "SAK 19 (2K) is Classic");
    CHECK_INT(mfc_is_classic_sak(0x01), 1, "SAK 01 (1K TNP) is Classic");
    CHECK_INT(mfc_is_classic_sak(0x28), 1, "SAK 28 (EV1 1K) is Classic");
    CHECK_INT(mfc_is_classic_sak(0x38), 1, "SAK 38 (EV1 4K) is Classic");
    CHECK_INT(mfc_is_classic_sak(0x10), 1, "SAK 10 (Plus 2K) is Classic");
    CHECK_INT(mfc_is_classic_sak(0x11), 1, "SAK 11 (Plus 4K) is Classic");

    /* Non-Classic SAKs are rejected. */
    CHECK_INT(mfc_is_classic_sak(0x00), 0, "SAK 00 (T2T/UL) not Classic");
    CHECK_INT(mfc_is_classic_sak(0x20), 0, "SAK 20 (T4T/DESFire) not Classic");
    CHECK_INT(mfc_is_classic_sak(0x44), 0, "SAK 44 not Classic");

    /* Layout mapping — known variants. */
    check_layout(0x09, 1,  5,  20, "Mini -> 5 sectors / 20 blocks");
    check_layout(0x08, 1, 16,  64, "1K -> 16 sectors / 64 blocks");
    check_layout(0x01, 1, 16,  64, "1K TNP -> 16/64");
    check_layout(0x28, 1, 16,  64, "EV1 1K -> 16/64");
    check_layout(0x10, 1, 32, 128, "Plus 2K -> 32/128");
    check_layout(0x19, 1, 32, 128, "Classic 2K -> 32/128");
    check_layout(0x11, 1, 40, 256, "Plus 4K -> 40/256");
    check_layout(0x18, 1, 40, 256, "4K -> 40 sectors / 256 blocks");
    check_layout(0x38, 1, 40, 256, "EV1 4K -> 40/256");

    /* Unknown SAK falls back to 1K layout and reports NOT known (caller logs). */
    check_layout(0x00, 0, 16, 64, "unknown SAK -> 1K fallback, known=false");
    check_layout(0x77, 0, 16, 64, "unknown SAK 0x77 -> 1K fallback, known=false");

    if (g_failures == 0) {
        printf("test_mfc_sak_layout: PASS (%d checks)\n", g_checks);
        return 0;
    }
    printf("test_mfc_sak_layout: FAIL (%d/%d checks failed)\n", g_failures, g_checks);
    return 1;
}
