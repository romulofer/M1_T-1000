/* See COPYING.txt for license details. */

/*
*
* m1_u2f.c
*
* U2F/CTAP1 authenticator: menu entry, CTAPHID transport over USB, and
* encrypted on-SD storage of the per-device master secret.
*
* USB is switched to CTAPHID mode exclusively for the duration of the menu
* screen (same pattern as BadUSB's HID keyboard mode); only REGISTER,
* AUTHENTICATE, and VERSION are implemented (CTAP1/U2F, no CTAP2/CBOR/PIN).
* See m1_u2f_core.c for credential derivation and signing, and
* m1_u2f_attest_data.h for the fixed batch attestation identity.
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"
#include "stm32h5xx_hal.h"
#include "m1_display.h"
#include "m1_usb_cdc_msc.h"
#include "m1_menu.h"
#include "m1_tasks.h"
#include "m1_system.h"
#include "m1_compile_cfg.h"
#include "m1_crypto.h"
#include "m1_u2f.h"
#include "m1_u2f_core.h"
#include "m1_u2f_attest_data.h"
#include "usbd_u2f.h"
#include "sha256.h"
#include "ff.h"
#include "m1_log_debug.h"
#include <string.h>
#include <stdio.h>

#ifdef M1_APP_U2F_ENABLE

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG  "U2F"

#define U2F_KEY_FILE_PATH        "0:/System/u2f.key"
#define U2F_STORE_PLAIN_LEN      (U2F_MASTER_SECRET_LEN + 4U) /* secret + counter */
#define U2F_STORE_WORK_BUF_SIZE  96U

/* STM32H5 96-bit unique device ID, same fixed address m1_crypto.c reads. */
#define U2F_UID_WORD0  (*(volatile uint32_t *)(0x08FFF800))
#define U2F_UID_WORD1  (*(volatile uint32_t *)(0x08FFF804))
#define U2F_UID_WORD2  (*(volatile uint32_t *)(0x08FFF808))

#define CTAPHID_BROADCAST_CID   0xFFFFFFFFU
#define CTAPHID_CMD_MSG         0x83U
#define CTAPHID_CMD_INIT        0x86U
#define CTAPHID_CMD_PING        0x81U
#define CTAPHID_CMD_ERROR       0xBFU
#define CTAPHID_INIT_NONCE_LEN  8U
#define CTAPHID_MAX_MSG         512U
#define CTAPHID_INIT_PAYLOAD    57U /* 64 - 7 byte header */
#define CTAPHID_CONT_PAYLOAD    59U /* 64 - 5 byte header */

#define CTAP1_INS_REGISTER       0x01U
#define CTAP1_INS_AUTHENTICATE   0x02U
#define CTAP1_INS_VERSION        0x03U
#define CTAP1_P1_CHECK_ONLY      0x07U

#define U2F_SW_NO_ERROR                   0x9000U
#define U2F_SW_CONDITIONS_NOT_SATISFIED   0x6985U
#define U2F_SW_WRONG_DATA                 0x6A80U
#define U2F_SW_INS_NOT_SUPPORTED          0x6D00U
#define U2F_SW_WRONG_LENGTH               0x6700U

/* Response worst case: 0x05 + pubkey(65) + handleLen(1) + handle(32) +
 * attestation cert(514) + DER signature(<=72) + status word(2) */
#define U2F_RESP_BUF_SIZE  700U

//****************************** V A R I A B L E S *****************************/

static volatile bool s_pkt_pending;
static uint8_t s_pkt_buf[64];

static uint32_t s_next_cid = 1;
static uint32_t s_active_cid;

static u2f_device_keys_t s_keys;
static uint32_t s_sign_counter;

static uint8_t s_resp_buf[U2F_RESP_BUF_SIZE];

typedef struct
{
	bool assembling;
	uint32_t cid;
	uint16_t total_len;
	uint16_t recv_len;
	uint8_t next_seq;
	uint8_t buf[CTAPHID_MAX_MSG];
} ctaphid_reassembly_t;

static ctaphid_reassembly_t s_reasm;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void ctaphid_process_apdu(uint32_t cid, const uint8_t *apdu, uint16_t apdu_len);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/* Encrypted SD storage: same AES-256-CBC + length-prefixed-record pattern  */
/* m1_wifi_cred.c already uses for on-SD secrets.                           */
/*============================================================================*/
static bool u2f_storage_load(void)
{
	FIL file;
	FRESULT res;
	UINT br;
	uint8_t len_buf[4];
	uint32_t enc_len;
	uint32_t plain_len;
	static uint8_t work[U2F_STORE_WORK_BUF_SIZE];

	res = f_open(&file, U2F_KEY_FILE_PATH, FA_READ);
	if (res != FR_OK)
		return false;

	res = f_read(&file, len_buf, 4, &br);
	if (res != FR_OK || br != 4)
	{
		f_close(&file);
		return false;
	}

	enc_len = (uint32_t)len_buf[0] | ((uint32_t)len_buf[1] << 8) |
	          ((uint32_t)len_buf[2] << 16) | ((uint32_t)len_buf[3] << 24);
	if (enc_len == 0U || enc_len > sizeof(work))
	{
		f_close(&file);
		return false;
	}

	res = f_read(&file, work, enc_len, &br);
	f_close(&file);
	if (res != FR_OK || br != enc_len)
		return false;

	plain_len = m1_crypto_decrypt(work, enc_len);
	if (plain_len != U2F_STORE_PLAIN_LEN)
	{
		memset(work, 0, sizeof(work));
		return false;
	}

	memcpy(s_keys.master_secret, work, U2F_MASTER_SECRET_LEN);
	memcpy(&s_sign_counter, &work[U2F_MASTER_SECRET_LEN], 4);
	memset(work, 0, sizeof(work));

	return true;
}

static bool u2f_storage_save(void)
{
	FIL file;
	FRESULT res;
	UINT bw;
	uint8_t len_buf[4];
	uint32_t enc_len;
	static uint8_t work[U2F_STORE_WORK_BUF_SIZE];

	f_mkdir("0:/System");

	memcpy(work, s_keys.master_secret, U2F_MASTER_SECRET_LEN);
	memcpy(&work[U2F_MASTER_SECRET_LEN], &s_sign_counter, 4);

	enc_len = m1_crypto_encrypt(work, U2F_STORE_PLAIN_LEN, sizeof(work));
	if (enc_len == 0U)
	{
		memset(work, 0, sizeof(work));
		return false;
	}

	res = f_open(&file, U2F_KEY_FILE_PATH, FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
	{
		memset(work, 0, sizeof(work));
		return false;
	}

	len_buf[0] = (uint8_t)(enc_len & 0xFFU);
	len_buf[1] = (uint8_t)((enc_len >> 8) & 0xFFU);
	len_buf[2] = (uint8_t)((enc_len >> 16) & 0xFFU);
	len_buf[3] = (uint8_t)((enc_len >> 24) & 0xFFU);

	res = f_write(&file, len_buf, 4, &bw);
	if (res == FR_OK && bw == 4U)
		res = f_write(&file, work, enc_len, &bw);

	f_close(&file);
	memset(work, 0, sizeof(work));

	return (res == FR_OK && bw == enc_len);
}

/*============================================================================*/
/* One-time master secret generation (first use only). No HAL_RNG driver is */
/* vendored in this project, so this seeds from the device UID + boot tick  */
/* + accumulated DWT cycle-counter jitter, whitened through SHA-256. Not a  */
/* certified TRNG; documented as a known limitation. The per-REGISTER nonce */
/* below is a much lighter-weight draw from the same style of source, which */
/* is fine there because the nonce's role is uniqueness, not secrecy -- the */
/* actual secret is master_secret, generated exactly once, here.           */
/*============================================================================*/
static void u2f_generate_master_secret(void)
{
	SHA256_CTX ctx;
	uint32_t val;
	unsigned i;

	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

	sha256_init(&ctx);

	val = U2F_UID_WORD0; sha256_update(&ctx, (const uint8_t *)&val, 4);
	val = U2F_UID_WORD1; sha256_update(&ctx, (const uint8_t *)&val, 4);
	val = U2F_UID_WORD2; sha256_update(&ctx, (const uint8_t *)&val, 4);
	val = HAL_GetTick();  sha256_update(&ctx, (const uint8_t *)&val, 4);

	for (i = 0; i < 32; i++)
	{
		uint32_t start = DWT->CYCCNT;
		while ((DWT->CYCCNT - start) < 5000U) {} /* let jitter accumulate */
		val = DWT->CYCCNT ^ HAL_GetTick();
		sha256_update(&ctx, (const uint8_t *)&val, 4);
	}

	sha256_final(&ctx, s_keys.master_secret);
	s_sign_counter = 0;
}

/* Per-REGISTER key-handle nonce. Reuses m1_crypto_generate_iv()'s existing
 * UID+tick+counter construction (m1_crypto.c) -- same entropy model already
 * shipping in this codebase for WiFi credential IVs. */
static void u2f_rng_for_core(uint8_t *out, unsigned len)
{
	uint8_t iv[M1_CRYPTO_IV_SIZE];
	unsigned copied = 0;

	while (copied < len)
	{
		unsigned chunk = (len - copied) < M1_CRYPTO_IV_SIZE ? (len - copied) : M1_CRYPTO_IV_SIZE;
		m1_crypto_generate_iv(iv);
		memcpy(&out[copied], iv, chunk);
		copied += chunk;
	}
}

/*============================================================================*/
/* CTAPHID packet I/O                                                       */
/*============================================================================*/
static void u2f_send_packet(const uint8_t *pkt)
{
	uint32_t start = HAL_GetTick();

	while (!USBD_U2F_IsIdle())
	{
		if ((HAL_GetTick() - start) > 500U)
			return; /* host likely gone; drop rather than hang the menu loop */
	}
	USBD_U2F_SendReport(&hUsbDeviceFS, (uint8_t *)pkt, 64);
}

static void u2f_send_message(uint32_t cid, uint8_t cmd, const uint8_t *data, uint16_t len)
{
	uint8_t pkt[64];
	uint16_t sent;
	uint8_t seq = 0;
	uint16_t chunk;

	memset(pkt, 0, sizeof(pkt));
	pkt[0] = (uint8_t)(cid >> 24);
	pkt[1] = (uint8_t)(cid >> 16);
	pkt[2] = (uint8_t)(cid >> 8);
	pkt[3] = (uint8_t)(cid);
	pkt[4] = cmd;
	pkt[5] = (uint8_t)(len >> 8);
	pkt[6] = (uint8_t)(len);

	chunk = (len < CTAPHID_INIT_PAYLOAD) ? len : CTAPHID_INIT_PAYLOAD;
	memcpy(&pkt[7], data, chunk);
	u2f_send_packet(pkt);
	sent = chunk;

	while (sent < len)
	{
		{
			uint16_t remaining = (uint16_t)(len - sent);
			chunk = (remaining < CTAPHID_CONT_PAYLOAD) ? remaining : CTAPHID_CONT_PAYLOAD;
		}
		memset(pkt, 0, sizeof(pkt));
		pkt[0] = (uint8_t)(cid >> 24);
		pkt[1] = (uint8_t)(cid >> 16);
		pkt[2] = (uint8_t)(cid >> 8);
		pkt[3] = (uint8_t)(cid);
		pkt[4] = seq & 0x7FU;
		memcpy(&pkt[5], &data[sent], chunk);
		u2f_send_packet(pkt);
		sent = (uint16_t)(sent + chunk);
		seq++;
	}
}

static void ctaphid_send_error(uint32_t cid, uint8_t code)
{
	u2f_send_message(cid, CTAPHID_CMD_ERROR, &code, 1);
}

static void u2f_rx_isr(const uint8_t *data, uint16_t len)
{
	/* Single-slot mailbox: CTAPHID is request/response over one channel at
	 * a time for our single-client use case, so the main loop always drains
	 * one packet well before the host could send an unrelated next one. */
	uint16_t copy_len = (len < sizeof(s_pkt_buf)) ? len : (uint16_t)sizeof(s_pkt_buf);
	memcpy(s_pkt_buf, data, copy_len);
	s_pkt_pending = true;
}

static void ctaphid_handle_init_packet(const uint8_t *pkt)
{
	uint32_t cid = ((uint32_t)pkt[0] << 24) | ((uint32_t)pkt[1] << 16) |
	               ((uint32_t)pkt[2] << 8) | pkt[3];
	uint8_t cmd = pkt[4];
	uint16_t bcnt = (uint16_t)(((uint16_t)pkt[5] << 8) | pkt[6]);

	if (cmd == CTAPHID_CMD_INIT)
	{
		uint8_t resp[17];
		uint32_t new_cid = (cid == CTAPHID_BROADCAST_CID) ? s_next_cid++ : cid;
		s_active_cid = new_cid;

		memcpy(resp, &pkt[7], CTAPHID_INIT_NONCE_LEN);
		resp[8]  = (uint8_t)(new_cid >> 24);
		resp[9]  = (uint8_t)(new_cid >> 16);
		resp[10] = (uint8_t)(new_cid >> 8);
		resp[11] = (uint8_t)(new_cid);
		resp[12] = 2; /* CTAPHID protocol version */
		resp[13] = 1; /* device version major */
		resp[14] = 0; /* device version minor */
		resp[15] = 0; /* device version build */
		resp[16] = 0; /* capabilities: none */

		u2f_send_message(cid, CTAPHID_CMD_INIT, resp, sizeof(resp));
		s_reasm.assembling = false;
		return;
	}

	if (cid != s_active_cid)
		return; /* traffic on a channel we never allocated: ignore */

	if (cmd == CTAPHID_CMD_PING)
	{
		uint16_t chunk = (bcnt < CTAPHID_INIT_PAYLOAD) ? bcnt : CTAPHID_INIT_PAYLOAD;
		u2f_send_message(cid, CTAPHID_CMD_PING, &pkt[7], chunk);
		s_reasm.assembling = false;
		return;
	}

	if (cmd != CTAPHID_CMD_MSG || bcnt > CTAPHID_MAX_MSG)
	{
		ctaphid_send_error(cid, 0x01U); /* ERR_INVALID_CMD / ERR_INVALID_LEN */
		s_reasm.assembling = false;
		return;
	}

	s_reasm.cid = cid;
	s_reasm.total_len = bcnt;
	s_reasm.next_seq = 0;
	{
		uint16_t chunk = (bcnt < CTAPHID_INIT_PAYLOAD) ? bcnt : CTAPHID_INIT_PAYLOAD;
		memcpy(s_reasm.buf, &pkt[7], chunk);
		s_reasm.recv_len = chunk;
	}
	s_reasm.assembling = (s_reasm.recv_len < s_reasm.total_len);

	if (!s_reasm.assembling)
		ctaphid_process_apdu(cid, s_reasm.buf, s_reasm.total_len);
}

static void ctaphid_handle_cont_packet(const uint8_t *pkt)
{
	uint32_t cid = ((uint32_t)pkt[0] << 24) | ((uint32_t)pkt[1] << 16) |
	               ((uint32_t)pkt[2] << 8) | pkt[3];
	uint8_t seq = pkt[4];
	uint16_t remaining;
	uint16_t chunk;

	if (!s_reasm.assembling || cid != s_reasm.cid || seq != s_reasm.next_seq)
		return; /* stray/out-of-order continuation: drop silently */

	remaining = (uint16_t)(s_reasm.total_len - s_reasm.recv_len);
	chunk = (remaining < CTAPHID_CONT_PAYLOAD) ? remaining : CTAPHID_CONT_PAYLOAD;
	memcpy(&s_reasm.buf[s_reasm.recv_len], &pkt[5], chunk);
	s_reasm.recv_len = (uint16_t)(s_reasm.recv_len + chunk);
	s_reasm.next_seq++;

	if (s_reasm.recv_len >= s_reasm.total_len)
	{
		s_reasm.assembling = false;
		ctaphid_process_apdu(s_reasm.cid, s_reasm.buf, s_reasm.total_len);
	}
}

static void u2f_process_packet(const uint8_t *pkt)
{
	if (pkt[4] & 0x80U)
		ctaphid_handle_init_packet(pkt);
	else
		ctaphid_handle_cont_packet(pkt);
}

/*============================================================================*/
/* CTAP1/U2F command dispatch (ISO7816-style APDU carried inside a          */
/* CTAPHID_MSG payload).                                                    */
/*============================================================================*/
static void ctaphid_process_apdu(uint32_t cid, const uint8_t *apdu, uint16_t apdu_len)
{
	uint8_t ins, p1;
	uint16_t offset, lc;
	const uint8_t *data;
	uint16_t sw = U2F_SW_INS_NOT_SUPPORTED;
	uint16_t resp_len = 0;

	if (apdu_len < 4U)
	{
		sw = U2F_SW_WRONG_LENGTH;
		goto respond;
	}

	ins = apdu[1];
	p1 = apdu[2];
	offset = 4;
	lc = 0;

	if (apdu_len > offset)
	{
		if (apdu[offset] == 0x00U && apdu_len >= (uint16_t)(offset + 3))
		{
			lc = (uint16_t)(((uint16_t)apdu[offset + 1] << 8) | apdu[offset + 2]);
			offset = (uint16_t)(offset + 3);
		}
		else
		{
			lc = apdu[offset];
			offset = (uint16_t)(offset + 1);
		}
	}
	data = &apdu[offset];
	if ((uint16_t)(offset + lc) > apdu_len)
		lc = 0; /* malformed length: treat as empty instead of reading OOB */

	switch (ins)
	{
		case CTAP1_INS_VERSION:
			memcpy(s_resp_buf, "U2F_V2", 6);
			resp_len = 6;
			sw = U2F_SW_NO_ERROR;
			break;

		case CTAP1_INS_REGISTER:
		{
			/* Request layout: challengeParam(32) || appParam(32) */
			uint8_t key_handle[U2F_KEY_HANDLE_LEN];
			uint8_t pubkey[U2F_PUBKEY_LEN];
			uint8_t sig_der[U2F_MAX_SIGNATURE_DER];
			uint8_t sig_len;

			if (lc != (U2F_CHALLENGE_LEN + U2F_APP_PARAM_LEN))
			{
				sw = U2F_SW_WRONG_LENGTH;
				break;
			}

			if (!u2f_register(&s_keys, &data[U2F_CHALLENGE_LEN], data,
			                   key_handle, pubkey, sig_der, &sig_len))
			{
				sw = U2F_SW_WRONG_DATA;
				break;
			}

			resp_len = 0;
			s_resp_buf[resp_len++] = 0x05U;
			memcpy(&s_resp_buf[resp_len], pubkey, U2F_PUBKEY_LEN); resp_len = (uint16_t)(resp_len + U2F_PUBKEY_LEN);
			s_resp_buf[resp_len++] = U2F_KEY_HANDLE_LEN;
			memcpy(&s_resp_buf[resp_len], key_handle, U2F_KEY_HANDLE_LEN); resp_len = (uint16_t)(resp_len + U2F_KEY_HANDLE_LEN);
			memcpy(&s_resp_buf[resp_len], U2F_ATTEST_CERT_DER, U2F_ATTEST_CERT_LEN); resp_len = (uint16_t)(resp_len + U2F_ATTEST_CERT_LEN);
			memcpy(&s_resp_buf[resp_len], sig_der, sig_len); resp_len = (uint16_t)(resp_len + sig_len);
			sw = U2F_SW_NO_ERROR;
			break;
		}

		case CTAP1_INS_AUTHENTICATE:
		{
			/* Request layout: challengeParam(32) || appParam(32) ||
			 * keyHandleLen(1) || keyHandle(keyHandleLen) */
			uint8_t key_handle_len;
			const uint8_t *challenge_param;
			const uint8_t *app_param;
			const uint8_t *key_handle;
			uint8_t sig_der[U2F_MAX_SIGNATURE_DER];
			uint8_t sig_len;

			if (lc < (uint16_t)(U2F_CHALLENGE_LEN + U2F_APP_PARAM_LEN + 1U))
			{
				sw = U2F_SW_WRONG_LENGTH;
				break;
			}

			challenge_param = &data[0];
			app_param = &data[U2F_CHALLENGE_LEN];
			key_handle_len = data[U2F_CHALLENGE_LEN + U2F_APP_PARAM_LEN];
			key_handle = &data[U2F_CHALLENGE_LEN + U2F_APP_PARAM_LEN + 1U];

			if (key_handle_len != U2F_KEY_HANDLE_LEN)
			{
				sw = U2F_SW_WRONG_DATA;
				break;
			}

			if (!u2f_authenticate(&s_keys, app_param, key_handle, challenge_param,
			                       s_sign_counter + 1U, sig_der, &sig_len))
			{
				sw = U2F_SW_WRONG_DATA; /* not this device's/appParam's key handle */
				break;
			}

			if (p1 == CTAP1_P1_CHECK_ONLY)
			{
				sw = U2F_SW_CONDITIONS_NOT_SATISFIED; /* handle is valid, nothing signed */
				break;
			}

			s_sign_counter++;
			(void)u2f_storage_save();

			resp_len = 0;
			s_resp_buf[resp_len++] = 0x01U; /* user presence: verified by menu entry */
			s_resp_buf[resp_len++] = (uint8_t)(s_sign_counter >> 24);
			s_resp_buf[resp_len++] = (uint8_t)(s_sign_counter >> 16);
			s_resp_buf[resp_len++] = (uint8_t)(s_sign_counter >> 8);
			s_resp_buf[resp_len++] = (uint8_t)(s_sign_counter);
			memcpy(&s_resp_buf[resp_len], sig_der, sig_len); resp_len = (uint16_t)(resp_len + sig_len);
			sw = U2F_SW_NO_ERROR;
			break;
		}

		default:
			sw = U2F_SW_INS_NOT_SUPPORTED;
			break;
	}

respond:
	s_resp_buf[resp_len++] = (uint8_t)(sw >> 8);
	s_resp_buf[resp_len++] = (uint8_t)(sw);
	u2f_send_message(cid, CTAPHID_CMD_MSG, s_resp_buf, resp_len);
}

/*============================================================================*/
/* Menu entry point                                                          */
/*============================================================================*/
void u2f_main_menu(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	bool running = true;
	bool need_redraw = true;
	uint32_t last_draw_tick = 0;

	if (!u2f_storage_load())
	{
		u2f_generate_master_secret();
		(void)u2f_storage_save();
	}

	u2f_core_init(u2f_rng_for_core);

	s_reasm.assembling = false;
	s_active_cid = 0;
	s_pkt_pending = false;
	USBD_U2F_SetRxCallback(u2f_rx_isr);

	m1_usb_switch_to_u2f();

	while (running)
	{
		if (need_redraw || (HAL_GetTick() - last_draw_tick) >= 500U)
		{
			need_redraw = false;
			last_draw_tick = HAL_GetTick();

			u8g2_FirstPage(&m1_u8g2);
			do {
				char buf[32];

				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
				u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
				u8g2_DrawStr(&m1_u8g2, 2, 11, "U2F Authenticator");

				u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
				snprintf(buf, sizeof(buf), "Sign count: %lu", (unsigned long)s_sign_counter);
				u8g2_DrawStr(&m1_u8g2, 2, 35, buf);

				u8g2_DrawStr(&m1_u8g2, 2, 47,
				             (m1_usb_get_current_mode() == M1_USB_MODE_HID) ?
				             "Waiting for host..." : "USB not ready");
				u8g2_DrawStr(&m1_u8g2, 2, 63, "Press BACK to exit");
			} while (u8g2_NextPage(&m1_u8g2));
		}

		if (s_pkt_pending)
		{
			uint8_t local[64];

			__disable_irq();
			memcpy(local, s_pkt_buf, sizeof(local));
			s_pkt_pending = false;
			__enable_irq();

			u2f_process_packet(local);
			need_redraw = true;
		}

		ret = xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(20));
		if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if (ret == pdTRUE && this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				running = false;
		}
	}

	USBD_U2F_SetRxCallback(NULL);
	m1_usb_switch_to_normal();

	memset(&s_keys, 0, sizeof(s_keys));

	xQueueReset(main_q_hdl);
	m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT);
}

#endif /* M1_APP_U2F_ENABLE */
