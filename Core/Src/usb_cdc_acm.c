#include "usb_cdc_acm.h"

#include <string.h>

#define USB_MAX_EP0_SIZE                 64U
#define CDC_NOTIFICATION_EP              0x82U
#define CDC_DATA_IN_EP                   0x81U
#define CDC_DATA_OUT_EP                  0x01U
#define CDC_DATA_FS_MAX_PACKET_SIZE      64U
#define CDC_CMD_PACKET_SIZE              8U

#define USB_DESC_TYPE_DEVICE             0x01U
#define USB_DESC_TYPE_CONFIGURATION      0x02U
#define USB_DESC_TYPE_STRING             0x03U
#define USB_DESC_TYPE_INTERFACE          0x04U
#define USB_DESC_TYPE_ENDPOINT           0x05U
#define USB_DESC_TYPE_DEVICE_QUALIFIER   0x06U
#define USB_DESC_TYPE_IAD                0x0BU
#define USB_DESC_TYPE_CS_INTERFACE       0x24U
#define USB_DESC_TYPE_CS_ENDPOINT        0x25U

#define USB_REQ_GET_STATUS               0x00U
#define USB_REQ_CLEAR_FEATURE            0x01U
#define USB_REQ_SET_FEATURE              0x03U
#define USB_REQ_SET_ADDRESS              0x05U
#define USB_REQ_GET_DESCRIPTOR           0x06U
#define USB_REQ_SET_DESCRIPTOR           0x07U
#define USB_REQ_GET_CONFIGURATION        0x08U
#define USB_REQ_SET_CONFIGURATION        0x09U
#define USB_REQ_GET_INTERFACE            0x0AU
#define USB_REQ_SET_INTERFACE            0x0BU

#define CDC_REQ_SET_LINE_CODING          0x20U
#define CDC_REQ_GET_LINE_CODING          0x21U
#define CDC_REQ_SET_CONTROL_LINE_STATE   0x22U
#define CDC_REQ_SEND_BREAK               0x23U

#define CDC_COMM_INTERFACE               0U
#define CDC_DATA_INTERFACE               1U

#define USB_REQ_TYPE_STANDARD            0x00U
#define USB_REQ_TYPE_CLASS               0x20U
#define USB_REQ_TYPE_MASK                0x60U
#define USB_REQ_RECIPIENT_DEVICE         0x00U
#define USB_REQ_RECIPIENT_INTERFACE      0x01U
#define USB_REQ_RECIPIENT_ENDPOINT       0x02U
#define USB_REQ_RECIPIENT_MASK           0x1FU
#define USB_REQ_DIR_MASK                 0x80U
#define USB_REQ_DIR_DEVICE_TO_HOST       0x80U
#define USB_REQ_DIR_HOST_TO_DEVICE       0x00U

#define USB_LANGID_ENGLISH_US            0x0409U

typedef struct __attribute__((packed))
{
  uint8_t  bmRequest;
  uint8_t  bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
} USB_SetupRequest;

typedef struct __attribute__((packed))
{
  uint32_t dwDTERate;
  uint8_t  bCharFormat;
  uint8_t  bParityType;
  uint8_t  bDataBits;
} CDC_LineCoding;

typedef enum
{
  CTRL_STATE_IDLE = 0,
  CTRL_STATE_EXPECT_LINE_CODING
} CtrlState;

typedef struct
{
  PCD_HandleTypeDef *pcd;
  uint8_t            configuration_value;
  uint8_t            pending_address;
  bool               configured;
  bool               tx_busy;
  bool               notify_busy;
  CtrlState          ctrl_state;
  uint8_t            ctrl_buffer[16];
  CDC_LineCoding     line_coding;
  uint8_t            rx_buffer[CDC_DATA_FS_MAX_PACKET_SIZE];
} CDC_Context;

static CDC_Context cdc_ctx;
static USB_SetupRequest current_setup;

static const uint8_t device_descriptor[] =
{
  0x12, USB_DESC_TYPE_DEVICE,
  0x00, 0x02,
  0x02, 0x00, 0x00,
  USB_MAX_EP0_SIZE,
  0x83, 0x04,
  0x40, 0x57,
  0x00, 0x01,
  0x01, 0x02, 0x03,
  0x01
};

static const uint8_t configuration_descriptor[] =
{
  0x09, USB_DESC_TYPE_CONFIGURATION,
  75, 0x00,
  0x02,
  0x01,
  0x00,
  0x80,
  50,

  0x08, USB_DESC_TYPE_IAD,
  CDC_COMM_INTERFACE,
  0x02,
  0x02,
  0x02,
  0x01,
  0x00,

  0x09, USB_DESC_TYPE_INTERFACE,
  CDC_COMM_INTERFACE,
  0x00,
  0x01,
  0x02,
  0x02,
  0x01,
  0x00,

  0x05, USB_DESC_TYPE_CS_INTERFACE,
  0x00,
  0x10, 0x01,

  0x05, USB_DESC_TYPE_CS_INTERFACE,
  0x01,
  0x01,
  CDC_DATA_INTERFACE,

  0x04, USB_DESC_TYPE_CS_INTERFACE,
  0x02,
  0x02,

  0x05, USB_DESC_TYPE_CS_INTERFACE,
  0x06,
  CDC_COMM_INTERFACE,
  CDC_DATA_INTERFACE,

  0x07, USB_DESC_TYPE_ENDPOINT,
  CDC_NOTIFICATION_EP,
  0x03,
  CDC_CMD_PACKET_SIZE, 0x00,
  0x10,

  0x09, USB_DESC_TYPE_INTERFACE,
  CDC_DATA_INTERFACE,
  0x00,
  0x02,
  0x0A,
  0x00,
  0x00,
  0x00,

  0x07, USB_DESC_TYPE_ENDPOINT,
  CDC_DATA_OUT_EP,
  0x02,
  CDC_DATA_FS_MAX_PACKET_SIZE, 0x00,
  0x00,

  0x07, USB_DESC_TYPE_ENDPOINT,
  CDC_DATA_IN_EP,
  0x02,
  CDC_DATA_FS_MAX_PACKET_SIZE, 0x00,
  0x00
};

static const uint8_t device_qualifier_descriptor[] =
{
  0x0A, USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00, 0x02,
  0x02,
  0x00,
  0x00,
  USB_MAX_EP0_SIZE,
  0x01,
  0x00
};

static const uint8_t string_langid[] =
{
  0x04, USB_DESC_TYPE_STRING,
  (uint8_t)(USB_LANGID_ENGLISH_US & 0xFFU),
  (uint8_t)(USB_LANGID_ENGLISH_US >> 8)
};

static const uint8_t string_manufacturer[] =
{
  0x0E, USB_DESC_TYPE_STRING,
  'O', 0x00,
  'p', 0x00,
  'e', 0x00,
  'n', 0x00,
  'A', 0x00,
  'I', 0x00
};

static const uint8_t string_product[] =
{
  0x1A, USB_DESC_TYPE_STRING,
  'S', 0x00,
  'T', 0x00,
  'M', 0x00,
  '3', 0x00,
  '2', 0x00,
  ' ', 0x00,
  'C', 0x00,
  'D', 0x00,
  'C', 0x00,
  ' ', 0x00,
  'A', 0x00,
  'C', 0x00,
  'M', 0x00
};

static const uint8_t string_serial[] =
{
  0x0A, USB_DESC_TYPE_STRING,
  '0', 0x00,
  '0', 0x00,
  '0', 0x00,
  '1', 0x00
};

static const uint8_t *get_string_descriptor(uint8_t index, uint16_t *len)
{
  switch (index)
  {
    case 0:
      *len = sizeof(string_langid);
      return string_langid;
    case 1:
      *len = sizeof(string_manufacturer);
      return string_manufacturer;
    case 2:
      *len = sizeof(string_product);
      return string_product;
    case 3:
      *len = sizeof(string_serial);
      return string_serial;
    default:
      *len = 0;
      return NULL;
  }
}

static void usb_ep0_send(const uint8_t *buffer, uint16_t len)
{
  if (len > current_setup.wLength)
  {
    len = current_setup.wLength;
  }
  HAL_PCD_EP_Transmit(cdc_ctx.pcd, 0x80, (uint8_t *)buffer, len);
}

static void usb_ep0_send_zlp(void)
{
  HAL_PCD_EP_Transmit(cdc_ctx.pcd, 0x80, NULL, 0U);
}

static void usb_ep0_stall(void)
{
  HAL_PCD_EP_SetStall(cdc_ctx.pcd, 0x80);
  HAL_PCD_EP_SetStall(cdc_ctx.pcd, 0x00);
}

static void usb_open_cdc_endpoints(void)
{
  HAL_PCD_EP_Open(cdc_ctx.pcd, CDC_DATA_IN_EP, USB_EP_TYPE_BULK, CDC_DATA_FS_MAX_PACKET_SIZE);
  HAL_PCD_EP_Open(cdc_ctx.pcd, CDC_DATA_OUT_EP, USB_EP_TYPE_BULK, CDC_DATA_FS_MAX_PACKET_SIZE);
  HAL_PCD_EP_Open(cdc_ctx.pcd, CDC_NOTIFICATION_EP, USB_EP_TYPE_INTR, CDC_CMD_PACKET_SIZE);
  HAL_PCD_EP_Receive(cdc_ctx.pcd, CDC_DATA_OUT_EP, cdc_ctx.rx_buffer, sizeof(cdc_ctx.rx_buffer));
}

static void usb_close_cdc_endpoints(void)
{
  HAL_PCD_EP_Close(cdc_ctx.pcd, CDC_DATA_IN_EP);
  HAL_PCD_EP_Close(cdc_ctx.pcd, CDC_DATA_OUT_EP);
  HAL_PCD_EP_Close(cdc_ctx.pcd, CDC_NOTIFICATION_EP);
}

static void handle_get_descriptor(void)
{
  uint8_t descriptor_type = (uint8_t)(current_setup.wValue >> 8);
  uint8_t descriptor_index = (uint8_t)(current_setup.wValue & 0xFFU);
  uint16_t len = 0;

  switch (descriptor_type)
  {
    case USB_DESC_TYPE_DEVICE:
      usb_ep0_send(device_descriptor, sizeof(device_descriptor));
      break;
    case USB_DESC_TYPE_CONFIGURATION:
      len = sizeof(configuration_descriptor);
      if (current_setup.wLength < len)
      {
        len = current_setup.wLength;
      }
      HAL_PCD_EP_Transmit(cdc_ctx.pcd, 0x80, (uint8_t *)configuration_descriptor, len);
      break;
    case USB_DESC_TYPE_DEVICE_QUALIFIER:
      usb_ep0_send(device_qualifier_descriptor, sizeof(device_qualifier_descriptor));
      break;
    case USB_DESC_TYPE_STRING:
    {
      const uint8_t *str = get_string_descriptor(descriptor_index, &len);
      if ((str != NULL) && (len > 0U))
      {
        usb_ep0_send(str, len);
      }
      else
      {
        usb_ep0_stall();
      }
      break;
    }
    default:
      usb_ep0_stall();
      break;
  }
}

static void handle_standard_device_request(void)
{
  switch (current_setup.bRequest)
  {
    case USB_REQ_GET_DESCRIPTOR:
      handle_get_descriptor();
      break;

    case USB_REQ_SET_ADDRESS:
      cdc_ctx.pending_address = (uint8_t)(current_setup.wValue & 0x7FU);
      usb_ep0_send_zlp();
      break;

    case USB_REQ_SET_CONFIGURATION:
      cdc_ctx.configuration_value = (uint8_t)(current_setup.wValue & 0xFFU);
      if (cdc_ctx.configuration_value == 1U)
      {
        cdc_ctx.configured = true;
        usb_open_cdc_endpoints();
      }
      else
      {
        cdc_ctx.configured = false;
        usb_close_cdc_endpoints();
      }
      usb_ep0_send_zlp();
      break;

    case USB_REQ_GET_CONFIGURATION:
      usb_ep0_send(&cdc_ctx.configuration_value, 1U);
      break;

    case USB_REQ_GET_STATUS:
    {
      uint16_t status = 0x0000U;
      usb_ep0_send((uint8_t *)&status, 2U);
      break;
    }

    case USB_REQ_SET_FEATURE:
    case USB_REQ_CLEAR_FEATURE:
      usb_ep0_send_zlp();
      break;

    default:
      usb_ep0_stall();
      break;
  }
}

static void handle_standard_interface_request(void)
{
  switch (current_setup.bRequest)
  {
    case USB_REQ_GET_INTERFACE:
    {
      uint8_t alt_setting = 0U;
      usb_ep0_send(&alt_setting, 1U);
      break;
    }
    case USB_REQ_SET_INTERFACE:
      usb_ep0_send_zlp();
      break;
    default:
      usb_ep0_stall();
      break;
  }
}

static void handle_standard_request(void)
{
  uint8_t recipient = current_setup.bmRequest & USB_REQ_RECIPIENT_MASK;

  switch (recipient)
  {
    case USB_REQ_RECIPIENT_DEVICE:
      handle_standard_device_request();
      break;
    case USB_REQ_RECIPIENT_INTERFACE:
      handle_standard_interface_request();
      break;
    default:
      usb_ep0_stall();
      break;
  }
}

static void handle_cdc_class_request(void)
{
  switch (current_setup.bRequest)
  {
    case CDC_REQ_SET_LINE_CODING:
      if (current_setup.wLength == sizeof(CDC_LineCoding))
      {
        cdc_ctx.ctrl_state = CTRL_STATE_EXPECT_LINE_CODING;
        HAL_PCD_EP_Receive(cdc_ctx.pcd, 0x00, cdc_ctx.ctrl_buffer, sizeof(CDC_LineCoding));
      }
      else
      {
        usb_ep0_stall();
      }
      break;

    case CDC_REQ_GET_LINE_CODING:
      usb_ep0_send((uint8_t *)&cdc_ctx.line_coding, sizeof(CDC_LineCoding));
      break;

    case CDC_REQ_SET_CONTROL_LINE_STATE:
      usb_ep0_send_zlp();
      break;

    case CDC_REQ_SEND_BREAK:
      usb_ep0_send_zlp();
      break;

    default:
      usb_ep0_stall();
      break;
  }
}

static void handle_setup_packet(void)
{
  uint8_t request_type = current_setup.bmRequest & USB_REQ_TYPE_MASK;

  if (request_type == USB_REQ_TYPE_STANDARD)
  {
    handle_standard_request();
  }
  else if (request_type == USB_REQ_TYPE_CLASS)
  {
    handle_cdc_class_request();
  }
  else
  {
    usb_ep0_stall();
  }
}

void USB_CDC_ACM_Init(PCD_HandleTypeDef *hpcd)
{
  memset(&cdc_ctx, 0, sizeof(cdc_ctx));
  cdc_ctx.pcd = hpcd;
  cdc_ctx.line_coding.dwDTERate = 9600U;
  cdc_ctx.line_coding.bCharFormat = 0U;
  cdc_ctx.line_coding.bParityType = 0U;
  cdc_ctx.line_coding.bDataBits = 8U;

  HAL_PCDEx_SetRxFiFo(hpcd, 0x80);
  HAL_PCDEx_SetTxFiFo(hpcd, 0, 0x40);
  HAL_PCDEx_SetTxFiFo(hpcd, CDC_NOTIFICATION_EP & 0x7FU, 0x10);
  HAL_PCDEx_SetTxFiFo(hpcd, CDC_DATA_IN_EP & 0x7FU, 0x40);

  HAL_PCD_Start(hpcd);
}

bool USB_CDC_ACM_Configured(void)
{
  return cdc_ctx.configured;
}

HAL_StatusTypeDef USB_CDC_ACM_Transmit(const uint8_t *data, uint16_t length)
{
  if ((!cdc_ctx.configured) || (cdc_ctx.tx_busy) || (length == 0U))
  {
    return HAL_BUSY;
  }

  cdc_ctx.tx_busy = true;
  HAL_StatusTypeDef status = HAL_PCD_EP_Transmit(cdc_ctx.pcd, CDC_DATA_IN_EP, (uint8_t *)data, length);
  if (status != HAL_OK)
  {
    cdc_ctx.tx_busy = false;
  }
  return status;
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
  if (hpcd != cdc_ctx.pcd)
  {
    return;
  }

  cdc_ctx.configured = false;
  cdc_ctx.configuration_value = 0U;
  cdc_ctx.pending_address = 0U;

  HAL_PCD_EP_Open(hpcd, 0x00, USB_MAX_EP0_SIZE, USB_EP_TYPE_CTRL);
  HAL_PCD_EP_Open(hpcd, 0x80, USB_MAX_EP0_SIZE, USB_EP_TYPE_CTRL);
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
  if (hpcd != cdc_ctx.pcd)
  {
    return;
  }

  uint8_t *raw = (uint8_t *)hpcd->Setup;
  current_setup.bmRequest = raw[0];
  current_setup.bRequest = raw[1];
  current_setup.wValue = (uint16_t)raw[2] | ((uint16_t)raw[3] << 8);
  current_setup.wIndex = (uint16_t)raw[4] | ((uint16_t)raw[5] << 8);
  current_setup.wLength = (uint16_t)raw[6] | ((uint16_t)raw[7] << 8);

  cdc_ctx.ctrl_state = CTRL_STATE_IDLE;
  handle_setup_packet();
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  if (hpcd != cdc_ctx.pcd)
  {
    return;
  }

  if (epnum == 0U)
  {
    if (cdc_ctx.pending_address != 0U)
    {
      HAL_PCD_SetAddress(cdc_ctx.pcd, cdc_ctx.pending_address);
      cdc_ctx.pending_address = 0U;
    }
  }
  else if (epnum == (CDC_DATA_IN_EP & 0x7FU))
  {
    cdc_ctx.tx_busy = false;
  }
  else if (epnum == (CDC_NOTIFICATION_EP & 0x7FU))
  {
    cdc_ctx.notify_busy = false;
  }
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  if (hpcd != cdc_ctx.pcd)
  {
    return;
  }

  if (epnum == 0U)
  {
    if (cdc_ctx.ctrl_state == CTRL_STATE_EXPECT_LINE_CODING)
    {
      memcpy(&cdc_ctx.line_coding, cdc_ctx.ctrl_buffer, sizeof(CDC_LineCoding));
      if (cdc_ctx.line_coding.dwDTERate == 0U)
      {
        cdc_ctx.line_coding.dwDTERate = 9600U;
      }
      usb_ep0_send_zlp();
      cdc_ctx.ctrl_state = CTRL_STATE_IDLE;
    }
  }
  else if (epnum == (CDC_DATA_OUT_EP & 0x7FU))
  {
    uint32_t count = HAL_PCD_EP_GetRxCount(hpcd, CDC_DATA_OUT_EP);
    (void)count;
    HAL_PCD_EP_Receive(hpcd, CDC_DATA_OUT_EP, cdc_ctx.rx_buffer, sizeof(cdc_ctx.rx_buffer));
  }
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
  (void)hpcd;
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
  if (hpcd != cdc_ctx.pcd)
  {
    return;
  }
  cdc_ctx.configured = false;
}
