#include "cdc.h"

#ifdef _USE_HW_CDC
#include "usb.h"
#include "uart.h"
#include "qbuffer.h"
#include "tinyusb_cdc_acm.h"


/* ESP-IDF 에서는 esp_tinyusb 가 TinyUSB 콜백(tud_cdc_rx_cb, tud_cdc_line_state_cb)과
   tud_task() 태스크를 갖고 있다. 그래서 수신은 esp_tinyusb 콜백에서 qbuffer 로 받고,
   송신은 esp_tinyusb 의 write_queue / flush 를 쓴다. */


#define CDC_RX_BUF_LENGTH       4096        // cmd 패킷(최대 1034 B)이 통째로 들어갈 만큼
#define CDC_TX_TIMEOUT_MS       100


static void cdcRxCallback(int itf, cdcacm_event_t *event);
static void cdcLineStateCallback(int itf, cdcacm_event_t *event);


static bool      is_init = false;
static qbuffer_t rx_q;
static uint8_t   rx_buf[CDC_RX_BUF_LENGTH];
static uint8_t   rx_usb_buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
static bool      prev_rts = false;



bool cdcInit(void)
{
  esp_err_t esp_ret;

  if (usbIsInit() != true)
    return false;

  qbufferCreate(&rx_q, rx_buf, CDC_RX_BUF_LENGTH);

  const tinyusb_config_cdcacm_t acm_cfg =
  {
    .cdc_port                     = TINYUSB_CDC_ACM_0,
    .callback_rx                  = cdcRxCallback,
    .callback_rx_wanted_char      = NULL,
    .callback_line_state_changed  = cdcLineStateCallback,
    .callback_line_coding_changed = NULL,
  };

  esp_ret = tinyusb_cdcacm_init(&acm_cfg);
  is_init = (esp_ret == ESP_OK);

  logPrintf("[%s] cdcInit()\n", is_init ? "OK":"E_");
  return is_init;
}

bool cdcIsInit(void)
{
  return is_init;
}

bool cdcIsConnect(void)
{
  if (is_init != true)
    return false;

  return tud_cdc_n_connected(0);
}

uint32_t cdcAvailable(void)
{
  if (is_init != true)
    return 0;

  return qbufferAvailable(&rx_q);
}

uint8_t cdcRead(void)
{
  uint8_t ret = 0;

  if (is_init != true)
    return 0;

  qbufferRead(&rx_q, &ret, 1);
  return ret;
}

uint32_t cdcWrite(uint8_t *p_data, uint32_t length)
{
  uint32_t pre_time;
  uint32_t sent_len = 0;

  if (cdcIsConnect() != true)
    return 0;

  pre_time = millis();
  while (sent_len < length)
  {
    uint32_t tx_len;

    tx_len = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, &p_data[sent_len], length - sent_len);
    if (tx_len > 0)
    {
      sent_len += tx_len;
    }
    else
    {
      // FIFO 가 찼다. USB 태스크가 내보낼 때까지 잠깐 기다린다
      tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(10));
    }

    if (cdcIsConnect() != true)
      break;

    if (millis() - pre_time >= CDC_TX_TIMEOUT_MS)
      break;
  }
  tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);

  return sent_len;
}

uint32_t cdcGetBaud(void)
{
  cdc_line_coding_t coding;

  if (is_init != true)
    return 0;

  tud_cdc_n_get_line_coding(0, &coding);
  return coding.bit_rate;
}

//-- 호스트가 연 보율로 CDC 스트림의 주인을 가른다.
//
//   115200 = 터미널(CLI), 그 외 = 호스트 툴(cmd 패킷).
//   둘이 같은 스트림에서 각자 읽으면 서로 바이트를 훔쳐 양쪽 다 깨진다.
//   판정은 cli 모듈과 cmd 모듈이 같이 쓴다.
//
uint8_t cdcGetType(void)
{
  if (cdcGetBaud() == 115200)
    return USB_CON_CLI;

  return USB_CON_CDC;
}

//-- CDC 를 uart 채널(HW_UART_CH_USB) 로 쓰기 위한 드라이버.
//   uartSetDriver() 로 꽂는다. CLI 와 로그가 uartWrite() 로 CDC 에 쓸 수 있게 된다.
//
static bool     cdcUartOpen(uint32_t baud)  { (void)baud; return true; }
static bool     cdcUartClose(void)          { return true; }
static uint32_t cdcUartAvailable(void)      { return cdcAvailable(); }
static bool     cdcUartFlush(void)          { while (cdcAvailable()) cdcRead(); return true; }
static uint8_t  cdcUartRead(void)           { return cdcRead(); }
static uint32_t cdcUartWrite(uint8_t *p_data, uint32_t length) { return cdcWrite(p_data, length); }

uart_driver_t cdc_uart_driver =
{
  .open      = cdcUartOpen,
  .close     = cdcUartClose,
  .available = cdcUartAvailable,
  .flush     = cdcUartFlush,
  .read      = cdcUartRead,
  .write     = cdcUartWrite,
};

void cdcRxCallback(int itf, cdcacm_event_t *event)
{
  size_t   rx_size = 0;
  uint32_t q_free;

  (void)event;

  if (tinyusb_cdcacm_read(itf, rx_usb_buf, sizeof(rx_usb_buf), &rx_size) != ESP_OK)
  {
    return;
  }

  // 큐가 차면 넘치는 만큼 버린다 (qbuffer 는 덮어쓰지 않는다)
  q_free = rx_q.len - qbufferAvailable(&rx_q) - 1;
  if (rx_size > q_free)
  {
    rx_size = q_free;
  }
  qbufferWrite(&rx_q, rx_usb_buf, rx_size);
}

//-- esptool 리셋 시퀀스로 ROM 다운로드 모드에 들어간다.
//
//   esptool 클래식 리셋 : (DTR=0, RTS=1) → (DTR=1, RTS=0)
//   RTS 가 1 → 0 으로 떨어질 때 DTR 이 1 이면 다운로드 모드로 재시작한다.
//   IDF 의 S2/S3 USB 콘솔(usb_console.c)과 같은 판정이다.
//
//   DTR 이 0 인 RTS 하강(포트 닫기 등)은 일반 리셋으로 보내지 않는다.
//   macOS 에서 포트를 닫을 때마다 보드가 재시작하는 것을 막는다.
//
void cdcLineStateCallback(int itf, cdcacm_event_t *event)
{
  bool dtr = event->line_state_changed_data.dtr;
  bool rts = event->line_state_changed_data.rts;

  (void)itf;

  if (prev_rts == true && rts == false && dtr == true)
  {
    usbJumpToBoot();
  }
  prev_rts = rts;
}

#endif
