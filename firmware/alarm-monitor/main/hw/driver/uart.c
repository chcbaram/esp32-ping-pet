#include "uart.h"
#include "qbuffer.h"
#include "cli.h"

#ifdef _USE_HW_UART
#include "driver/uart.h"


#define UART_RX_BUF_LENGTH        4096        // cmd 패킷(최대 1034 B)이 통째로 들어갈 만큼



typedef struct
{
  bool is_open;
  uint32_t baud;

  uint32_t rx_cnt;
  uint32_t tx_cnt;

  uart_driver_t *p_driver;   /* NULL 이면 HW UART, 아니면 커스텀 드라이버(USB CDC 등) */
} uart_tbl_t;

typedef struct
{
  const char  *p_msg;
  uart_port_t  port;         /* UART_NUM_MAX 이면 HW UART 가 없는 채널 (드라이버로만 쓴다) */
  int          tx_pin;
  int          rx_pin;
} uart_hw_t;


#if CLI_USE(HW_UART)
static void cliUart(cli_args_t *args);
#endif


static bool is_init = false;

static uart_tbl_t uart_tbl[UART_MAX_CH];

const static uart_hw_t uart_hw_tbl[UART_MAX_CH] =
{
  {"UART0 DEBUG  ", UART_NUM_0,   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE},   // GPIO37 TX / GPIO38 RX (IDF 콘솔과 같은 핀)
  {"USB CDC      ", UART_NUM_MAX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE},   // cdc.c 의 cdc_uart_driver 를 uartSetDriver 로 꽂는다
};


bool uartInit(void)
{
  for (int i=0; i<UART_MAX_CH; i++)
  {
    uart_tbl[i].is_open    = false;
    uart_tbl[i].baud       = 57600;
    uart_tbl[i].rx_cnt     = 0;
    uart_tbl[i].tx_cnt     = 0;
    uart_tbl[i].p_driver   = NULL;
  }

  is_init = true;

#if CLI_USE(HW_UART)
  cliAdd("uart", cliUart);
#endif
  return true;
}

bool uartDeInit(void)
{
  return true;
}

bool uartIsInit(void)
{
  return is_init;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  bool ret = false;
  uart_port_t port;


  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    uart_tbl[ch].baud    = baud;
    uart_tbl[ch].is_open = uart_tbl[ch].p_driver->open(baud);
    return uart_tbl[ch].is_open;
  }

  if (uart_tbl[ch].is_open == true && uart_tbl[ch].baud == baud)
  {
    return true;
  }

  port = uart_hw_tbl[ch].port;
  if (port >= UART_NUM_MAX)
  {
    return false;
  }

  // 보레이트를 바꿔 다시 여는 경우. 드라이버는 그대로 두고 보레이트만 바꾼다.
  //
  if (uart_tbl[ch].is_open == true)
  {
    if (uart_set_baudrate(port, baud) == ESP_OK)
    {
      uart_tbl[ch].baud = baud;
      ret = true;
    }
    return ret;
  }

  uart_config_t uart_cfg =
  {
    .baud_rate  = (int)baud,
    .data_bits  = UART_DATA_8_BITS,
    .parity     = UART_PARITY_DISABLE,
    .stop_bits  = UART_STOP_BITS_1,
    .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
  };

  // 콘솔(IDF 로그)이 이미 같은 UART 를 쓰고 있을 수 있다. 드라이버만 새로 올린다.
  //
  if (uart_is_driver_installed(port) != true)
  {
    if (uart_driver_install(port, UART_RX_BUF_LENGTH, UART_RX_BUF_LENGTH, 0, NULL, 0) != ESP_OK)
    {
      return false;
    }
  }
  if (uart_param_config(port, &uart_cfg) != ESP_OK)
  {
    return false;
  }
  uart_set_pin(port, uart_hw_tbl[ch].tx_pin, uart_hw_tbl[ch].rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_flush_input(port);

  uart_tbl[ch].baud    = baud;
  uart_tbl[ch].is_open = true;
  ret = true;

  return ret;
}

bool uartClose(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    uart_tbl[ch].p_driver->close();
  }
  else if (uart_tbl[ch].is_open == true)
  {
    uart_driver_delete(uart_hw_tbl[ch].port);
  }

  uart_tbl[ch].is_open = false;

  return true;
}

bool uartSetDriver(uint8_t ch, uart_driver_t *p_driver)
{
  if (ch >= UART_MAX_CH) return false;

  uart_tbl[ch].p_driver = p_driver;
  return true;
}

uint32_t uartAvailable(uint8_t ch)
{
  uint32_t ret = 0;
  size_t   len = 0;

  if (ch >= UART_MAX_CH) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->available();
  }

  if (uart_tbl[ch].is_open)
  {
    uart_get_buffered_data_len(uart_hw_tbl[ch].port, &len);
    ret = len;
  }

  return ret;
}

bool uartFlush(uint8_t ch)
{
  uint32_t pre_time;

  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->flush();
  }

  pre_time = millis();
  while(uartAvailable(ch))
  {
    if (millis()-pre_time >= 10)
    {
      break;
    }
    uartRead(ch);
  }

  return true;
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t ret = 0;

  if (ch >= UART_MAX_CH) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    ret = uart_tbl[ch].p_driver->read();
    uart_tbl[ch].rx_cnt++;
    return ret;
  }

  if (uart_tbl[ch].is_open)
  {
    uart_read_bytes(uart_hw_tbl[ch].port, &ret, 1, 0);
  }
  uart_tbl[ch].rx_cnt++;

  return ret;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  uint32_t ret = 0;
  int      wr_len;

  if (ch >= UART_MAX_CH) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    ret = uart_tbl[ch].p_driver->write(p_data, length);
    uart_tbl[ch].tx_cnt += ret;
    return ret;
  }

  if (uart_tbl[ch].is_open != true)
  {
    return 0;
  }

  // 송신 링 버퍼(4 KB)에 넣고 돌아온다. 버퍼가 차면 그만큼 기다린다.
  wr_len = uart_write_bytes(uart_hw_tbl[ch].port, (const char *)p_data, length);
  if (wr_len > 0)
  {
    ret = wr_len;
  }
  uart_tbl[ch].tx_cnt += ret;

  return ret;
}

uint32_t uartPrintf(uint8_t ch, const char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;
  uint32_t ret;

  va_start(args, fmt);
  len = vsnprintf(buf, 256, fmt, args);

  ret = uartWrite(ch, (uint8_t *)buf, len);

  va_end(args);


  return ret;
}

uint32_t uartGetBaud(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].baud;
}

uint32_t uartGetRxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].rx_cnt;
}

uint32_t uartGetTxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].tx_cnt;
}

#if CLI_USE(HW_UART)
void cliUart(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i=0; i<UART_MAX_CH; i++)
    {
      cliPrintf("_DEF_UART%d : %s, %d bps, rx %d, tx %d\n",
                i+1, uart_hw_tbl[i].p_msg, uartGetBaud(i), uartGetRxCnt(i), uartGetTxCnt(i));
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "test"))
  {
    uint8_t uart_ch;

    uart_ch = constrain(args->getData(1), 1, UART_MAX_CH) - 1;

    if (uart_ch != cliGetPort())
    {
      uint8_t rx_data;

      while(1)
      {
        if (uartAvailable(uart_ch) > 0)
        {
          rx_data = uartRead(uart_ch);
          cliPrintf("<- _DEF_UART%d RX : 0x%X\n", uart_ch + 1, rx_data);
        }

        if (cliAvailable() > 0)
        {
          rx_data = cliRead();
          if (rx_data == 'q')
          {
            break;
          }
          else
          {
            uartWrite(uart_ch, &rx_data, 1);
            cliPrintf("-> _DEF_UART%d TX : 0x%X\n", uart_ch + 1, rx_data);
          }
        }
        delay(1);
      }
    }
    else
    {
      cliPrintf("This is cliPort\n");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("uart info\n");
    cliPrintf("uart test ch[1~%d]\n", HW_UART_MAX_CH);
  }
}
#endif


#endif
