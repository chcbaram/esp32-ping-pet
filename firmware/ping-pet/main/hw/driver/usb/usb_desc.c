#include "usb.h"

#ifdef _USE_HW_USB
#include "esp_mac.h"


//   VID 0x1209 는 pid.codes (오픈소스 하드웨어용 공용 VID). PID 는 레지스트리와 내 저장소에서 비어 있는 것을 골랐다.
//   stm32h563-core (B563 ~ B566), weact-h750 (B750 ~ B752) 과 같이 칩 이름을 대역으로 쓴다 (E32 + P4).
//
//   **PID 는 모드가 아니라 USB 구성을 따른다.**
//
//     E324  CDC                 앱 (지금)
//     E325  CDC + HID           앱 + 웹 설정 채널 (예약, 다음 단계)
//     E326  (예약)
//     E327  (예약)
//
//   같은 VID/PID 로 구성만 바뀌면 Windows 가 캐시한 디스크립터로 잘못 붙는다. 구성이 바뀌면 PID 를 바꾼다.
//   ROM 다운로드 모드는 칩이 정한 303A:0012 다 (tools/flash.py).
#define USB_VID           0x1209
#define USB_PID           0xE324
#define USB_BCD           0x0200


//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
static const tusb_desc_device_t desc_device =
{
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = USB_BCD,

  // CDC 가 IAD 를 쓰므로 디바이스 클래스는 MISC/COMMON/IAD 여야 한다.
  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

  .idVendor           = USB_VID,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0100,

  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,

  .bNumConfigurations = 0x01
};

// HS 장치는 qualifier 가 있어야 한다 (ESP32-P4 는 USB 2.0 HS OTG 포트)
static const tusb_desc_device_qualifier_t desc_qualifier =
{
  .bLength            = sizeof(tusb_desc_device_qualifier_t),
  .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
  .bcdUSB             = USB_BCD,

  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,

  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
  .bNumConfigurations = 0x01,
  .bReserved          = 0x00
};


//--------------------------------------------------------------------+
// Configuration Descriptor
//
//   HID(웹 설정 채널)는 다음 단계에서 ITF_NUM_HID, EP 0x03 / 0x83 으로 붙인다.
//--------------------------------------------------------------------+
enum
{
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_fs_configuration[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

  // CDC : itf, str, notif EP, notif size, out EP, in EP, size
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 16, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

static const uint8_t desc_hs_configuration[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

  // HS 벌크 엔드포인트는 512 B
  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 16, EPNUM_CDC_OUT, EPNUM_CDC_IN, 512),
};


//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+
enum
{
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
  STRID_CDC,
  STRID_COUNT
};

//   시리얼은 MAC 을 ROM 다운로드 장치와 같은 형식("e8:f6:0a:e8:b4:1d")으로 쓴다.
//   앱과 ROM 다운로드 모드에서 시리얼이 같으면 macOS 포트 이름이 같게 나온다.
static char serial_str[18];

static const char *string_desc_arr[STRID_COUNT] =
{
  (const char[]){0x09, 0x04},     // 0: English (0x0409)
  "BARAM",                        // 1: Manufacturer
  _DEF_BOARD_NAME,                // 2: Product
  serial_str,                     // 3: Serial (MAC 으로 생성)
  _DEF_BOARD_NAME " CDC",         // 4
};


const char *usbGetSerial(void)
{
  return serial_str;
}

void usbDescConfig(tinyusb_config_t *p_cfg)
{
  uint8_t mac[6] = {0};

  esp_efuse_mac_get_default(mac);
  snprintf(serial_str, sizeof(serial_str), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  p_cfg->descriptor.device            = &desc_device;
  p_cfg->descriptor.qualifier         = &desc_qualifier;
  p_cfg->descriptor.string            = string_desc_arr;
  p_cfg->descriptor.string_count      = STRID_COUNT;
  p_cfg->descriptor.full_speed_config = desc_fs_configuration;
  p_cfg->descriptor.high_speed_config = desc_hs_configuration;
}

#endif
