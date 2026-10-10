#include "lv_port_disp.h"
#include "lcd.h"
#include "esp_heap_caps.h"


#define BYTE_PER_PIXEL  (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565))
#define BUF_SIZE        (LCD_WIDTH * LCD_HEIGHT * BYTE_PER_PIXEL)
#define BUF_ALIGN       128       // PPA 입력 : L2 캐시 라인


static void disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);


static lv_display_t   *disp = NULL;
static volatile bool   disp_flush_enabled = true;
static uint16_t       *draw_buf = NULL;




void lv_port_disp_init(void)
{
  disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);

  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, disp_flush);

  /* stm32h7-lvgl 은 LTDC 프레임버퍼 두 장을 그대로 LVGL 의 DIRECT 버퍼로 넘긴다.
   *
   * 이 보드는 패널이 세로(480x800)이고 화면은 가로(800x480)라 그대로 넘길 수 없다.
   * LVGL 은 PSRAM 의 화면 버퍼 한 장에 DIRECT 모드로 그리고 (바뀐 영역만 다시 그린다),
   * 마지막 flush 에서 lcdDrawScreen() 이 PPA 로 돌려 DPI 프레임버퍼 두 장을 번갈아 채운다.
   * PPA 가 블로킹으로 끝난 뒤 돌아오므로 화면 버퍼 한 장이면 된다.
   */
  draw_buf = heap_caps_aligned_alloc(BUF_ALIGN, BUF_SIZE, MALLOC_CAP_SPIRAM);
  if (draw_buf == NULL)
  {
    logPrintf("[E_] lv_port_disp_init() draw_buf\n");
    return;
  }
  memset(draw_buf, 0, BUF_SIZE);

  lv_display_set_buffers(disp,
                         draw_buf,
                         NULL,
                         BUF_SIZE,
                         LV_DISPLAY_RENDER_MODE_DIRECT);
}

void disp_enable_update(void)
{
  disp_flush_enabled = true;
}

void disp_disable_update(void)
{
  disp_flush_enabled = false;
}

/* DIRECT 모드라 px_map 은 화면 버퍼 전체다. 마지막 area 일 때만 통째로 돌려 표시한다.
 */
void disp_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
  LV_UNUSED(area);

  if (disp_flush_enabled == true && lv_display_flush_is_last(disp_drv) == true)
  {
    lcdDrawScreen((const uint16_t *)px_map);
  }

  lv_display_flush_ready(disp_drv);
}
