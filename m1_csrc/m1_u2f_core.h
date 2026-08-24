/* See COPYING.txt for license details. */

/*
*
* m1_u2f_core.h
*
* U2F/CTAP1 credential derivation and signing. No HAL/FreeRTOS dependency,
* so this file (and its .c) compile and test directly on the host.
*
* M1 Project
*
*/

#ifndef M1_U2F_CORE_H_
#define M1_U2F_CORE_H_

#include <stdint.h>
#include <stdbool.h>

#define U2F_APP_PARAM_LEN       32
#define U2F_CHALLENGE_LEN       32
#define U2F_KEY_HANDLE_LEN      32
#define U2F_PUBKEY_LEN          65   /* 0x04 || X(32) || Y(32) */
#define U2F_MASTER_SECRET_LEN   32
#define U2F_MAX_SIGNATURE_DER   72   /* SEQUENCE + 2 INTEGERs, 33 bytes worst case each */

/* Per-device secret. Only master_secret needs to be random; the attestation
 * keypair is a fixed batch identity, see m1_u2f_attest_data.h. */
typedef struct
{
	uint8_t master_secret[U2F_MASTER_SECRET_LEN];
} u2f_device_keys_t;

/* Caller-supplied randomness source (device entropy pool). Keeping this a
 * callback instead of touching hardware directly is what keeps this file
 * host-testable. */
typedef void (*u2f_rng_fn)(uint8_t *out, unsigned len);

void u2f_core_init(u2f_rng_fn rng);

/*
 * Handle a REGISTER request: mints a fresh key handle bound to app_param,
 * derives its public key, and signs the standard U2F registration payload
 * with the fixed attestation key. Returns false only on internal crypto
 * failure (should not happen in practice).
 */
bool u2f_register(const u2f_device_keys_t *keys,
                   const uint8_t app_param[U2F_APP_PARAM_LEN],
                   const uint8_t challenge_param[U2F_CHALLENGE_LEN],
                   uint8_t key_handle_out[U2F_KEY_HANDLE_LEN],
                   uint8_t pubkey_out[U2F_PUBKEY_LEN],
                   uint8_t sig_der_out[U2F_MAX_SIGNATURE_DER],
                   uint8_t *sig_der_len_out);

/*
 * Handle an AUTHENTICATE request: recomputes the private key for
 * key_handle, verifies the key handle's integrity tag matches this device
 * and app_param, and signs the standard U2F authentication payload.
 * Returns false if the key handle does not belong to this device/app_param
 * pair (caller should map that to U2F status word SW_WRONG_DATA).
 */
bool u2f_authenticate(const u2f_device_keys_t *keys,
                       const uint8_t app_param[U2F_APP_PARAM_LEN],
                       const uint8_t key_handle[U2F_KEY_HANDLE_LEN],
                       const uint8_t challenge_param[U2F_CHALLENGE_LEN],
                       uint32_t counter,
                       uint8_t sig_der_out[U2F_MAX_SIGNATURE_DER],
                       uint8_t *sig_der_len_out);

/* HMAC-SHA256, exposed for host tests. */
void u2f_hmac_sha256(const uint8_t *key, unsigned key_len,
                      const uint8_t *msg, unsigned msg_len,
                      uint8_t out[32]);

#endif /* M1_U2F_CORE_H_ */
