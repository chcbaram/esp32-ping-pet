#include "i2c.h"
#include "cli.h"

#ifdef _USE_HW_I2C
#include "driver/i2c_master.h"


/* ESP-IDF i2c_master 위에 i2c.h API 를 얹는다.
   i2c_master 는 장치(주소)마다 핸들이 있어야 해서, 처음 쓰는 주소는 핸들을 만들어 표에 남겨 둔다. */


#define I2C_DEV_MAX         8


typedef struct
{
  bool                    is_used;
  uint8_t                 addr;
  i2c_master_dev_handle_t handle;
} i2c_dev_t;

typedef struct
{
  bool                    is_begin;
  uint32_t                freq_khz;
  uint32_t                timeout;
  uint32_t                err_count;
  i2c_master_bus_handle_t bus;
  i2c_dev_t               dev[I2C_DEV_MAX];
} i2c_tbl_t;

typedef struct
{
  const char *p_msg;
  i2c_port_t  port;
  int         sda;
  int         scl;
} i2c_hw_t;


#if CLI_USE(HW_I2C)
static void cliI2C(cli_args_t *args);
#endif
static i2c_master_dev_handle_t i2cGetDev(uint8_t ch, uint8_t dev_addr);


static bool is_init = false;
static i2c_tbl_t i2c_tbl[I2C_MAX_CH];

static const i2c_hw_t i2c_hw_tbl[I2C_MAX_CH] =
{
  {"I2C0 TOUCH", I2C_NUM_0, HW_I2C_CH1_SDA, HW_I2C_CH1_SCL},   // GT911 + ES8311 (같은 버스)
};




bool i2cInit(void)
{
  for (int i=0; i<I2C_MAX_CH; i++)
  {
    memset(&i2c_tbl[i], 0, sizeof(i2c_tbl_t));
    i2c_tbl[i].timeout = 10;
  }
  is_init = true;

#if CLI_USE(HW_I2C)
  cliAdd("i2c", cliI2C);
#endif
  return true;
}

bool i2cIsInit(void)
{
  return is_init;
}

bool i2cBegin(uint8_t ch, uint32_t freq_khz)
{
  esp_err_t err;

  if (ch >= I2C_MAX_CH) return false;

  i2c_tbl[ch].freq_khz = freq_khz;

  if (i2c_tbl[ch].is_begin == true)
  {
    return true;
  }

  i2c_master_bus_config_t bus_cfg =
  {
    .i2c_port                     = i2c_hw_tbl[ch].port,
    .sda_io_num                   = i2c_hw_tbl[ch].sda,
    .scl_io_num                   = i2c_hw_tbl[ch].scl,
    .clk_source                   = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt            = 7,
    .flags.enable_internal_pullup = true,
  };

  err = i2c_new_master_bus(&bus_cfg, &i2c_tbl[ch].bus);
  if (err != ESP_OK)
  {
    logPrintf("[E_] i2cBegin(%d) %s\n", ch, esp_err_to_name(err));
    return false;
  }

  i2c_tbl[ch].is_begin = true;
  return true;
}

bool i2cIsBegin(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return false;

  return i2c_tbl[ch].is_begin;
}

void i2cReset(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return;

  if (i2c_tbl[ch].is_begin == true)
  {
    i2c_master_bus_reset(i2c_tbl[ch].bus);
  }
}

bool i2cIsDeviceReady(uint8_t ch, uint8_t dev_addr)
{
  if (ch >= I2C_MAX_CH) return false;
  if (i2c_tbl[ch].is_begin != true) return false;

  return i2c_master_probe(i2c_tbl[ch].bus, dev_addr, (int)i2c_tbl[ch].timeout) == ESP_OK;
}

bool i2cRecovery(uint8_t ch)
{
  i2cReset(ch);
  return true;
}

static i2c_master_dev_handle_t i2cGetDev(uint8_t ch, uint8_t dev_addr)
{
  i2c_tbl_t *p_tbl = &i2c_tbl[ch];
  int        empty = -1;

  if (p_tbl->is_begin != true) return NULL;

  for (int i=0; i<I2C_DEV_MAX; i++)
  {
    if (p_tbl->dev[i].is_used == true && p_tbl->dev[i].addr == dev_addr)
    {
      return p_tbl->dev[i].handle;
    }
    if (p_tbl->dev[i].is_used == false && empty < 0)
    {
      empty = i;
    }
  }
  if (empty < 0) return NULL;

  i2c_device_config_t dev_cfg =
  {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address  = dev_addr,
    .scl_speed_hz    = p_tbl->freq_khz * 1000,
  };

  if (i2c_master_bus_add_device(p_tbl->bus, &dev_cfg, &p_tbl->dev[empty].handle) != ESP_OK)
  {
    return NULL;
  }
  p_tbl->dev[empty].is_used = true;
  p_tbl->dev[empty].addr    = dev_addr;

  return p_tbl->dev[empty].handle;
}

static bool i2cResult(uint8_t ch, esp_err_t err)
{
  if (err != ESP_OK)
  {
    i2c_tbl[ch].err_count++;
    return false;
  }
  return true;
}

bool i2cReadByte (uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t timeout)
{
  return i2cReadBytes(ch, dev_addr, reg_addr, p_data, 1, timeout);
}

bool i2cReadBytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  i2c_master_dev_handle_t dev;
  uint8_t reg = (uint8_t)reg_addr;

  if (ch >= I2C_MAX_CH) return false;
  if ((dev = i2cGetDev(ch, dev_addr)) == NULL) return false;

  return i2cResult(ch, i2c_master_transmit_receive(dev, &reg, 1, p_data, length, (int)timeout));
}

bool i2cReadA16Bytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  i2c_master_dev_handle_t dev;
  uint8_t reg[2] = {(uint8_t)(reg_addr >> 8), (uint8_t)(reg_addr >> 0)};

  if (ch >= I2C_MAX_CH) return false;
  if ((dev = i2cGetDev(ch, dev_addr)) == NULL) return false;

  return i2cResult(ch, i2c_master_transmit_receive(dev, reg, 2, p_data, length, (int)timeout));
}

bool i2cWriteByte (uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t data, uint32_t timeout)
{
  return i2cWriteBytes(ch, dev_addr, reg_addr, &data, 1, timeout);
}

bool i2cWriteBytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  i2c_master_dev_handle_t dev;
  uint8_t reg = (uint8_t)reg_addr;

  if (ch >= I2C_MAX_CH) return false;
  if ((dev = i2cGetDev(ch, dev_addr)) == NULL) return false;

  i2c_master_transmit_multi_buffer_info_t buf[2] =
  {
    {.write_buffer = &reg,   .buffer_size = 1},
    {.write_buffer = p_data, .buffer_size = length},
  };
  return i2cResult(ch, i2c_master_multi_buffer_transmit(dev, buf, 2, (int)timeout));
}

bool i2cWriteA16Bytes(uint8_t ch, uint16_t dev_addr, uint16_t reg_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  i2c_master_dev_handle_t dev;
  uint8_t reg[2] = {(uint8_t)(reg_addr >> 8), (uint8_t)(reg_addr >> 0)};

  if (ch >= I2C_MAX_CH) return false;
  if ((dev = i2cGetDev(ch, dev_addr)) == NULL) return false;

  i2c_master_transmit_multi_buffer_info_t buf[2] =
  {
    {.write_buffer = reg,    .buffer_size = 2},
    {.write_buffer = p_data, .buffer_size = length},
  };
  return i2cResult(ch, i2c_master_multi_buffer_transmit(dev, buf, 2, (int)timeout));
}

bool i2cReadData(uint8_t ch, uint16_t dev_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  i2c_master_dev_handle_t dev;

  if (ch >= I2C_MAX_CH) return false;
  if ((dev = i2cGetDev(ch, dev_addr)) == NULL) return false;

  return i2cResult(ch, i2c_master_receive(dev, p_data, length, (int)timeout));
}

bool i2cWriteData(uint8_t ch, uint16_t dev_addr, uint8_t *p_data, uint32_t length, uint32_t timeout)
{
  i2c_master_dev_handle_t dev;

  if (ch >= I2C_MAX_CH) return false;
  if ((dev = i2cGetDev(ch, dev_addr)) == NULL) return false;

  return i2cResult(ch, i2c_master_transmit(dev, p_data, length, (int)timeout));
}

void i2cSetTimeout(uint8_t ch, uint32_t timeout)
{
  if (ch >= I2C_MAX_CH) return;
  i2c_tbl[ch].timeout = timeout;
}

uint32_t i2cGetTimeout(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return 0;
  return i2c_tbl[ch].timeout;
}

void i2cClearErrCount(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return;
  i2c_tbl[ch].err_count = 0;
}

uint32_t i2cGetErrCount(uint8_t ch)
{
  if (ch >= I2C_MAX_CH) return 0;
  return i2c_tbl[ch].err_count;
}


#if CLI_USE(HW_I2C)
void cliI2C(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    for (int i=0; i<I2C_MAX_CH; i++)
    {
      cliPrintf("_DEF_I2C%d : %s, SDA %d, SCL %d, %s, %d KHz, err %d\n",
                i+1, i2c_hw_tbl[i].p_msg, i2c_hw_tbl[i].sda, i2c_hw_tbl[i].scl,
                i2c_tbl[i].is_begin ? "begin" : "-",
                (int)i2c_tbl[i].freq_khz, (int)i2c_tbl[i].err_count);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "scan"))
  {
    uint8_t ch = constrain(args->getData(1), 1, I2C_MAX_CH) - 1;

    if (i2cBegin(ch, i2c_tbl[ch].freq_khz > 0 ? i2c_tbl[ch].freq_khz : 400) == true)
    {
      for (int addr=0x08; addr<0x78; addr++)
      {
        if (i2cIsDeviceReady(ch, addr) == true)
        {
          cliPrintf("I2C CH%d Addr 0x%02X : OK\n", ch+1, addr);
        }
      }
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("i2c info\n");
    cliPrintf("i2c scan ch[1~%d]\n", I2C_MAX_CH);
  }
}
#endif

#endif
