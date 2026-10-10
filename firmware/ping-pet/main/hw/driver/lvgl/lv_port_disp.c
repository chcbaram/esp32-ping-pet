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
static bool            flush_full = false;   // true : 매번 화면 전체를 돌린다 (비교용)
static lv_area_t       area_cur;             // 이번 프레임에 바뀐 영역 (flush 들의 합)
static lv_area_t       area_prev;            // 직전 프레임에 바뀐 영역
static bool            area_cur_valid  = false;
static bool            area_prev_valid = false;
static bool            force_full = false;    // 지난 갱신을 못 했으면 다음에 전체를 돌린다




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

void disp_set_flush_full(bool enable)
{
  flush_full = enable;
}

bool disp_get_flush_full(void)
{
  return flush_full;
}

static void area_join(lv_area_t *p_dst, bool *p_valid, const lv_area_t *p_src)
{
  if (*p_valid == false)
  {
    *p_dst   = *p_src;
    *p_valid = true;
    return;
  }
  p_dst->x1 = LV_MIN(p_dst->x1, p_src->x1);
  p_dst->y1 = LV_MIN(p_dst->y1, p_src->y1);
  p_dst->x2 = LV_MAX(p_dst->x2, p_src->x2);
  p_dst->y2 = LV_MAX(p_dst->y2, p_src->y2);
}

/* DIRECT 모드라 px_map 은 화면 버퍼 전체이고, area 는 이번에 다시 그린 영역이다.
 *
 * 한 프레임의 area 들을 모아 두었다가 마지막 flush 에서 한 번에 돌린다.
 * DPI 프레임버퍼가 두 장이라 이번에 쓸 버퍼에는 두 프레임 전 내용이 남아 있다.
 * 그래서 이번 영역과 직전 영역을 합친 영역을 돌려야 두 버퍼가 같아진다.
 */
void disp_flush(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *px_map)
{
  area_join(&area_cur, &area_cur_valid, area);

  if (disp_flush_enabled == true && lv_display_flush_is_last(disp_drv) == true)
  {
    if (flush_full == true || force_full == true)
    {
      force_full = (lcdDrawScreen((const uint16_t *)px_map) != true);
    }
    else
    {
      lv_area_t area_draw = area_cur;
      bool      valid     = true;

      area_join(&area_draw, &valid, area_prev_valid ? &area_prev : &area_cur);

      // 못 그렸으면 (캡처 중 멈춤 등) 두 버퍼가 어긋났으니 다음에 전체를 돌린다
      force_full = (lcdDrawScreenArea((const uint16_t *)px_map,
                                      area_draw.x1,
                                      area_draw.y1,
                                      lv_area_get_width(&area_draw),
                                      lv_area_get_height(&area_draw)) != true);
    }

    area_prev       = area_cur;
    area_prev_valid = true;
    area_cur_valid  = false;
  }

  lv_display_flush_ready(disp_drv);
}
