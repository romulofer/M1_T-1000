/* Host tests for m1_u2f_core.c: HMAC-SHA256 known-answer vectors, SHA-256
 * known-answer vectors, and an end-to-end REGISTER/AUTHENTICATE round trip
 * verified against uECC_verify. No hardware, no SD card, no USB. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "m1_u2f_core.h"
#include "m1_u2f_attest_data.h"
#include "sha256.h"
#include "uECC.h"

static int g_failures = 0;

static void check(int cond, const char *what)
{
	if (!cond)
	{
		printf("FAIL: %s\n", what);
		g_failures++;
	}
	else
	{
		printf("ok:   %s\n", what);
	}
}

static void hex_to_bytes(const char *hex, uint8_t *out, unsigned out_len)
{
	unsigned i;
	for (i = 0; i < out_len; i++)
	{
		unsigned byte;
		sscanf(hex + (i * 2), "%2x", &byte);
		out[i] = (uint8_t)byte;
	}
}

/* NIST FIPS 180-2 test vector: SHA256("abc") */
static void test_sha256_abc(void)
{
	SHA256_CTX ctx;
	uint8_t out[32];
	uint8_t expected[32];

	hex_to_bytes("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", expected, 32);

	sha256_init(&ctx);
	sha256_update(&ctx, (const unsigned char *)"abc", 3);
	sha256_final(&ctx, out);

	check(memcmp(out, expected, 32) == 0, "SHA-256(\"abc\") matches NIST vector");
}

/* RFC 4231 test case 1: HMAC-SHA256(key=0x0b*20, data="Hi There") */
static void test_hmac_rfc4231_case1(void)
{
	uint8_t key[20];
	uint8_t out[32];
	uint8_t expected[32];
	unsigned i;

	for (i = 0; i < 20; i++)
		key[i] = 0x0b;

	hex_to_bytes("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", expected, 32);

	u2f_hmac_sha256(key, 20, (const uint8_t *)"Hi There", 8, out);

	check(memcmp(out, expected, 32) == 0, "HMAC-SHA256 RFC4231 case 1");
}

/* RFC 4231 test case 3: HMAC-SHA256(key=0xaa*20, data=0xdd*50) */
static void test_hmac_rfc4231_case3(void)
{
	uint8_t key[20];
	uint8_t data[50];
	uint8_t out[32];
	uint8_t expected[32];
	unsigned i;

	for (i = 0; i < 20; i++)
		key[i] = 0xaa;
	for (i = 0; i < 50; i++)
		data[i] = 0xdd;

	hex_to_bytes("773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe", expected, 32);

	u2f_hmac_sha256(key, 20, data, 50, out);

	check(memcmp(out, expected, 32) == 0, "HMAC-SHA256 RFC4231 case 3");
}

/* Deterministic stub RNG so the round-trip test is reproducible. */
static void stub_rng(uint8_t *out, unsigned len)
{
	unsigned i;
	for (i = 0; i < len; i++)
		out[i] = (uint8_t)(0xA5 ^ i);
}

static void test_register_authenticate_round_trip(void)
{
	u2f_device_keys_t keys;
	uint8_t app_param[U2F_APP_PARAM_LEN];
	uint8_t challenge[U2F_CHALLENGE_LEN];
	uint8_t key_handle[U2F_KEY_HANDLE_LEN];
	uint8_t pubkey[U2F_PUBKEY_LEN];
	uint8_t sig_der[U2F_MAX_SIGNATURE_DER];
	uint8_t sig_len;
	uint8_t auth_challenge[U2F_CHALLENGE_LEN];
	uint8_t auth_sig_der[U2F_MAX_SIGNATURE_DER];
	uint8_t auth_sig_len;
	uint8_t signed_data[U2F_APP_PARAM_LEN + 1 + 4 + U2F_CHALLENGE_LEN];
	uint8_t hash[32];
	uint8_t raw_sig[64];
	SHA256_CTX sctx;
	unsigned pos;
	int ok;
	unsigned i;

	u2f_core_init(stub_rng);

	memset(&keys, 0x42, sizeof(keys));
	for (i = 0; i < U2F_APP_PARAM_LEN; i++)
		app_param[i] = (uint8_t)(i * 3 + 1);
	for (i = 0; i < U2F_CHALLENGE_LEN; i++)
		challenge[i] = (uint8_t)(0xC0 + i);

	ok = u2f_register(&keys, app_param, challenge, key_handle, pubkey, sig_der, &sig_len);
	check(ok, "u2f_register succeeds");
	check(pubkey[0] == 0x04, "public key is uncompressed EC point (0x04 prefix)");

	/* Registration signature verifies under the fixed attestation pubkey. */
	{
		uint8_t reg_signed_data[1 + U2F_APP_PARAM_LEN + U2F_CHALLENGE_LEN + U2F_KEY_HANDLE_LEN + U2F_PUBKEY_LEN];
		uint8_t reg_hash[32];
		unsigned p = 0;

		reg_signed_data[p++] = 0x00;
		memcpy(&reg_signed_data[p], app_param, U2F_APP_PARAM_LEN); p += U2F_APP_PARAM_LEN;
		memcpy(&reg_signed_data[p], challenge, U2F_CHALLENGE_LEN); p += U2F_CHALLENGE_LEN;
		memcpy(&reg_signed_data[p], key_handle, U2F_KEY_HANDLE_LEN); p += U2F_KEY_HANDLE_LEN;
		memcpy(&reg_signed_data[p], pubkey, U2F_PUBKEY_LEN); p += U2F_PUBKEY_LEN;

		sha256_init(&sctx);
		sha256_update(&sctx, reg_signed_data, p);
		sha256_final(&sctx, reg_hash);

		/* sig_der is a DER SEQUENCE; strip it back to raw r||s for uECC_verify
		 * by re-decoding minimally (r,s are the two INTEGERs). This mirrors
		 * what a relying party's crypto library does. */
		{
			unsigned idx = 3; /* skip SEQUENCE tag+len+INTEGER tag */
			unsigned r_len, s_len;
			uint8_t r[32] = {0}, s[32] = {0};

			r_len = sig_der[idx++];
			if (r_len <= 32) memcpy(&r[32 - r_len], &sig_der[idx], r_len);
			else memcpy(r, &sig_der[idx + (r_len - 32)], 32);
			idx += r_len;

			idx++; /* INTEGER tag */
			s_len = sig_der[idx++];
			if (s_len <= 32) memcpy(&s[32 - s_len], &sig_der[idx], s_len);
			else memcpy(s, &sig_der[idx + (s_len - 32)], 32);

			memcpy(raw_sig, r, 32);
			memcpy(raw_sig + 32, s, 32);
		}

		/* REGISTER's signature is made with the fixed attestation key, not
		 * the freshly-derived site pubkey. */
		ok = uECC_verify(&U2F_ATTEST_PUB_KEY[1], reg_hash, 32, raw_sig, uECC_secp256r1());
		check(ok, "registration signature verifies under attestation pubkey");
	}

	/* Authenticate with the returned key handle: must succeed and verify. */
	for (i = 0; i < U2F_CHALLENGE_LEN; i++)
		auth_challenge[i] = (uint8_t)(0x30 + i);

	ok = u2f_authenticate(&keys, app_param, key_handle, auth_challenge, 1, auth_sig_der, &auth_sig_len);
	check(ok, "u2f_authenticate succeeds for the correct device/appParam");

	pos = 0;
	memcpy(&signed_data[pos], app_param, U2F_APP_PARAM_LEN); pos += U2F_APP_PARAM_LEN;
	signed_data[pos++] = 0x01;
	signed_data[pos++] = 0x00; signed_data[pos++] = 0x00; signed_data[pos++] = 0x00; signed_data[pos++] = 0x01;
	memcpy(&signed_data[pos], auth_challenge, U2F_CHALLENGE_LEN); pos += U2F_CHALLENGE_LEN;

	sha256_init(&sctx);
	sha256_update(&sctx, signed_data, pos);
	sha256_final(&sctx, hash);

	{
		unsigned idx = 3;
		unsigned r_len, s_len;
		uint8_t r[32] = {0}, s[32] = {0};

		r_len = auth_sig_der[idx++];
		if (r_len <= 32) memcpy(&r[32 - r_len], &auth_sig_der[idx], r_len);
		else memcpy(r, &auth_sig_der[idx + (r_len - 32)], 32);
		idx += r_len;
		idx++;
		s_len = auth_sig_der[idx++];
		if (s_len <= 32) memcpy(&s[32 - s_len], &auth_sig_der[idx], s_len);
		else memcpy(s, &auth_sig_der[idx + (s_len - 32)], 32);

		memcpy(raw_sig, r, 32);
		memcpy(raw_sig + 32, s, 32);
	}

	ok = uECC_verify(&pubkey[1], hash, 32, raw_sig, uECC_secp256r1()); /* skip 0x04 prefix: uECC wants raw X||Y */
	check(ok, "authenticate signature verifies under the registered pubkey");

	/* Authenticate with a different appParam must be rejected (bad key handle). */
	{
		uint8_t wrong_app_param[U2F_APP_PARAM_LEN];
		uint8_t dummy_sig[U2F_MAX_SIGNATURE_DER];
		uint8_t dummy_len;

		memcpy(wrong_app_param, app_param, U2F_APP_PARAM_LEN);
		wrong_app_param[0] ^= 0xFF;

		ok = u2f_authenticate(&keys, wrong_app_param, key_handle, auth_challenge, 1, dummy_sig, &dummy_len);
		check(!ok, "authenticate rejects a key handle from a different appParam");
	}
}

int main(void)
{
	test_sha256_abc();
	test_hmac_rfc4231_case1();
	test_hmac_rfc4231_case3();
	test_register_authenticate_round_trip();

	if (g_failures == 0)
	{
		printf("All U2F core tests passed.\n");
		return 0;
	}

	printf("%d test(s) FAILED.\n", g_failures);
	return 1;
}
