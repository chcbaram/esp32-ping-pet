#include "ui.h"
#include "module.h"


#ifdef _USE_HW_LVGL

#define UI_THREAD_STACK   (8*1024)
#define UI_THREAD_PRI     3


static void uiThread(void *arg);
static void uiTestScreen(void);




bool uiInit(void)
{
  bool ret;


  lvglInit();
  uiTestScreen();

  ret = xTaskCreatePinnedToCore(uiThread, "ui", UI_THREAD_STACK, NULL, UI_THREAD_PRI, NULL, 1) == pdPASS;

  logPrintf("[%s] uiInit()\n", ret ? "OK":"NG");
  return ret;
}

/* LVGL 은 이 태스크에서만 부른다. */
void uiThread(void *arg)
{
  (void)arg;

  while(1)
  {
    lvglUpdate();
    delay(5);
  }
}


//-- LVGL 단계 확인용 화면 (그리기, 터치, 갱신). UI 단계에서 바꾼다.
//
static lv_obj_t *lbl_count;
static lv_obj_t *lbl_touch;
static lv_obj_t *lbl_slider;
static uint32_t  click_count = 0;

static void onButton(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_SHORT_CLICKED)
  {
    click_count++;
    lv_label_set_text_fmt(lbl_count, "clicked %d", (int)click_count);
  }
  else if (code == LV_EVENT_LONG_PRESSED)
  {
    lv_label_set_text(lbl_count, "long pressed");
  }
}

static void onSlider(lv_event_t *e)
{
  lv_obj_t *slider = lv_event_get_target(e);

  lv_label_set_text_fmt(lbl_slider, "%d %%", (int)lv_slider_get_value(slider));
}

static void onScreen(lv_event_t *e)
{
  lv_point_t p;

  lv_indev_get_point(lv_indev_active(), &p);
  lv_label_set_text_fmt(lbl_touch, "touch %d, %d", (int)p.x, (int)p.y);
}

void uiTestScreen(void)
{
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *obj;

  lv_obj_set_style_bg_color(scr, lv_color_hex(0xDCE8C8), 0);
  lv_obj_add_event_cb(scr, onScreen, LV_EVENT_PRESSING, NULL);

  obj = lv_label_create(scr);
  lv_label_set_text_fmt(obj, "Ping Pet - LVGL %d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  lv_obj_set_style_text_font(obj, &lv_font_montserrat_40, 0);
  lv_obj_set_style_text_color(obj, lv_color_hex(0x1F2A1A), 0);
  lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, 30);

  obj = lv_button_create(scr);
  lv_obj_set_size(obj, 240, 90);
  lv_obj_align(obj, LV_ALIGN_LEFT_MID, 60, 0);
  lv_obj_add_event_cb(obj, onButton, LV_EVENT_ALL, NULL);
  lbl_count = lv_label_create(obj);
  lv_label_set_text(lbl_count, "press me");
  lv_obj_center(lbl_count);

  obj = lv_slider_create(scr);
  lv_obj_set_width(obj, 340);
  lv_obj_align(obj, LV_ALIGN_RIGHT_MID, -60, 0);
  lv_obj_add_event_cb(obj, onSlider, LV_EVENT_VALUE_CHANGED, NULL);
  lbl_slider = lv_label_create(scr);
  lv_label_set_text(lbl_slider, "0 %");
  lv_obj_align_to(lbl_slider, obj, LV_ALIGN_OUT_TOP_MID, 0, -20);

  lbl_touch = lv_label_create(scr);
  lv_label_set_text(lbl_touch, "touch -, -");
  lv_obj_set_style_text_font(lbl_touch, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_touch, LV_ALIGN_BOTTOM_LEFT, 20, -20);

  // 모서리 표식 (lcd test 와 같은 색) — 회전 확인용
  const struct { lv_align_t align; uint32_t color; } mark[4] =
  {
    {LV_ALIGN_TOP_LEFT,     0xFF0000},
    {LV_ALIGN_TOP_RIGHT,    0x00FF00},
    {LV_ALIGN_BOTTOM_LEFT,  0x0000FF},
    {LV_ALIGN_BOTTOM_RIGHT, 0xFFFFFF},
  };
  for (int i=0; i<4; i++)
  {
    obj = lv_obj_create(scr);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, 30, 30);
    lv_obj_set_style_bg_color(obj, lv_color_hex(mark[i].color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_align(obj, mark[i].align, 0, 0);
  }
}


MODULE_DEF(ui)
{
  .name     = "ui",
  .priority = MODULE_PRI_NORMAL,
  .init     = uiInit,
};

#endif
