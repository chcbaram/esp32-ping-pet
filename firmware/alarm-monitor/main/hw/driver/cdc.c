#include "cdc.h"




#ifdef _USE_HW_CDC
#include "qbuffer.h"
#include "cli.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"


#define CDC_RX_BUF_LEN        4096
#define CDC_TX_TIMEOUT_MS     100


static void cdcRxCallback(int itf, cdcacm_event_t *event);
static void cdcLineStateCallback(int itf, cdcacm_event_t *event);
#ifdef _USE_HW_CLI
static void cliCdc(cli_args_t *args);
#endif

static bool              is_init = false;
static volatile bool     is_connected = false;
static qbuffer_t         rx_q;
static uint8_t           rx_buf[CDC_RX_BUF_LEN];
static uint8_t           rx_usb_buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
static SemaphoreHandle_t tx_mutex;
static uint32_t          rx_cnt = 0;
static uint32_t          tx_cnt = 0;
static uint32_t          rx_drop_cnt = 0;




bool cdcInit(void)
{
  esp_err_t esp_ret;


  qbufferCreate(&rx_q, rx_buf, CDC_RX_BUF_LEN);
  tx_mutex = xSemaphoreCreateMutex();

  // ESP32-P4: USB 2.0 HS OTG 포트
  const tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

  esp_ret = tinyusb_driver_install(&tusb_cfg);
  if (esp_ret != ESP_OK)
  {
    logPrintf("[E_] cdcInit() tinyusb_driver_install : %s\n", esp_err_to_name(esp_ret));
    return false;
  }

  const tinyusb_config_cdcacm_t acm_cfg = {
    .cdc_port                     = TINYUSB_CDC_ACM_0,
    .callback_rx                  = cdcRxCallback,
    .callback_rx_wanted_char      = NULL,
    .callback_line_state_changed  = cdcLineStateCallback,
    .callback_line_coding_changed = NULL,
  };

  esp_ret = tinyusb_cdcacm_init(&acm_cfg);
  if (esp_ret != ESP_OK)
  {
    logPrintf("[E_] cdcInit() tinyusb_cdcacm_init : %s\n", esp_err_to_name(esp_ret));
    return false;
  }

  is_init = true;

#ifdef _USE_HW_CLI
  cliAdd("cdc", cliCdc);
#endif

  return true;
}

bool cdcIsInit(void)
{
  return is_init;
}

bool cdcIsConnect(void)
{
  return is_connected;
}

uint32_t cdcAvailable(void)
{
  return qbufferAvailable(&rx_q);
}

uint8_t cdcRead(void)
{
  uint8_t ret = 0;

  qbufferRead(&rx_q, &ret, 1);

  return ret;
}

void cdcDataIn(uint8_t rx_data)
{
  qbufferWrite(&rx_q, &rx_data, 1);
}

uint32_t cdcWrite(uint8_t *p_data, uint32_t length)
{
  uint32_t sent = 0;
  uint32_t pre_time;


  // 호스트가 포트를 열지 않았으면 버린다 (막히지 않게)
  if (is_init != true || is_connected != true)
  {
    return 0;
  }

  if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(CDC_TX_TIMEOUT_MS)) != pdTRUE)
  {
    return 0;
  }

  pre_time = millis();
  while (sent < length)
  {
    sent += tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, &p_data[sent], length - sent);
    if (sent < length)
    {
      tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(10));
    }
    if (millis() - pre_time >= CDC_TX_TIMEOUT_MS)
    {
      break;
    }
  }
  tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
  tx_cnt += sent;

  xSemaphoreGive(tx_mutex);

  return sent;
}

uint32_t cdcGetBaud(void)
{
  return 115200;
}

void cdcRxCallback(int itf, cdcacm_event_t *event)
{
  size_t   rx_size = 0;
  uint32_t q_free;


  if (tinyusb_cdcacm_read(itf, rx_usb_buf, sizeof(rx_usb_buf), &rx_size) != ESP_OK)
  {
    return;
  }

  q_free = rx_q.len - qbufferAvailable(&rx_q) - 1;
  if (rx_size > q_free)
  {
    rx_drop_cnt += rx_size - q_free;
    rx_size = q_free;
  }
  qbufferWrite(&rx_q, rx_usb_buf, rx_size);
  rx_cnt += rx_size;
}

void cdcLineStateCallback(int itf, cdcacm_event_t *event)
{
  is_connected = event->line_state_changed_data.dtr;
}

#ifdef _USE_HW_CLI
void cliCdc(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_init      : %s\n", is_init ? "True" : "False");
    cliPrintf("is_connected : %s\n", is_connected ? "True" : "False");
    cliPrintf("rx_cnt       : %lu\n", rx_cnt);
    cliPrintf("tx_cnt       : %lu\n", tx_cnt);
    cliPrintf("rx_drop_cnt  : %lu\n", rx_drop_cnt);
    cliPrintf("rx_q avail   : %lu\n", qbufferAvailable(&rx_q));
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("cdc info\n");
  }
}
#endif

#endif
