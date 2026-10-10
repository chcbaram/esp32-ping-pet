#include "touch/gt911.h"


#ifdef _USE_HW_GT911
#include "i2c.h"
#include "cli.h"
#include "driver/gpio.h"


#ifdef _USE_HW_RTOS
#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);
#else
#define lock()
#define unLock()
#endif


/* JC4880P443C 의 GT911 (docs/22-lcd-touch.md)

     I2C  : SDA GPIO7 / SCL GPIO8 (ES8311 코덱과 같은 버스)
     RST  : GPIO3
     INT  : 연결 안 됨 → 상태 레지스터(0x814E)를 폴링한다

   - 주소는 리셋하는 동안의 INT 레벨로 0x5D / 0x14 가 갈린다. INT 를 잡을 수 없으므로 둘 다 probe 한다.
   - 리셋 뒤 I2C 에 답하기까지 시간이 걸린다 (보고된 값 50 ~ 300 ms). 300 ms 까지 재시도한다.
   - 공장 설정(0x8047 ~)은 쓰지 않는다. 다른 보드(480x480)용 설정 테이블을 쓰면 해상도가 바뀐다.
     해상도는 0x8146 에서 읽는다. */

#define GT911_ADDR_1          0x5D
#define GT911_ADDR_2          0x14
#define GT911_RESET_WAIT_MS   300


static bool readRegs(uint16_t reg_addr, void *p_data, uint32_t length);
static bool writeRegs(uint16_t reg_addr, void *p_data, uint32_t length);
#if CLI_USE(HW_GT911)
static void cliCmd(cli_args_t *args);
#endif

static uint8_t  i2c_ch   = HW_I2C_CH_TOUCH;
static uint8_t  i2c_addr = GT911_ADDR_1;
static bool     is_init = false;
static bool     is_detected = false;
static uint16_t touch_width  = 0;
static uint16_t touch_height = 0;
static char     product_id[5] = {0};
static uint16_t fw_version = 0;
static uint32_t detect_ms = 0;
#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock = NULL;
#endif




bool gt911Init(void)
{
  bool     ret = false;
  uint32_t pre_time;
  uint8_t  buf[10];

  #ifdef _USE_HW_RTOS
  if (mutex_lock == NULL)
  {
    mutex_lock = xSemaphoreCreateMutex();
  }
  #endif

  if (i2cIsBegin(i2c_ch) == true)
    ret = true;
  else
    ret = i2cBegin(i2c_ch, 400);

  // 리셋
  //
  gpio_config_t rst_cfg =
  {
    .pin_bit_mask = 1ULL << HW_GT911_PIN_RST,
    .mode         = GPIO_MODE_OUTPUT,
  };
  gpio_config(&rst_cfg);
  gpio_set_level(HW_GT911_PIN_RST, 0);
  delay(10);
  gpio_set_level(HW_GT911_PIN_RST, 1);
  delay(50);

  // 주소 찾기. 답할 때까지 GT911_RESET_WAIT_MS 동안 재시도한다
  //
  pre_time = millis();
  while (ret == true)
  {
    if (i2cIsDeviceReady(i2c_ch, GT911_ADDR_1) == true)
    {
      i2c_addr    = GT911_ADDR_1;
      is_detected = true;
      break;
    }
    if (i2cIsDeviceReady(i2c_ch, GT911_ADDR_2) == true)
    {
      i2c_addr    = GT911_ADDR_2;
      is_detected = true;
      break;
    }
    if (millis() - pre_time >= GT911_RESET_WAIT_MS)
    {
      break;
    }
    delay(10);
  }
  detect_ms = millis() - pre_time + 50;

  // ID, 펌웨어 버전, 해상도 (0x8140 ~ 0x8149)
  //
  ret = false;
  if (is_detected == true && readRegs(GT911_PRODUCT_ID, buf, 10) == true)
  {
    memcpy(product_id, buf, 4);
    product_id[4] = 0;
    fw_version    = (buf[5] << 8) | buf[4];
    touch_width   = (buf[7] << 8) | buf[6];
    touch_height  = (buf[9] << 8) | buf[8];
    ret = true;
  }

  is_init = ret;

  logPrintf("[%s] gt911Init()\n", ret ? "OK":"NG");
  if (ret == true)
  {
    logPrintf("     addr 0x%02X, ID %s, FW 0x%04X, %dx%d, %d ms\n",
              i2c_addr, product_id, fw_version, touch_width, touch_height, (int)detect_ms);
  }

#if CLI_USE(HW_GT911)
  cliAdd("gt911", cliCmd);
#endif
  return ret;
}

bool readRegs(uint16_t reg_addr, void *p_data, uint32_t length)
{
  bool ret;

  lock();
  ret = i2cReadA16Bytes(i2c_ch, i2c_addr, reg_addr, p_data, length, 10);
  unLock();

  return ret;
}

bool writeRegs(uint16_t reg_addr, void *p_data, uint32_t length)
{
  bool ret;

  lock();
  ret = i2cWriteA16Bytes(i2c_ch, i2c_addr, reg_addr, p_data, length, 10);
  unLock();

  return ret;
}

uint16_t gt911GetWidth(void)
{
  return touch_width;
}

uint16_t gt911GetHeight(void)
{
  return touch_height;
}

bool gt911GetInfo(gt911_info_t *p_info)
{
  bool ret = false;
  uint8_t buf[8] = {0};
  uint8_t touch_cnt = 0;
  const uint16_t point_addr[] = GT911_POINTS_REG;


  p_info->count = 0;

  if (is_init == false)
  {
    return false;
  }

  // INT 가 없으므로 상태 레지스터의 buffer ready(bit7) 를 본다.
  // 준비가 안 됐으면 이전 상태를 모르는 것이므로 "변화 없음"(count 0, true) 으로 돌려준다.
  //
  ret = readRegs(GT911_POINT_INFO, buf, 1);
  if (ret != true)
  {
    return false;
  }
  if ((buf[0] & (1<<7)) == 0)
  {
    return true;
  }

  touch_cnt = buf[0] & 0x0F;
  if (touch_cnt > 5) touch_cnt = 5;

  for (int i=0; i<touch_cnt; i++)
  {
    ret = readRegs(point_addr[i], buf, 8);
    if (ret != true)
    {
      break;
    }
    p_info->point[i].id     = buf[0];
    p_info->point[i].event  = 0;
    p_info->point[i].weight = 0;
    p_info->point[i].area   = buf[5];
    p_info->point[i].x      = (buf[2]<<8) | (buf[1]<<0);
    p_info->point[i].y      = (buf[4]<<8) | (buf[3]<<0);
  }
  p_info->count = (ret == true) ? touch_cnt : 0;

  // 읽었으면 상태를 지운다 (다음 좌표를 받는다)
  buf[0] = 0;
  writeRegs(GT911_POINT_INFO, buf, 1);

  return ret;
}


#if CLI_USE(HW_GT911)
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_init  : %s\n", is_init ? "True" : "False");
    cliPrintf("addr     : 0x%02X\n", i2c_addr);
    cliPrintf("ID       : %s\n", product_id);
    cliPrintf("FW       : 0x%04X\n", fw_version);
    cliPrintf("res      : %d x %d\n", touch_width, touch_height);
    cliPrintf("detect   : %d ms after reset\n", (int)detect_ms);
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "read"))
  {
    uint16_t addr = (uint16_t)args->getData(1);
    uint32_t len  = constrain(args->getData(2), 1, 16);
    uint8_t  data[16];

    if (readRegs(addr, data, len) == true)
    {
      for (uint32_t i=0; i<len; i++)
      {
        cliPrintf("0x%04X : 0x%02X\n", addr + i, data[i]);
      }
    }
    else
    {
      cliPrintf("read fail\n");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("gt911 info\n");
    cliPrintf("gt911 read addr len\n");
  }
}
#endif

#endif
