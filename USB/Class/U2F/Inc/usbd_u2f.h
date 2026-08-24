/**
  ******************************************************************************
  * @file    usbd_u2f.h
  * @brief   USB CTAPHID transport class header (U2F/CTAP1 authenticator)
  ******************************************************************************
  */

#ifndef __USBD_U2F_H
#define __USBD_U2F_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "usbd_ioreq.h"

/* CTAPHID transport: bidirectional 64-byte reports on one interrupt EP pair.
 * Standalone (non-composite) class, same pattern as usbd_hid.c: only ever
 * loaded exclusively via m1_usb_switch_to_u2f(), replacing CDC+MSC. */
#define U2F_EPIN_ADDR                   0x81U
#define U2F_EPOUT_ADDR                  0x01U
#define U2F_EP_SIZE                     64U

/* config(9) + interface(9) + HID(9) + EP IN(7) + EP OUT(7) */
#define USB_U2F_CONFIG_DESC_SIZ         41U
#define USB_U2F_DESC_SIZ                9U

#define U2F_DESCRIPTOR_TYPE             0x21U
#define U2F_REPORT_DESC                 0x22U

#define U2F_FS_BINTERVAL                5U

#define U2F_REPORT_DESC_SIZE            34U

typedef enum
{
  U2F_IDLE = 0,
  U2F_BUSY,
} U2F_StateTypeDef;

/* Callback invoked from the DataOut ISR path with one freshly-received
 * 64-byte CTAPHID packet. Kept short: the real work happens in the
 * u2f_main_menu() polling loop in m1_u2f.c, this just hands the packet off. */
typedef void (*USBD_U2F_RxCallback)(const uint8_t *data, uint16_t len);

typedef struct
{
  uint8_t  rx_buf[U2F_EP_SIZE];
  U2F_StateTypeDef state;
} USBD_U2F_HandleTypeDef;

extern USBD_ClassTypeDef USBD_U2F;

/* Register the callback fired for each received 64-byte OUT packet. */
void USBD_U2F_SetRxCallback(USBD_U2F_RxCallback cb);

/* Send a 64-byte CTAPHID packet. Returns USBD_OK if queued, USBD_FAIL if the
 * previous packet is still in flight (caller should retry). */
uint8_t USBD_U2F_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len);

/* True once the previous USBD_U2F_SendReport() has finished transmitting. */
bool USBD_U2F_IsIdle(void);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_U2F_H */
