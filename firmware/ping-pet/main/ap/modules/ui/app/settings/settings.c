#include "app.h"
#include "ui_theme.h"
#include "launcher.h"
#include "inbox.h"


#ifdef _USE_HW_LVGL

/* 설정 — Ping Pet 화면에서 시계(상단 바 왼쪽)를 길게 누르면 열린다.
 *
 *   글자 크기 : 보통 · 크게 · 아주 크게. 고르면 저장하고 이 화면을 그 크기로 다시 그린다 (미리 보기)
 *   화면 밝기 : 10 ~ 100 %. 끄는 동안 바로 적용하고, 손을 떼면 저장한다
 *   데모 모드 : 가상의 메시지로 4 상태를 돌려 본다
 *
 * 누를 수 있는 것은 모두 높이 UI_TOUCH_MIN(64) 이상.
 */


#define ROW_GAP           18


static lv_obj_t *lbl_bl;
static lv_obj_t *btn_demo;
static lv_obj_t *lbl_demo;


static lv_obj_t *button(lv_obj_t *parent, const char *text, bool selected, int32_t w)
{
  lv_obj_t *btn = uiCreateBox(parent);
  lv_obj_t *lbl;

  lv_obj_set_size(btn, w, UI_TOUCH_MIN);
  lv_obj_set_clickable(btn, true);
  lv_obj_set_style_border_width(btn, UI_BORDER, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(UI_COLOR_INK), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(selected ? UI_COLOR_INK : UI_COLOR_CARD), 0);
  lv_obj_set_style_translate_y(btn, 2, LV_STATE_PRESSED);

  lbl = uiCreateLabel(btn, text, uiFont(20), selected ? UI_COLOR_BG : UI_COLOR_INK);
  lv_obj_center(lbl);
  return btn;
}

static lv_obj_t *section(lv_obj_t *parent, const char *title)
{
  lv_obj_t *col = uiCreateBox(parent);

  lv_obj_set_width(col, LV_PCT(100));
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(col, 10, 0);
  uiCreateLabel(col, title, uiFont(18), UI_COLOR_INK_DIM);
  return col;
}

static lv_obj_t *hrow(lv_obj_t *parent)
{
  lv_obj_t *row = uiCreateBox(parent);

  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 12, 0);
  return row;
}


//-- 이벤트
//
static void onClose(lv_event_t *e)
{
  LV_UNUSED(e);
  launcherRequestApp(UI_APP_DEFAULT);
}

static void onFont(lv_event_t *e)
{
  uint8_t level = (uint8_t)(intptr_t)lv_event_get_user_data(e);

  if (level != uiThemeGetFontLevel())
  {
    uiThemeSetFontLevel(level);
    launcherRequestApp("settings");      // 새 크기로 다시 그린다
  }
}

static void onBrightness(lv_event_t *e)
{
  lv_obj_t *slider = lv_event_get_target(e);
  int32_t   v      = lv_slider_get_value(slider);

  if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
  {
    lcdSetBackLight(v);                  // 끄는 동안은 적용만
    lv_label_set_text_fmt(lbl_bl, "%d %%", (int)v);
  }
  else if (lv_event_get_code(e) == LV_EVENT_RELEASED)
  {
    uiThemeSetBrightness(v);             // 손을 떼면 저장
  }
}

static void demoButtonStyle(bool on)
{
  lv_obj_set_style_bg_color(btn_demo, lv_color_hex(on ? UI_COLOR_INK : UI_COLOR_CARD), 0);
  lv_obj_set_style_text_color(lbl_demo, lv_color_hex(on ? UI_COLOR_BG : UI_COLOR_INK), 0);
  lv_label_set_text(lbl_demo, on ? "켜짐" : "꺼짐");
}

static void onDemo(lv_event_t *e)
{
  bool on = !inboxDemoIsOn();

  LV_UNUSED(e);
  inboxDemoSet(on);
  demoButtonStyle(on);
}


//-- app
//
static bool settingsEnter(lv_obj_t *scr)
{
  lv_obj_t *bar, *body, *sec, *row, *obj;
  char      buf[64];

  uiApplyScreen(scr);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);

  // 상단 바 : 제목 + 닫기
  bar = uiCreateBox(scr);
  lv_obj_set_size(bar, LV_PCT(100), 64);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(bar, UI_BORDER, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(UI_COLOR_INK), 0);
  lv_obj_set_style_pad_hor(bar, UI_SPACE_LG, 0);

  obj = uiCreateLabel(bar, "설정", uiFont(26), UI_COLOR_INK);
  lv_obj_align(obj, LV_ALIGN_LEFT_MID, 0, 0);

  obj = button(bar, "닫기", false, 120);
  lv_obj_set_height(obj, 52);
  lv_obj_align(obj, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(obj, onClose, LV_EVENT_CLICKED, NULL);

  // 본문
  body = uiCreateBox(scr);
  lv_obj_set_width(body, LV_PCT(100));
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(body, UI_SPACE_XL, 0);
  lv_obj_set_style_pad_row(body, ROW_GAP, 0);
  lv_obj_set_scrollable(body, true);       // 아주 크게일 때 넘치면 스크롤

  // 글자 크기
  sec = section(body, "글자 크기");
  row = hrow(sec);
  for (int i=0; i<UI_FONT_LEVEL_MAX; i++)
  {
    obj = button(row, uiThemeGetFontLevelName(i), i == uiThemeGetFontLevel(), 180);
    lv_obj_add_event_cb(obj, onFont, LV_EVENT_CLICKED, (void *)(intptr_t)i);
  }

  // 화면 밝기
  sec = section(body, "화면 밝기");
  row = hrow(sec);
  lv_obj_set_style_pad_column(row, 36, 0);            // 손잡이가 숫자에 붙지 않게
  obj = lv_slider_create(row);
  lv_obj_set_size(obj, 440, 20);
  lv_obj_set_style_margin_left(obj, 12, 0);           // 손잡이가 왼쪽 끝에서 잘리지 않게
  lv_obj_set_style_margin_ver(obj, 22, 0);            // 누르는 높이 64 를 맞춘다
  lv_obj_set_ext_click_area(obj, 22);
  lv_slider_set_range(obj, 10, 100);
  lv_slider_set_value(obj, uiThemeGetBrightness(), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(obj, lv_color_hex(UI_COLOR_LINE_DASH), LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, lv_color_hex(UI_COLOR_INK), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(obj, lv_color_hex(UI_COLOR_INK), LV_PART_KNOB);
  lv_obj_add_event_cb(obj, onBrightness, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(obj, onBrightness, LV_EVENT_RELEASED, NULL);
  snprintf(buf, sizeof(buf), "%d %%", uiThemeGetBrightness());
  lbl_bl = uiCreateLabel(row, buf, uiFontPixel(22), UI_COLOR_INK);

  // 데모 모드
  sec = section(body, "데모 모드 (가상의 메시지로 화면 돌려 보기)");
  row = hrow(sec);
  btn_demo = button(row, "", false, 180);
  lbl_demo = lv_obj_get_child(btn_demo, 0);
  lv_obj_add_event_cb(btn_demo, onDemo, LV_EVENT_CLICKED, NULL);
  demoButtonStyle(inboxDemoIsOn());

  // 정보
  snprintf(buf, sizeof(buf), "%s %s · SN %s", _DEF_BOARD_NAME, _DEF_FIRMWATRE_VERSION, usbGetSerial());
  uiCreateLabel(body, buf, uiFont(15), UI_COLOR_INK_DIM);

  return true;
}

APP_DEF(settings)
{
  .name   = "settings",
  .order  = 10,
  .icon   = LV_SYMBOL_SETTINGS,
  .enter  = settingsEnter,
};

#endif
