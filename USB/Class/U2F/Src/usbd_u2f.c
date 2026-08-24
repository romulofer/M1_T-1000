/**
  ******************************************************************************
  * @file    usbd_u2f.c
  * @brief   USB CTAPHID transport class implementation (U2F/CTAP1)
  *
  * Standalone (non-composite) class, structurally a clone of usbd_hid.c with
  * the two things a CTAPHID authenticator needs that a boot keyboard doesn't:
  * an OUT endpoint and 64-byte (not 8-byte) reports. Loaded exclusively via
  * m1_usb_switch_to_u2f(), same disconnect/patch-descriptor/reconnect flow
  * BadUSB already uses for its HID keyboard mode.
  ******************************************************************************
  */

#include "usbd_u2f.h"
#include "usbd_ctlreq.h"
#include "usbd_conf.h"
#include <string.h>

static uint8_t USBD_U2F_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_U2F_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_U2F_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t USBD_U2F_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_U2F_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t *USBD_U2F_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_U2F_GetDeviceQualifierDesc(uint16_t *length);

USBD_ClassTypeDef USBD_U2F =
{
  USBD_U2F_Init,
  USBD_U2F_DeInit,
  USBD_U2F_Setup,
  NULL,                 /* EP0_TxSent */
  NULL,                 /* EP0_RxReady */
  USBD_U2F_DataIn,
  USBD_U2F_DataOut,
  NULL,                 /* SOF */
  NULL,                 /* IsoINIncomplete */
  NULL,                 /* IsoOUTIncomplete */
  NULL,                 /* GetHSConfigDescriptor (not HS) */
  USBD_U2F_GetFSCfgDesc,
  NULL,                 /* GetOtherSpeedConfigDescriptor */
  USBD_U2F_GetDeviceQualifierDesc,
#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
  NULL,                 /* GetUsrStrDescriptor */
#endif
};

/* Standard CTAPHID report descriptor (FIDO Alliance usage page 0xF1D0),
 * fixed by the CTAP spec, not something to redesign. */
__ALIGN_BEGIN static const uint8_t U2F_ReportDesc[U2F_REPORT_DESC_SIZE] __ALIGN_END =
{
  0x06, 0xD0, 0xF1, /* Usage Page (FIDO Alliance = 0xF1D0) */
  0x09, 0x01,       /* Usage (U2F HID Authenticator Device) */
  0xA1, 0x01,       /* Collection (Application) */
  0x09, 0x20,       /*   Usage (Input Report Data) */
  0x15, 0x00,       /*   Logical Minimum (0) */
  0x26, 0xFF, 0x00, /*   Logical Maximum (255) */
  0x75, 0x08,       /*   Report Size (8) */
  0x95, 0x40,       /*   Report Count (64) */
  0x81, 0x02,       /*   Input (Data, Variable, Absolute) */
  0x09, 0x21,       /*   Usage (Output Report Data) */
  0x15, 0x00,       /*   Logical Minimum (0) */
  0x26, 0xFF, 0x00, /*   Logical Maximum (255) */
  0x75, 0x08,       /*   Report Size (8) */
  0x95, 0x40,       /*   Report Count (64) */
  0x91, 0x02,       /*   Output (Data, Variable, Absolute) */
  0xC0              /* End Collection */
};

__ALIGN_BEGIN static uint8_t USBD_U2F_CfgFSDesc[USB_U2F_CONFIG_DESC_SIZ] __ALIGN_END =
{
  /* Configuration Descriptor */
  0x09, USB_DESC_TYPE_CONFIGURATION, USB_U2F_CONFIG_DESC_SIZ, 0x00,
  0x01,                              /* bNumInterfaces */
  0x01,                              /* bConfigurationValue */
  0x00,                              /* iConfiguration */
  0x80 | (USBD_SELF_POWERED << 6),  /* bmAttributes */
  USBD_MAX_POWER,                   /* bMaxPower */

  /* Interface Descriptor */
  0x09, USB_DESC_TYPE_INTERFACE,
  0x00,                              /* bInterfaceNumber */
  0x00,                              /* bAlternateSetting */
  0x02,                              /* bNumEndpoints */
  0x03,                              /* bInterfaceClass: HID */
  0x00,                              /* bInterfaceSubClass: none (not boot) */
  0x00,                              /* bInterfaceProtocol: none */
  0x00,                              /* iInterface */

  /* HID Descriptor */
  0x09, U2F_DESCRIPTOR_TYPE,
  0x11, 0x01,                        /* bcdHID: 1.11 */
  0x00,                              /* bCountryCode */
  0x01,                              /* bNumDescriptors */
  U2F_REPORT_DESC,
  U2F_REPORT_DESC_SIZE, 0x00,        /* wDescriptorLength */

  /* Endpoint Descriptor (IN) */
  0x07, USB_DESC_TYPE_ENDPOINT,
  U2F_EPIN_ADDR,
  0x03,                              /* bmAttributes: Interrupt */
  U2F_EP_SIZE, 0x00,
  U2F_FS_BINTERVAL,

  /* Endpoint Descriptor (OUT) */
  0x07, USB_DESC_TYPE_ENDPOINT,
  U2F_EPOUT_ADDR,
  0x03,                              /* bmAttributes: Interrupt */
  U2F_EP_SIZE, 0x00,
  U2F_FS_BINTERVAL,
};

__ALIGN_BEGIN static uint8_t USBD_U2F_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00, 0x02,
  0x00, 0x00, 0x00,
  USB_MAX_EP0_SIZE,
  0x01,
  0x00,
};

static USBD_U2F_HandleTypeDef u2f_handle;
static USBD_U2F_RxCallback s_rx_callback = NULL;

void USBD_U2F_SetRxCallback(USBD_U2F_RxCallback cb)
{
  s_rx_callback = cb;
}

static uint8_t USBD_U2F_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  (void)cfgidx;

  (void)USBD_LL_OpenEP(pdev, U2F_EPIN_ADDR, USBD_EP_TYPE_INTR, U2F_EP_SIZE);
  pdev->ep_in[U2F_EPIN_ADDR & 0x7FU].is_used = 1U;
  pdev->ep_in[U2F_EPIN_ADDR & 0x7FU].bInterval = U2F_FS_BINTERVAL;

  (void)USBD_LL_OpenEP(pdev, U2F_EPOUT_ADDR, USBD_EP_TYPE_INTR, U2F_EP_SIZE);
  pdev->ep_out[U2F_EPOUT_ADDR & 0x7FU].is_used = 1U;
  pdev->ep_out[U2F_EPOUT_ADDR & 0x7FU].bInterval = U2F_FS_BINTERVAL;

  pdev->pClassData = &u2f_handle;
  u2f_handle.state = U2F_IDLE;

  (void)USBD_LL_PrepareReceive(pdev, U2F_EPOUT_ADDR, u2f_handle.rx_buf, U2F_EP_SIZE);

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_U2F_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  (void)cfgidx;

  (void)USBD_LL_CloseEP(pdev, U2F_EPIN_ADDR);
  pdev->ep_in[U2F_EPIN_ADDR & 0x7FU].is_used = 0U;

  (void)USBD_LL_CloseEP(pdev, U2F_EPOUT_ADDR);
  pdev->ep_out[U2F_EPOUT_ADDR & 0x7FU].is_used = 0U;

  pdev->pClassData = NULL;

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_U2F_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_StatusTypeDef ret = USBD_OK;
  uint16_t len;
  uint8_t *pbuf;

  if (pdev->pClassData == NULL)
    return (uint8_t)USBD_FAIL;

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
      /* No SET_REPORT/GET_REPORT support needed: all traffic goes over the
       * interrupt IN/OUT endpoints per the CTAPHID transport. */
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_DESCRIPTOR:
          if ((req->wValue >> 8) == U2F_REPORT_DESC)
          {
            pbuf = (uint8_t *)U2F_ReportDesc;
            len = MIN(U2F_REPORT_DESC_SIZE, req->wLength);
            (void)USBD_CtlSendData(pdev, pbuf, len);
          }
          else if ((req->wValue >> 8) == U2F_DESCRIPTOR_TYPE)
          {
            pbuf = USBD_U2F_CfgFSDesc + 18U;
            len = MIN(USB_U2F_DESC_SIZ, req->wLength);
            (void)USBD_CtlSendData(pdev, pbuf, len);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_INTERFACE:
        case USB_REQ_SET_INTERFACE:
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return (uint8_t)ret;
}

uint8_t USBD_U2F_SendReport(USBD_HandleTypeDef *pdev, uint8_t *report, uint16_t len)
{
  USBD_U2F_HandleTypeDef *hu2f = (USBD_U2F_HandleTypeDef *)pdev->pClassData;

  if (hu2f == NULL)
    return (uint8_t)USBD_FAIL;

  if (pdev->dev_state != USBD_STATE_CONFIGURED)
    return (uint8_t)USBD_FAIL;

  if (hu2f->state == U2F_IDLE)
  {
    hu2f->state = U2F_BUSY;
    (void)USBD_LL_Transmit(pdev, U2F_EPIN_ADDR, report, len);
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_U2F_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  (void)epnum;
  USBD_U2F_HandleTypeDef *hu2f = (USBD_U2F_HandleTypeDef *)pdev->pClassData;

  if (hu2f != NULL)
    hu2f->state = U2F_IDLE;

  return (uint8_t)USBD_OK;
}

bool USBD_U2F_IsIdle(void)
{
  return u2f_handle.state == U2F_IDLE;
}

static uint8_t USBD_U2F_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  uint32_t rx_len = USBD_LL_GetRxDataSize(pdev, epnum);

  if (s_rx_callback != NULL && rx_len == U2F_EP_SIZE)
    s_rx_callback(u2f_handle.rx_buf, (uint16_t)rx_len);

  (void)USBD_LL_PrepareReceive(pdev, U2F_EPOUT_ADDR, u2f_handle.rx_buf, U2F_EP_SIZE);

  return (uint8_t)USBD_OK;
}

static uint8_t *USBD_U2F_GetFSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_U2F_CfgFSDesc);
  return USBD_U2F_CfgFSDesc;
}

static uint8_t *USBD_U2F_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_U2F_DeviceQualifierDesc);
  return USBD_U2F_DeviceQualifierDesc;
}
