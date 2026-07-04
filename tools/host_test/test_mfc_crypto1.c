/* See COPYING.txt for license details. */

/*
 * test_mfc_crypto1.c  (HOST TEST)
 *
 * Known-answer vector for the software Crypto-1 forward cipher in the REAL
 * NFC/NFC_drv/legacy/mfc_crypto1.c (compiled against minimal RFAL stubs). It
 * reproduces the encrypted reader-answer {ar} for a published mfkey32v2 vector
 * and asserts it matches, proving crypto1_init / crypto1_word /
 * mfc_prng_successor are correct.
 *
 * Why this exists (Task 5 diagnostic): the MIFARE Classic dictionary dump does
 * not recover sectors on-card. This test isolates the cause to the RF layer
 * (encrypted-parity framing — see the note atop mfc_crypto1.c) by proving the
 * cipher MATH is sound. If the cipher were wrong, this vector would fail here,
 * long before any hardware is involved.
 *
 * Vector source: mfkey32_selftest() in mfkey32.c —
 *   uid 2a234f80,
 *   nt0 240bd022, {nr0} ad2e1687, {ar0} 57e6f7e4,
 *   nt1 18a4bd3e, {nr1} accc1a23, {ar1} 6f10e401,  key a0a1a2a3a4a5
 *
 * The reader answer is {ar} = suc64(nt) XOR ks2, where ks2 is the third cipher
 * word after feeding (uid^nt) then the reader nonce. Feeding the ENCRYPTED
 * {nr} with is_encrypted=1 advances the LFSR identically to feeding plaintext
 * nr with is_encrypted=0, so the published {nr}/{ar} suffice — no plaintext nr
 * needed. This mirrors mfc_auth()'s sequence exactly.
 *
 * M1 Project — host test harness
 */

#include "mfc_crypto1.h"
#include "rfal_rf.h" /* host-test stub: provides ReturnCode for the linker stub */

#include <stdio.h>
#include <stdint.h>

/* The real mfc_crypto1.c references these RF symbols; the cipher-core test
 * never calls them, so provide inert bodies just to satisfy the linker. */
uint32_t HAL_GetTick(void) { return 0; }
ReturnCode rfalTransceiveBlockingTxRx(uint8_t *txBuf, uint16_t txLen,
                                      uint8_t *rxBuf, uint16_t rxBufLen,
                                      uint16_t *actLen, uint32_t flags,
                                      uint32_t fwt)
{
    (void)txBuf; (void)txLen; (void)rxBuf; (void)rxBufLen;
    (void)actLen; (void)flags; (void)fwt;
    return (ReturnCode)-1; /* never invoked by the cipher-core test */
}

static int g_checks = 0;
static int g_failures = 0;

#define CHECK_EQ_U32(actual, expected, msg)                                    \
    do {                                                                       \
        g_checks++;                                                            \
        uint32_t _a = (uint32_t)(actual);                                      \
        uint32_t _e = (uint32_t)(expected);                                    \
        if (_a != _e) {                                                        \
            g_failures++;                                                      \
            printf("  FAIL: %s  (got 0x%08X, want 0x%08X)  (%s:%d)\n",         \
                   (msg), _a, _e, __FILE__, __LINE__);                         \
        }                                                                      \
    } while (0)

#define CHECK_NE_U32(actual, notexpected, msg)                                 \
    do {                                                                       \
        g_checks++;                                                            \
        uint32_t _a = (uint32_t)(actual);                                      \
        uint32_t _n = (uint32_t)(notexpected);                                 \
        if (_a == _n) {                                                        \
            g_failures++;                                                      \
            printf("  FAIL: %s  (got 0x%08X, must differ)  (%s:%d)\n",         \
                   (msg), _a, __FILE__, __LINE__);                             \
        }                                                                      \
    } while (0)

/* Reproduce the encrypted reader answer {ar} using the real cipher core,
 * following mfc_auth()'s exact word sequence. */
static uint32_t compute_ar_enc(uint64_t key, uint32_t uid, uint32_t nt,
                               uint32_t nr_enc)
{
    crypto1_state_t s;
    crypto1_init(&s, key);
    crypto1_word(&s, uid ^ nt, 0);        /* ks0: absorb uid^nt (unencrypted) */
    crypto1_word(&s, nr_enc, 1);          /* ks1: absorb reader nonce (fed encrypted) */
    uint32_t ks2 = crypto1_word(&s, 0, 0);/* ks2: keystream for the answer word */
    return mfc_prng_successor(nt, 64) ^ ks2;
}

int main(void)
{
    const uint64_t KEY = 0xa0a1a2a3a4a5ULL;
    const uint32_t UID = 0x2a234f80u;

    /* Vector 0 */
    CHECK_EQ_U32(compute_ar_enc(KEY, UID, 0x240bd022u, 0xad2e1687u),
                 0x57e6f7e4u, "vector0: {ar0} from key a0a1a2a3a4a5");

    /* Vector 1 (independent nonce pair, same key/uid) */
    CHECK_EQ_U32(compute_ar_enc(KEY, UID, 0x18a4bd3eu, 0xaccc1a23u),
                 0x6f10e401u, "vector1: {ar1} from key a0a1a2a3a4a5");

    /* Discrimination: a wrong key must NOT reproduce the published answer,
     * proving the vectors actually exercise the cipher (not a constant). */
    CHECK_NE_U32(compute_ar_enc(0xffffffffffffULL, UID, 0x240bd022u, 0xad2e1687u),
                 0x57e6f7e4u, "wrong key must not match {ar0}");

    /* PRNG known-answer: suc64 is deterministic and part of the auth math. */
    CHECK_EQ_U32(mfc_prng_successor(0x240bd022u, 0),
                 0x240bd022u, "prng_successor(x,0) is identity");

    if (g_failures == 0) {
        printf("test_mfc_crypto1: PASS (%d checks)\n", g_checks);
        return 0;
    }
    printf("test_mfc_crypto1: FAIL (%d/%d checks failed)\n", g_failures, g_checks);
    return 1;
}
