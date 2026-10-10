#include "lcd.h"


#ifdef _USE_HW_LCD
#include "cli.h"
#include "lcd/st7701.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"


/* JC4880P443C LCD — ST7701S, MIPI-DSI 2 레인 (docs/22-lcd-touch.md)

   올리는 순서 (ultramcu BSP 에서 검증된 순서, docs/22 2절)
     LDO3 2.5V → DSI 버스 → DBI → DPI → 패널 리셋 → 초기화 테이블 → panel_init → 백라이트

   - DSI PHY 클럭은 MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT(XTAL). MIPI_DSI_PHY_CLK_SRC_DEFAULT 는
     rev < 3 전용 PLL_F20M 의 별칭이라 rev 3 칩에서 abort 한다 (알려진 문제 #1)
   - DPI 프레임버퍼 두 장(num_fbs = 2). 다 그린 버퍼만 스캔한다 (알려진 문제 #5)
   - 프레임버퍼는 패널 방향(세로 480x800). 그리기 함수는 화면 좌표(가로 800x480)를 받아 여기서 바꾼다 */


#define PANEL_W             HW_LCD_PANEL_WIDTH
#define PANEL_H             HW_LCD_PANEL_HEIGHT
#define FB_SIZE             (PANEL_W * PANEL_H * sizeof(uint16_t))

#define BL_LEDC_TIMER       LEDC_TIMER_0
#define BL_LEDC_CH          LEDC_CHANNEL_0
#define BL_LEDC_RES         LEDC_TIMER_10_BIT
#define BL_LEDC_MAX         ((1 << 10) - 1)


static bool lcdInitPanel(void);
static void lcdInitBackLight(void);
static bool lcdOnVsync(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx);
#if CLI_USE(HW_LCD)
static void cliLcd(cli_args_t *args);
#endif


static bool is_init = false;
static uint8_t backlight_value = 0;

static esp_ldo_channel_handle_t  ldo_phy  = NULL;
static esp_lcd_dsi_bus_handle_t  dsi_bus  = NULL;
static esp_lcd_panel_io_handle_t dbi_io   = NULL;
static esp_lcd_panel_handle_t    dpi_panel = NULL;

static uint16_t         *frame_buf[2] = {NULL, NULL};
static uint8_t           draw_index = 1;          // 다음에 그릴 버퍼. 0 번이 먼저 표시된다
static SemaphoreHandle_t vsync_sem = NULL;
static volatile uint32_t vsync_cnt = 0;
static uint32_t          update_ms = 0;




bool lcdInit(void)
{
  bool ret;

  // 초기화 중 쓰레기 화면이 보이지 않게 백라이트부터 끈다
  lcdInitBackLight();
  lcdSetBackLight(0);

  ret = lcdInitPanel();
  if (ret == true)
  {
    is_init = true;

    lcdClear(black);
    lcdSetBackLight(100);
  }

  logPrintf("[%s] lcdInit()\n", ret ? "OK":"E_");
  logPrintf("     ST7701S %dx%d, MIPI-DSI %d lane %d Mbps, %d MHz\n",
            PANEL_W, PANEL_H, HW_LCD_DSI_LANES, HW_LCD_DSI_MBPS, HW_LCD_DPI_MHZ);
  logPrintf("     screen %dx%d, rotation %d\n", LCD_WIDTH, LCD_HEIGHT, HW_LCD_ROTATION);

#if CLI_USE(HW_LCD)
  cliAdd("lcd", cliLcd);
#endif
  return ret;
}

bool lcdInitPanel(void)
{
  esp_err_t err;


  // 1. DSI PHY 전원
  //
  esp_ldo_channel_config_t ldo_cfg =
  {
    .chan_id    = HW_LCD_LDO_CHAN,
    .voltage_mv = HW_LCD_LDO_MV,
  };
  err = esp_ldo_acquire_channel(&ldo_cfg, &ldo_phy);
  if (err != ESP_OK) { logPrintf("[E_] lcd ldo %s\n", esp_err_to_name(err)); return false; }

  // 2. DSI 버스
  //
  esp_lcd_dsi_bus_config_t bus_cfg =
  {
    .bus_id             = 0,
    .num_data_lanes     = HW_LCD_DSI_LANES,
    .phy_clk_src        = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT,
    .lane_bit_rate_mbps = HW_LCD_DSI_MBPS,
  };
  err = esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus);
  if (err != ESP_OK) { logPrintf("[E_] lcd dsi bus %s\n", esp_err_to_name(err)); return false; }

  // 3. DBI (명령 채널)
  //
  esp_lcd_dbi_io_config_t dbi_cfg =
  {
    .virtual_channel = 0,
    .lcd_cmd_bits    = 8,
    .lcd_param_bits  = 8,
  };
  err = esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io);
  if (err != ESP_OK) { logPrintf("[E_] lcd dbi %s\n", esp_err_to_name(err)); return false; }

  // 4. DPI (영상 채널), 프레임버퍼 두 장
  //
  esp_lcd_dpi_panel_config_t dpi_cfg =
  {
    .virtual_channel    = 0,
    .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
    .dpi_clock_freq_mhz = HW_LCD_DPI_MHZ,
    .pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565,
    .in_color_format    = LCD_COLOR_FMT_RGB565,
    .num_fbs            = 2,
    .video_timing       =
    {
      .h_size            = PANEL_W,
      .v_size            = PANEL_H,
      .hsync_pulse_width = 12,
      .hsync_back_porch  = 42,
      .hsync_front_porch = 42,
      .vsync_pulse_width = 2,
      .vsync_back_porch  = 8,
      .vsync_front_porch = 166,
    },
    .flags.use_dma2d = 1,
  };
  err = esp_lcd_new_panel_dpi(dsi_bus, &dpi_cfg, &dpi_panel);
  if (err != ESP_OK) { logPrintf("[E_] lcd dpi %s\n", esp_err_to_name(err)); return false; }

  // 5. 패널 리셋 — DPI 를 만든 뒤, 초기화 테이블 전에
  //
  gpio_config_t rst_cfg =
  {
    .pin_bit_mask = 1ULL << HW_LCD_PIN_RST,
    .mode         = GPIO_MODE_OUTPUT,
  };
  gpio_config(&rst_cfg);
  gpio_set_level(HW_LCD_PIN_RST, 0);
  delay(20);
  gpio_set_level(HW_LCD_PIN_RST, 1);
  delay(120);

  // 6. 초기화 테이블 (SLPOUT, DISPON 포함)
  //
  if (st7701InitRegs(dbi_io) != true)
  {
    return false;
  }

  // 7. DPI 시작
  //
  err = esp_lcd_panel_init(dpi_panel);
  if (err != ESP_OK) { logPrintf("[E_] lcd panel init %s\n", esp_err_to_name(err)); return false; }

  err = esp_lcd_dpi_panel_get_frame_buffer(dpi_panel, 2, (void **)&frame_buf[0], (void **)&frame_buf[1]);
  if (err != ESP_OK) { logPrintf("[E_] lcd fb %s\n", esp_err_to_name(err)); return false; }

  vsync_sem = xSemaphoreCreateBinary();
  esp_lcd_dpi_panel_event_callbacks_t cbs =
  {
    .on_vsync = lcdOnVsync,
  };
  esp_lcd_dpi_panel_register_event_callbacks(dpi_panel, &cbs, NULL);

  return true;
}

void lcdInitBackLight(void)
{
  ledc_timer_config_t timer_cfg =
  {
    .speed_mode      = LEDC_LOW_SPEED_MODE,
    .duty_resolution = BL_LEDC_RES,
    .timer_num       = BL_LEDC_TIMER,
    .freq_hz         = HW_LCD_BL_FREQ_HZ,
    .clk_cfg         = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&timer_cfg);

  ledc_channel_config_t ch_cfg =
  {
    .gpio_num   = HW_LCD_PIN_BL,
    .speed_mode = LEDC_LOW_SPEED_MODE,
    .channel    = BL_LEDC_CH,
    .timer_sel  = BL_LEDC_TIMER,
    .duty       = 0,
    .hpoint     = 0,
  };
  ledc_channel_config(&ch_cfg);
}

static bool lcdOnVsync(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
  BaseType_t woken = pdFALSE;

  vsync_cnt++;
  xSemaphoreGiveFromISR(vsync_sem, &woken);
  return woken == pdTRUE;
}

bool lcdIsInit(void)
{
  return is_init;
}

uint8_t lcdGetBackLight(void)
{
  return backlight_value;
}

void lcdSetBackLight(uint8_t value)
{
  if (value > 100) value = 100;

  backlight_value = value;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH, (BL_LEDC_MAX * value) / 100);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CH);
}

void lcdDisplayOff(void)
{
  if (is_init) st7701DisplayOn(dbi_io, false);
}

void lcdDisplayOn(void)
{
  if (is_init) st7701DisplayOn(dbi_io, true);
}

int32_t lcdGetWidth(void)
{
  return LCD_WIDTH;
}

int32_t lcdGetHeight(void)
{
  return LCD_HEIGHT;
}

uint16_t *lcdGetFrameBuffer(void)
{
  return frame_buf[draw_index];
}

uint16_t *lcdGetCurrentFrameBuffer(void)
{
  return frame_buf[draw_index ^ 1];
}

//-- 그린 버퍼를 표시한다.
//
//   draw_bitmap 에 프레임버퍼 자신을 넘기면 DPI 드라이버는 복사하지 않고 캐시만 내린 뒤
//   표시 버퍼를 바꾼다 (esp_lcd_panel_dpi.c). 다음 vsync 까지 기다리면 이전 버퍼는 스캔이 끝나
//   다시 그려도 된다. 이어 그릴 수 있게 방금 표시한 내용을 다음 버퍼로 복사해 둔다.
//
void lcdUpdateDraw(void)
{
  uint16_t *p_drawn;
  uint32_t  pre_time = millis();

  if (is_init != true) return;

  p_drawn = frame_buf[draw_index];

  xSemaphoreTake(vsync_sem, 0);
  esp_lcd_panel_draw_bitmap(dpi_panel, 0, 0, PANEL_W, PANEL_H, p_drawn);
  xSemaphoreTake(vsync_sem, pdMS_TO_TICKS(50));

  draw_index ^= 1;
  memcpy(frame_buf[draw_index], p_drawn, FB_SIZE);

  update_ms = millis() - pre_time;
}

//-- 화면 좌표(가로) → 패널 버퍼 인덱스(세로). touch.c 의 touchRotate() 와 반대 방향이다
//
static inline int32_t lcdIndex(int16_t x, int16_t y)
{
#if HW_LCD_ROTATION == 90
  int32_t px = y;
  int32_t py = (PANEL_H - 1) - x;
#elif HW_LCD_ROTATION == 270
  int32_t px = (PANEL_W - 1) - y;
  int32_t py = x;
#else
  int32_t px = x;
  int32_t py = y;
#endif
  return py * PANEL_W + px;
}

void lcdDrawPixel(int16_t x_pos, int16_t y_pos, uint32_t rgb_code)
{
  if (x_pos < 0 || x_pos >= LCD_WIDTH)  return;
  if (y_pos < 0 || y_pos >= LCD_HEIGHT) return;

  frame_buf[draw_index][lcdIndex(x_pos, y_pos)] = (uint16_t)rgb_code;
}

void lcdDrawFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
  int16_t x2 = x + w;
  int16_t y2 = y + h;

  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x2 > LCD_WIDTH)  x2 = LCD_WIDTH;
  if (y2 > LCD_HEIGHT) y2 = LCD_HEIGHT;

  for (int16_t j=y; j<y2; j++)
  {
    for (int16_t i=x; i<x2; i++)
    {
      frame_buf[draw_index][lcdIndex(i, j)] = color;
    }
  }
}

void lcdDrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
  lcdDrawFillRect(x,         y,         w, 1, color);
  lcdDrawFillRect(x,         y + h - 1, w, 1, color);
  lcdDrawFillRect(x,         y,         1, h, color);
  lcdDrawFillRect(x + w - 1, y,         1, h, color);
}

void lcdDrawFillScreen(uint16_t color)
{
  uint16_t *p_buf = frame_buf[draw_index];

  for (int i=0; i<PANEL_W * PANEL_H; i++)
  {
    p_buf[i] = color;
  }
}

void lcdClear(uint32_t rgb_code)
{
  if (is_init != true) return;

  lcdDrawFillScreen(rgb_code);
  lcdUpdateDraw();
}


#if CLI_USE(HW_LCD)
static void lcdDrawTest(void)
{
  const uint16_t bar_color[8] = {white, yellow, lightblue, green, purple, red, blue, black};
  int16_t bar_w = LCD_WIDTH / 8;

  for (int i=0; i<8; i++)
  {
    lcdDrawFillRect(i * bar_w, 0, bar_w, LCD_HEIGHT, bar_color[i]);
  }

  // 방향 표식 : 왼쪽 위 빨강, 오른쪽 위 초록, 왼쪽 아래 파랑, 오른쪽 아래 흰색 (모두 60x60)
  lcdDrawFillRect(0,              0,               60, 60, red);
  lcdDrawFillRect(LCD_WIDTH - 60, 0,               60, 60, green);
  lcdDrawFillRect(0,              LCD_HEIGHT - 60, 60, 60, blue);
  lcdDrawFillRect(LCD_WIDTH - 60, LCD_HEIGHT - 60, 60, 60, white);
  lcdDrawRect(0, 0, LCD_WIDTH, LCD_HEIGHT, white);

  lcdUpdateDraw();
}

void cliLcd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_init   : %s\n", is_init ? "True" : "False");
    cliPrintf("panel     : %d x %d\n", PANEL_W, PANEL_H);
    cliPrintf("screen    : %d x %d, rotation %d\n", LCD_WIDTH, LCD_HEIGHT, HW_LCD_ROTATION);
    cliPrintf("fb        : %p, %p (draw %d)\n", frame_buf[0], frame_buf[1], draw_index);
    cliPrintf("backlight : %d %%, %d Hz\n", backlight_value, HW_LCD_BL_FREQ_HZ);
    cliPrintf("update    : %d ms\n", (int)update_ms);

    uint32_t cnt = vsync_cnt;
    delay(1000);
    cliPrintf("fps       : %d\n", (int)(vsync_cnt - cnt));
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "bl"))
  {
    lcdSetBackLight(constrain(args->getData(1), 0, 100));
    cliPrintf("backlight : %d %%\n", lcdGetBackLight());
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "test"))
  {
    lcdDrawTest();
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "clear"))
  {
    lcdClear((uint16_t)args->getData(1));
    ret = true;
  }

  // DSI 링크만 시험한다 (프레임버퍼와 무관). 0 = 끄기, 1 = 세로 막대, 2 = 가로 막대, 3 = BER
  if (args->argc == 2 && args->isStr(0, "pattern"))
  {
    esp_err_t err = esp_lcd_dpi_panel_set_pattern(dpi_panel, (mipi_dsi_pattern_type_t)constrain(args->getData(1), 0, 3));
    cliPrintf("pattern   : %s\n", esp_err_to_name(err));
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "display"))
  {
    if (args->isStr(1, "on")) lcdDisplayOn();
    else                      lcdDisplayOff();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("lcd info\n");
    cliPrintf("lcd bl 0~100\n");
    cliPrintf("lcd test\n");
    cliPrintf("lcd clear rgb565\n");
    cliPrintf("lcd pattern 0~3\n");
    cliPrintf("lcd display on:off\n");
  }
}
#endif

#endif
