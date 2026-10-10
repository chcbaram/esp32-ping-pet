#ifndef USB_H_
#define USB_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_USB

#include "tinyusb.h"


typedef enum UsbMode
{
  USB_NON_MODE,
  USB_CDC_MODE,
  USB_MSC_MODE,
} UsbMode_t;

typedef enum UsbType
{
  USB_CON_CDC = 0,
  USB_CON_CLI = 1,
} UsbType_t;


bool      usbInit(void);
void      usbDeInit(void);
bool      usbIsInit(void);
bool      usbUpdate(void);
bool      usbIsOpen(void);
bool      usbIsConnect(void);
void      usbConnect(void);
void      usbDisconnect(void);

UsbMode_t usbGetMode(void);
UsbType_t usbGetType(void);

const char *usbGetSerial(void);

// ROM 다운로드 모드로 재시작한다 (BOOT 버튼을 누르고 리셋한 것과 같다). 돌아오지 않는다.
void      usbJumpToBoot(void);

// usb_desc.c : 디스크립터를 esp_tinyusb 설정에 채운다
void      usbDescConfig(tinyusb_config_t *p_cfg);

#endif

#ifdef __cplusplus
}
#endif

#endif
