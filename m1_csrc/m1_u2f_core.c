/* See COPYING.txt for license details. */

/*
*
* m1_u2f_core.c
*
* U2F/CTAP1 credential derivation and signing.
*
* Key handles are not stored anywhere: key_handle = nonce(16) || tag(16),
* where tag = HMAC-SHA256(masterSecret, 0x01 || appParam || nonce)[0:16].
* The per-site private key is re-derived on demand as
* HMAC-SHA256(masterSecret, 0x02 || appParam || nonce || retry), which is
* the seed used directly as the P-256 scalar (retried with an incrementing
* byte on the vanishingly rare chance the seed is not a valid scalar).
* This means only one 32-byte secret (master_secret) ever needs to persist;
* see m1_u2f.c for where it is generated and stored.
*
* The attestation keypair used to sign REGISTER responses is a fixed batch
* identity (m1_u2f_attest_data.h), not derived from master_secret.
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <string.h>
#include "m1_u2f_core.h"
#include "m1_u2f_attest_data.h"
#include "uECC.h"
#include "sha256.h"

/*************************** D E F I N E S ************************************/

#define U2F_NONCE_LEN         16
#define U2F_TAG_LEN           16
#define U2F_MAC_DOMAIN        0x01U
#define U2F_PRIV_DOMAIN       0x02U
#define U2F_PRIV_DERIVE_MAX_RETRY  8U

/* SHA-256 internal transform block size (not to be confused with the
 * vendored sha256.h's misleadingly-named SHA256_BLOCK_SIZE, which is
 * actually the 32-byte digest size). */
#define SHA256_HMAC_BLOCK_SIZE  64U

/***************************** V A R I A B L E S ******************************/

static u2f_rng_fn s_rng = NULL;

/* uECC hash context wired to the vendored sha256.c, per the pattern
 * documented in uECC.h's uECC_sign_deterministic() comment block. */
typedef struct
{
	uECC_HashContext uecc;
	SHA256_CTX ctx;
} u2f_sha256_hash_context_t;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void hash_ctx_init(const uECC_HashContext *base);
static void hash_ctx_update(const uECC_HashContext *base, const uint8_t *msg, unsigned msg_size);
static void hash_ctx_finish(const uECC_HashContext *base, uint8_t *hash_result);
static unsigned der_encode_int(const uint8_t val[32], uint8_t *out);
static void der_encode_signature(const uint8_t sig[64], uint8_t *out, uint8_t *out_len);
static bool sign_hash(const uint8_t priv[32], const uint8_t hash[32],
                       uint8_t sig_der_out[U2F_MAX_SIGNATURE_DER], uint8_t *sig_der_len_out);
static bool derive_priv_key(const uint8_t master_secret[U2F_MASTER_SECRET_LEN],
                             const uint8_t app_param[U2F_APP_PARAM_LEN],
                             const uint8_t nonce[U2F_NONCE_LEN],
                             uint8_t priv_out[32]);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

void u2f_core_init(u2f_rng_fn rng)
{
	s_rng = rng;
}

/*============================================================================*/
/* HMAC-SHA256 (RFC 2104), built on the vendored sha256.c primitives.        */
/*============================================================================*/
void u2f_hmac_sha256(const uint8_t *key, unsigned key_len,
                      const uint8_t *msg, unsigned msg_len,
                      uint8_t out[32])
{
	SHA256_CTX ctx;
	uint8_t block_key[SHA256_HMAC_BLOCK_SIZE];
	uint8_t ipad[SHA256_HMAC_BLOCK_SIZE];
	uint8_t opad[SHA256_HMAC_BLOCK_SIZE];
	uint8_t inner_hash[32];
	unsigned i;

	memset(block_key, 0, sizeof(block_key));
	if (key_len > SHA256_HMAC_BLOCK_SIZE)
	{
		sha256_init(&ctx);
		sha256_update(&ctx, key, key_len);
		sha256_final(&ctx, block_key);
	}
	else
	{
		memcpy(block_key, key, key_len);
	}

	for (i = 0; i < SHA256_HMAC_BLOCK_SIZE; i++)
	{
		ipad[i] = block_key[i] ^ 0x36U;
		opad[i] = block_key[i] ^ 0x5CU;
	}

	sha256_init(&ctx);
	sha256_update(&ctx, ipad, sizeof(ipad));
	sha256_update(&ctx, msg, msg_len);
	sha256_final(&ctx, inner_hash);

	sha256_init(&ctx);
	sha256_update(&ctx, opad, sizeof(opad));
	sha256_update(&ctx, inner_hash, sizeof(inner_hash));
	sha256_final(&ctx, out);
}

/*============================================================================*/
/* uECC_HashContext glue: SHA-256, block_size=64, result_size=32.           */
/*============================================================================*/
static void hash_ctx_init(const uECC_HashContext *base)
{
	u2f_sha256_hash_context_t *hc = (u2f_sha256_hash_context_t *)base;
	sha256_init(&hc->ctx);
}

static void hash_ctx_update(const uECC_HashContext *base, const uint8_t *msg, unsigned msg_size)
{
	u2f_sha256_hash_context_t *hc = (u2f_sha256_hash_context_t *)base;
	sha256_update(&hc->ctx, msg, msg_size);
}

static void hash_ctx_finish(const uECC_HashContext *base, uint8_t *hash_result)
{
	u2f_sha256_hash_context_t *hc = (u2f_sha256_hash_context_t *)base;
	sha256_final(&hc->ctx, hash_result);
}

/*============================================================================*/
/* DER encode one 32-byte big-endian unsigned integer (ASN.1 INTEGER,       */
/* minimal length, leading 0x00 inserted only when the high bit is set).    */
/*============================================================================*/
static unsigned der_encode_int(const uint8_t val[32], uint8_t *out)
{
	unsigned skip = 0;
	unsigned len;
	unsigned pos = 0;

	while (skip < 31 && val[skip] == 0U)
		skip++;
	len = 32U - skip;

	out[pos++] = 0x02U; /* INTEGER */
	if (val[skip] & 0x80U)
	{
		out[pos++] = (uint8_t)(len + 1U);
		out[pos++] = 0x00U;
	}
	else
	{
		out[pos++] = (uint8_t)len;
	}
	memcpy(&out[pos], &val[skip], len);
	pos += len;

	return pos;
}

static void der_encode_signature(const uint8_t sig[64], uint8_t *out, uint8_t *out_len)
{
	uint8_t tmp[70]; /* r + s, 35 bytes worst case each */
	unsigned r_len = der_encode_int(&sig[0], tmp);
	unsigned s_len = der_encode_int(&sig[32], &tmp[r_len]);
	unsigned total = r_len + s_len;
	unsigned pos = 0;

	out[pos++] = 0x30U; /* SEQUENCE */
	out[pos++] = (uint8_t)total; /* total is always < 128 for P-256 */
	memcpy(&out[pos], tmp, total);
	pos += total;

	*out_len = (uint8_t)pos;
}

/*============================================================================*/
/* Sign a 32-byte hash with a P-256 private key, RFC6979 deterministic      */
/* nonce (no RNG dependency for signing), DER-encoded output.               */
/*============================================================================*/
static bool sign_hash(const uint8_t priv[32], const uint8_t hash[32],
                       uint8_t sig_der_out[U2F_MAX_SIGNATURE_DER], uint8_t *sig_der_len_out)
{
	uint8_t sig_raw[64];
	uint8_t tmp[2U * 32U + 64U];
	u2f_sha256_hash_context_t hctx;

	hctx.uecc.init_hash = hash_ctx_init;
	hctx.uecc.update_hash = hash_ctx_update;
	hctx.uecc.finish_hash = hash_ctx_finish;
	hctx.uecc.block_size = 64;
	hctx.uecc.result_size = 32;
	hctx.uecc.tmp = tmp;

	if (!uECC_sign_deterministic(priv, hash, 32, &hctx.uecc, sig_raw, uECC_secp256r1()))
		return false;

	der_encode_signature(sig_raw, sig_der_out, sig_der_len_out);
	return true;
}

/*============================================================================*/
/* Derive the per-site private key scalar for (master_secret, app_param,    */
/* nonce). Retries with an incrementing domain byte on the negligible       */
/* chance the HMAC output is not a valid P-256 scalar.                      */
/*============================================================================*/
static bool derive_priv_key(const uint8_t master_secret[U2F_MASTER_SECRET_LEN],
                             const uint8_t app_param[U2F_APP_PARAM_LEN],
                             const uint8_t nonce[U2F_NONCE_LEN],
                             uint8_t priv_out[32])
{
	uint8_t msg[1 + U2F_APP_PARAM_LEN + U2F_NONCE_LEN + 1];
	uint8_t pub_scratch[U2F_PUBKEY_LEN];
	uint8_t retry;

	msg[0] = U2F_PRIV_DOMAIN;
	memcpy(&msg[1], app_param, U2F_APP_PARAM_LEN);
	memcpy(&msg[1 + U2F_APP_PARAM_LEN], nonce, U2F_NONCE_LEN);

	for (retry = 0; retry < U2F_PRIV_DERIVE_MAX_RETRY; retry++)
	{
		msg[1 + U2F_APP_PARAM_LEN + U2F_NONCE_LEN] = retry;
		u2f_hmac_sha256(master_secret, U2F_MASTER_SECRET_LEN, msg, sizeof(msg), priv_out);

		if (uECC_compute_public_key(priv_out, pub_scratch, uECC_secp256r1()))
			return true;
	}

	return false;
}

/*============================================================================*/
bool u2f_register(const u2f_device_keys_t *keys,
                   const uint8_t app_param[U2F_APP_PARAM_LEN],
                   const uint8_t challenge_param[U2F_CHALLENGE_LEN],
                   uint8_t key_handle_out[U2F_KEY_HANDLE_LEN],
                   uint8_t pubkey_out[U2F_PUBKEY_LEN],
                   uint8_t sig_der_out[U2F_MAX_SIGNATURE_DER],
                   uint8_t *sig_der_len_out)
{
	uint8_t nonce[U2F_NONCE_LEN];
	uint8_t mac_msg[1 + U2F_APP_PARAM_LEN + U2F_NONCE_LEN];
	uint8_t tag[32];
	uint8_t priv[32];
	uint8_t signed_data[1 + U2F_APP_PARAM_LEN + U2F_CHALLENGE_LEN + U2F_KEY_HANDLE_LEN + U2F_PUBKEY_LEN];
	uint8_t hash[32];
	SHA256_CTX sctx;
	unsigned pos;

	if (s_rng == NULL)
		return false;

	s_rng(nonce, U2F_NONCE_LEN);

	mac_msg[0] = U2F_MAC_DOMAIN;
	memcpy(&mac_msg[1], app_param, U2F_APP_PARAM_LEN);
	memcpy(&mac_msg[1 + U2F_APP_PARAM_LEN], nonce, U2F_NONCE_LEN);
	u2f_hmac_sha256(keys->master_secret, U2F_MASTER_SECRET_LEN, mac_msg, sizeof(mac_msg), tag);

	memcpy(key_handle_out, nonce, U2F_NONCE_LEN);
	memcpy(&key_handle_out[U2F_NONCE_LEN], tag, U2F_TAG_LEN);

	if (!derive_priv_key(keys->master_secret, app_param, nonce, priv))
		return false;

	/* uECC's public key format is raw X||Y (64 bytes, no prefix); the U2F
	 * wire format is the ANSI X9.62 uncompressed point (0x04 || X || Y). */
	pubkey_out[0] = 0x04U;
	if (!uECC_compute_public_key(priv, &pubkey_out[1], uECC_secp256r1()))
		return false;

	/* Registration response signed data: 0x00 || appParam || challengeParam
	 * || keyHandle || pubKey, signed with the fixed attestation key. */
	pos = 0;
	signed_data[pos++] = 0x00U;
	memcpy(&signed_data[pos], app_param, U2F_APP_PARAM_LEN); pos += U2F_APP_PARAM_LEN;
	memcpy(&signed_data[pos], challenge_param, U2F_CHALLENGE_LEN); pos += U2F_CHALLENGE_LEN;
	memcpy(&signed_data[pos], key_handle_out, U2F_KEY_HANDLE_LEN); pos += U2F_KEY_HANDLE_LEN;
	memcpy(&signed_data[pos], pubkey_out, U2F_PUBKEY_LEN); pos += U2F_PUBKEY_LEN;

	sha256_init(&sctx);
	sha256_update(&sctx, signed_data, pos);
	sha256_final(&sctx, hash);

	return sign_hash(U2F_ATTEST_PRIV_KEY, hash, sig_der_out, sig_der_len_out);
}

/*============================================================================*/
bool u2f_authenticate(const u2f_device_keys_t *keys,
                       const uint8_t app_param[U2F_APP_PARAM_LEN],
                       const uint8_t key_handle[U2F_KEY_HANDLE_LEN],
                       const uint8_t challenge_param[U2F_CHALLENGE_LEN],
                       uint32_t counter,
                       uint8_t sig_der_out[U2F_MAX_SIGNATURE_DER],
                       uint8_t *sig_der_len_out)
{
	const uint8_t *nonce = &key_handle[0];
	const uint8_t *tag_in = &key_handle[U2F_NONCE_LEN];
	uint8_t mac_msg[1 + U2F_APP_PARAM_LEN + U2F_NONCE_LEN];
	uint8_t tag_expected[32];
	uint8_t priv[32];
	uint8_t signed_data[U2F_APP_PARAM_LEN + 1 + 4 + U2F_CHALLENGE_LEN];
	uint8_t hash[32];
	uint8_t diff;
	unsigned i;
	unsigned pos;
	SHA256_CTX sctx;

	mac_msg[0] = U2F_MAC_DOMAIN;
	memcpy(&mac_msg[1], app_param, U2F_APP_PARAM_LEN);
	memcpy(&mac_msg[1 + U2F_APP_PARAM_LEN], nonce, U2F_NONCE_LEN);
	u2f_hmac_sha256(keys->master_secret, U2F_MASTER_SECRET_LEN, mac_msg, sizeof(mac_msg), tag_expected);

	/* Constant-time compare of the 16-byte tag. */
	diff = 0;
	for (i = 0; i < U2F_TAG_LEN; i++)
		diff |= (uint8_t)(tag_expected[i] ^ tag_in[i]);
	if (diff != 0U)
		return false;

	if (!derive_priv_key(keys->master_secret, app_param, nonce, priv))
		return false;

	pos = 0;
	memcpy(&signed_data[pos], app_param, U2F_APP_PARAM_LEN); pos += U2F_APP_PARAM_LEN;
	signed_data[pos++] = 0x01U; /* user presence: verified by menu entry */
	signed_data[pos++] = (uint8_t)(counter >> 24);
	signed_data[pos++] = (uint8_t)(counter >> 16);
	signed_data[pos++] = (uint8_t)(counter >> 8);
	signed_data[pos++] = (uint8_t)(counter);
	memcpy(&signed_data[pos], challenge_param, U2F_CHALLENGE_LEN); pos += U2F_CHALLENGE_LEN;

	sha256_init(&sctx);
	sha256_update(&sctx, signed_data, pos);
	sha256_final(&sctx, hash);

	return sign_hash(priv, hash, sig_der_out, sig_der_len_out);
}
