/* See COPYING.txt for license details. */
/*
 * rfal_rf.h  (HOST-TEST STUB)
 *
 * Minimal stand-in for the ST RFAL header so the real mfc_crypto1.c cipher
 * core can be compiled on the host. Only the symbols mfc_crypto1.c references
 * are provided. The RF transceive function is declared here and stubbed in the
 * test; the cipher-core known-answer test never calls it.
 */
#ifndef RFAL_RF_H_HOSTSTUB_
#define RFAL_RF_H_HOSTSTUB_

#include <stdint.h>

typedef int32_t ReturnCode;

#define RFAL_ERR_NONE                  ((ReturnCode)0)

#define RFAL_TXRX_FLAGS_DEFAULT        ((uint32_t)0x00u)
#define RFAL_TXRX_FLAGS_CRC_TX_MANUAL  ((uint32_t)0x01u)
#define RFAL_TXRX_FLAGS_CRC_RX_KEEP    ((uint32_t)0x02u)

static inline uint32_t rfalConvMsTo1fc(uint32_t ms) { return ms; }

ReturnCode rfalTransceiveBlockingTxRx(uint8_t *txBuf, uint16_t txLen,
                                      uint8_t *rxBuf, uint16_t rxBufLen,
                                      uint16_t *actLen, uint32_t flags,
                                      uint32_t fwt);

#endif /* RFAL_RF_H_HOSTSTUB_ */
