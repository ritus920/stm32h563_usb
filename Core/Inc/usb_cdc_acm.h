#ifndef USB_CDC_ACM_H
#define USB_CDC_ACM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h5xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

void USB_CDC_ACM_Init(PCD_HandleTypeDef *hpcd);
bool USB_CDC_ACM_Configured(void);
HAL_StatusTypeDef USB_CDC_ACM_Transmit(const uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* USB_CDC_ACM_H */
