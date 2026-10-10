#include "app.h"
#include "ui_theme.h"
#include "launcher.h"
#include "ota/ota.h"
#include "../pingpet/pet.h"


#ifdef _USE_HW_LVGL

/* 펌웨어 업데이트 화면 — USB OTA 를 받는 동안 (ui.c 가 otaIsActive() 를 보고 띄운다).
 *
 *   왼쪽 : 답답이 (웃는 눈 + 오물거리는 입, 펌웨어를 먹는 중)
 *   오른쪽 : 제목, 버전, 진행 막대, 퍼센트, 안내 (전원을 끄지 마세요 / 다시 시작 / 실패 이유)
 *
 * 폰트 업데이트는 폰트 파티션을 지우며 쓰므로 그동안 Jua · Silkscreen 을 쓸 수 없다.
 * 그래서 이 화면은 대상이 fonts 이면 LVGL 내장 Montserrat 로 영문만 쓴다.
 * 끝나면 (IDLE) 기본 app 으로 돌아간다.
 */


#define BAR_W             380
#define BAR_H             28


static lv_obj_t   *lbl_title;
static lv_obj_t   *lbl_version;
static lv_obj_t   *bar;
static lv_obj_t   *lbl_pct;
static lv_obj_t   *lbl_msg;
static bool        is_fonts;
static int         shown_state;
static int         shown_pct;


static const lv_font_t *font(int32_t size)
{
  if (is_fonts)
    return size >= 28 ? &lv_font_montserrat_28 : size >= 20 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
  return uiFont(size);
}

static const lv_font_t *fontPixel(int32_t size)
{
  if (is_fonts)
    return &lv_font_montserrat_40;
  return uiFontPixel(size);
}

static void showState(const ota_info_t *p_info)
{
  uint32_t color = UI_COLOR_INK_DIM;
  char     buf[160];

  switch (p_info->state)
  {
    case OTA_STATE_RUN:
      lv_label_set_text(lbl_msg, is_fonts ? "Do not unplug USB" : "USB 를 뽑거나 전원을 끄지 마세요");
      break;

    case OTA_STATE_DONE:
      lv_label_set_text(lbl_msg, is_fonts ? "Done. Restarting..." : "다 받았어요. 다시 시작합니다");
      color = UI_COLOR_OK;
      break;

    case OTA_STATE_FAIL:
      if (is_fonts)
        snprintf(buf, sizeof(buf), "Failed. Restarting...");
      else
        snprintf(buf, sizeof(buf), "업데이트 실패 : %s\n지금 펌웨어로 계속 동작해요", p_info->error);
      lv_label_set_text(lbl_msg, buf);
      color = UI_COLOR_ERROR;
      petSetState(PET_STATE_CRY);
      break;

    default:
      break;
  }
  lv_obj_set_style_text_color(lbl_msg, lv_color_hex(color), 0);
}

static void showProgress(const ota_info_t *p_info)
{
  int pct = p_info->size > 0 ? (int)((uint64_t)p_info->done * 100 / p_info->size) : 0;

  if (pct == shown_pct)
    return;
  shown_pct = pct;
  lv_bar_set_value(bar, pct, LV_ANIM_OFF);
  lv_label_set_text_fmt(lbl_pct, "%d%%", pct);
}


static bool updateEnter(lv_obj_t *scr)
{
  lv_obj_t  *left, *right, *pet;
  ota_info_t info;
  char       buf[96];

  otaGetInfo(&info);
  is_fonts    = (info.target == OTA_TARGET_FONTS);
  shown_state = -1;
  shown_pct   = -1;

  uiApplyScreen(scr);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(scr, UI_SPACE_XL * 2, 0);

  left = uiCreateBox(scr);
  lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  pet = petCreate(left);
  LV_UNUSED(pet);
  petSetState(PET_STATE_UPDATE);

  right = uiCreateBox(scr);
  lv_obj_set_size(right, BAR_W, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(right, UI_SPACE_MD, 0);

  lbl_title = uiCreateLabel(right, is_fonts ? "Updating fonts" : "펌웨어 업데이트 중", font(28), UI_COLOR_INK);

  if (is_fonts)
    snprintf(buf, sizeof(buf), "%s", info.version);
  else
    snprintf(buf, sizeof(buf), "지금 %s  /  새 %s", _DEF_FIRMWATRE_VERSION, info.version);   // Jua 에 화살표가 없다
  lbl_version = uiCreateLabel(right, buf, font(18), UI_COLOR_INK_DIM);

  bar = lv_bar_create(right);
  lv_obj_set_size(bar, BAR_W, BAR_H);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COLOR_CARD), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(bar, UI_BORDER, LV_PART_MAIN);
  lv_obj_set_style_border_color(bar, lv_color_hex(UI_COLOR_INK), LV_PART_MAIN);
  lv_obj_set_style_pad_all(bar, 4, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COLOR_GAUGE_NEW), LV_PART_INDICATOR);

  lbl_pct = uiCreateLabel(right, "0%", fontPixel(40), UI_COLOR_INK);
  lbl_msg = uiCreateLabel(right, "", font(18), UI_COLOR_INK_DIM);
  lv_obj_set_width(lbl_msg, BAR_W);
  lv_label_set_long_mode(lbl_msg, LV_LABEL_LONG_MODE_WRAP);

  LV_UNUSED(lbl_title);
  LV_UNUSED(lbl_version);
  return true;
}

static void updateUpdate(void)
{
  ota_info_t info;

  otaGetInfo(&info);
  if (info.state == OTA_STATE_IDLE)
  {
    launcherRequestApp(UI_APP_DEFAULT);
    return;
  }
  showProgress(&info);
  if ((int)info.state != shown_state)
  {
    shown_state = info.state;
    showState(&info);
  }
}

APP_DEF(update)
{
  .name   = "update",
  .order  = 90,
  .icon   = LV_SYMBOL_DOWNLOAD,
  .enter  = updateEnter,
  .update = updateUpdate,
};

#endif
