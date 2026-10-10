#include "app.h"
#include "ui_theme.h"
#include "pet.h"
#include "cli.h"


#ifdef _USE_HW_LVGL

/* Ping Pet — 답장을 기다리는 요청(핑)을 먹고 사는 책상 위 펫. 캐릭터 이름은 답답이.
 *
 * 화면 (800x480, 목업 기준)
 *   상단 바 48 : 시계 · 미응답 요약 · 연결 상태
 *   왼쪽 340   : 캐릭터, 대사, 배부름
 *   오른쪽     : 상태별 내용 (오늘 통계 / 답장 대기 카드 / 완료 카드)
 *
 * 지금은 폰트와 배치 확인용 껍데기다. 캐릭터와 4 상태는 다음 단계에서 채운다.
 */


#define BAR_H             48
#define LEFT_W            340


static void cliPet(cli_args_t *args);

static volatile int  req_state = -1;      // CLI → ui 태스크 (LVGL 은 ui 태스크에서만 부른다)
static lv_obj_t     *lbl_msg;

static const char *pet_msg[PET_STATE_MAX] =
{
  "배불러요!",
  "배고파요…",
  "엉엉… 너무 배고파요",
  "냠냠! 맛있어요",
};


static bool pingpetEnter(lv_obj_t *scr)
{
  lv_obj_t *bar, *left, *right, *obj;

  uiApplyScreen(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // 상단 바
  bar = uiCreateBox(scr);
  lv_obj_set_size(bar, LV_PCT(100), BAR_H);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(bar, UI_BORDER, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(UI_COLOR_INK), 0);
  lv_obj_set_style_pad_hor(bar, UI_SPACE_LG, 0);

  // 시계 + 날짜 · 요일 (맥이 보내는 시각으로 계산한다. 지금은 샘플)
  lv_obj_t *clock = uiCreateLabel(bar, "14:05", uiFontPixel(28), UI_COLOR_INK);
  lv_obj_align(clock, LV_ALIGN_LEFT_MID, 0, 0);
  // 날짜는 보조 정보라 시계(Silkscreen 28)보다 작게 둔다. Silkscreen 글자 높이는 0.62 em, Jua 한글은 약 0.8 em.
  // 두 글자의 바닥선을 맞춘다 (두 폰트 모두 기준선이 상자 아래에서 같은 비율 근처라 아래 정렬로 충분하다)
  obj = uiCreateLabel(bar, "10월 10일 (금)", uiFont(16), UI_COLOR_INK_DIM);
  lv_obj_align_to(obj, clock, LV_ALIGN_OUT_RIGHT_BOTTOM, UI_SPACE_MD, 0);
  obj = uiCreateLabel(bar, "미응답 2건 · 최장 12분", uiFont(18), UI_COLOR_INK);
  lv_obj_align(obj, LV_ALIGN_CENTER, 40, 0);
  obj = uiCreateLabel(bar, "SLACK · USB", uiFontPixel(14), UI_COLOR_INK_DIM);
  lv_obj_align(obj, LV_ALIGN_RIGHT_MID, 0, 0);

  // 왼쪽 : 캐릭터 자리
  left = uiCreateBox(scr);
  lv_obj_set_size(left, LEFT_W, 480 - BAR_H);
  lv_obj_set_pos(left, 0, BAR_H);
  lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_width(left, UI_BORDER, 0);
  lv_obj_set_style_border_color(left, lv_color_hex(UI_COLOR_INK), 0);

  obj = petCreate(left);
  lv_obj_align(obj, LV_ALIGN_CENTER, 0, -50);

  lbl_msg = uiCreateLabel(left, pet_msg[petGetState()], uiFont(26), UI_COLOR_INK);
  lv_obj_align(lbl_msg, LV_ALIGN_CENTER, 0, 80);
  obj = uiCreateLabel(left, "배부름", uiFont(15), UI_COLOR_INK_DIM);
  lv_obj_align(obj, LV_ALIGN_CENTER, 0, 120);

  // 오른쪽 : 글꼴 확인
  right = uiCreateBox(scr);
  lv_obj_set_size(right, 800 - LEFT_W, 480 - BAR_H);
  lv_obj_set_pos(right, LEFT_W, BAR_H);
  lv_obj_set_style_pad_all(right, UI_SPACE_XL, 0);
  lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(right, UI_SPACE_MD, 0);

  uiCreateLabel(right, "밥 주세요 (답장 대기)", uiFont(20), UI_COLOR_INK);
  uiCreateLabel(right, "김민수 · DM", uiFont(18), UI_COLOR_INK);
  uiCreateLabel(right, "샘플 보드 펌웨어 오늘 중으로 올려줄 수 있어요?", uiFont(16), UI_COLOR_INK_DIM);
  uiCreateLabel(right, "12m  38m  7  6m", uiFontPixel(26), UI_COLOR_WARN);
  uiCreateLabel(right, "답장한 요청 · 평균 응답 시간", uiFont(15), UI_COLOR_INK_DIM);
  uiCreateLabel(right, "카드를 누르면 맥의 슬랙에서 해당 대화가 열려요", uiFont(15), UI_COLOR_INK_DIM);

  return true;
}

static bool pingpetInit(void)
{
  cliAdd("pet", cliPet);
  return true;
}

static void pingpetUpdate(void)
{
  int st = req_state;

  if (st >= 0)
  {
    req_state = -1;
    petSetState((pet_state_t)st);
    lv_label_set_text(lbl_msg, pet_msg[st]);
    lv_obj_align(lbl_msg, LV_ALIGN_CENTER, 0, 80);
  }
}

void cliPet(cli_args_t *args)
{
  if (args->argc == 1)
  {
    for (int i=0; i<PET_STATE_MAX; i++)
    {
      if (args->isStr(0, petGetStateName(i)))
      {
        req_state = i;
        cliPrintf("pet : %s\n", petGetStateName(i));
        return;
      }
    }
  }
  cliPrintf("pet idle:hungry:cry:eat   (now %s)\n", petGetStateName(petGetState()));
}

APP_DEF(pingpet)
{
  .name   = "pingpet",
  .order  = 0,
  .icon   = LV_SYMBOL_BELL,
  .init   = pingpetInit,
  .enter  = pingpetEnter,
  .update = pingpetUpdate,
};

#endif
