#include "app.h"
#include "ui_theme.h"
#include "pet.h"
#include "inbox.h"
#include "lvgl/lv_port_indev.h"
#include "launcher.h"


#ifdef _USE_HW_LVGL

/* Ping Pet — 답장을 기다리는 요청(핑)을 먹고 사는 책상 위 펫. 캐릭터 이름은 답답이.
 *
 * 화면 (800x480, 목업 기준)
 *   상단 바 48 : 시계 · 날짜 · 요일 | 미응답 요약 | 연결 상태
 *   왼쪽 340   : (냠냠 배지) 캐릭터, 대사, 배부름 10 칸
 *   오른쪽     : 여유 = 오늘 통계 · 최근 처리 / 배고픔 · 울음 = 답장 대기 카드 / 냠냠 = 완료 카드 + 남은 요청
 *   울음       : 화면 테두리와 상단 바가 빨강
 *
 * 데이터는 inbox 모듈 스냅숏만 읽는다 (맥 중계와 데모가 같은 경로).
 * 목록 · 상태가 바뀔 때만 오른쪽 패널을 다시 만들고, 시계 · 경과 시간 · 배부름은 그 자리에서 바꾼다.
 * 카드를 누르는 동안 패널을 다시 만들지 않아야 1 초 길게 누르기가 끊기지 않는다.
 */


#define BAR_H             48
#define LEFT_W            340
#define CARD_MAX          INBOX_ITEM_MAX     // 보드가 받는 만큼 모두. 넘치면 스크롤한다
#define SCROLL_HOME_MS    30000              // 30 초 손대지 않으면 맨 위(가장 오래된 요청)로
#define VIEW_PERIOD_MS    200
#define LEVEL_WIP         3                  // 카드 스타일 : 처리 중 (경과 시간과 상관없이 흐리게)
#define CONFIRM_MS        15000              // 확인 창을 그대로 두면 15 초 뒤 취소

#define PIXEL_SIZE_CLOCK  28

#define DIM_AFTER_MS      (10 * 60 * 1000)   // 기다리는 미응답이 없고 마지막 일 뒤로 10 분이 지나면


typedef struct
{
  lv_obj_t *obj;
  lv_obj_t *avatar;
  lv_obj_t *lbl_avatar;
  lv_obj_t *lbl_title;
  lv_obj_t *lbl_text;
  lv_obj_t *lbl_age;
  char      id[48];
  uint32_t  ts;
  int       level;          // 0 기본, 1 10 분 넘음, 2 30 분 넘음, LEVEL_WIP 처리 중
  bool      wip;
} card_t;


static void pingpetBuildRight(void);
static void pingpetRefresh(bool force);

static inbox_view_t view;                 // 큰 구조체 (약 5 KB) — 스택에 두지 않는다
static uint32_t     shown_version = 0xFFFFFFFF;
static int          shown_state   = -1;
static uint32_t     view_ms = 0;

static lv_obj_t *scr;
static lv_obj_t *bar;
static lv_obj_t *lbl_clock, *lbl_date, *lbl_summary, *lbl_link;
static lv_obj_t *left;
static lv_obj_t *badge, *lbl_badge;
static lv_obj_t *lbl_msg;
static lv_obj_t *gauge_cell[INBOX_FULL_MAX];
static lv_obj_t *right;

static card_t    cards[CARD_MAX];
static int       card_cnt = 0;

static lv_obj_t *card_list = NULL;        // 스크롤하는 카드 목록 (없으면 NULL)
static lv_obj_t *more_row  = NULL;        // "▼ N건 더" (아래에 가려진 카드)
static lv_obj_t *lbl_more  = NULL;
static int32_t   saved_scroll = 0;        // 패널을 다시 만들 때 스크롤 위치를 되돌린다
static uint32_t  scroll_ms = 0;

static lv_obj_t *confirm = NULL;          // 1 초 누르기 창 : 처리 중 / 치우기 / 취소 (없으면 NULL)
static char      confirm_id[48];
static bool      confirm_wip = false;     // 그 카드가 이미 처리 중이다 (버튼이 "처리 중 해제")
static uint32_t  confirm_ms = 0;

static uint32_t  active_ms  = 0;          // 마지막으로 만졌거나 새 요청이 온 때
static uint32_t  last_sig = 0;           // 목록 · 처리 중 · 상태 요약 (바뀌면 "일이 있었다")
static bool      is_dimmed  = false;

static const char *pet_msg[] =
{
  "배불러요!",
  "배고파요…",
  "엉엉… 너무 배고파요",
  "냠냠! 맛있어요",
  "하나씩 처리하는 중",
};
static const char *weekday_str[7] = {"일", "월", "화", "수", "목", "금", "토"};




//-- 시간 · 글자 도우미
//
static void localTime(uint32_t unix_sec, int32_t tz, uint8_t *p_hour, uint8_t *p_min)
{
  uint16_t y;
  uint8_t  mo, d, s;

  utilCivilFromEpoch(unix_sec + tz, &y, &mo, &d, p_hour, p_min, &s);
}

static void ageText(char *buf, size_t size, uint32_t sec)
{
  uint32_t min = sec / 60;

  if (min < 60)
    snprintf(buf, size, "%lum", (unsigned long)min);
  else
    snprintf(buf, size, "%luh", (unsigned long)(min / 60));
}

static void itemTitle(char *buf, size_t size, uint8_t kind, const char *from, const char *where)
{
  if (kind == INBOX_KIND_MENTION)
    snprintf(buf, size, "%s · %s", where, from);
  else
    snprintf(buf, size, "%s · %s", from, where);
}

/* 이름의 첫 글자 (UTF-8 한 글자) */
static void firstChar(char *buf, size_t size, const char *str)
{
  int len = 1;
  uint8_t c = (uint8_t)str[0];

  if      ((c & 0xE0) == 0xC0) len = 2;
  else if ((c & 0xF0) == 0xE0) len = 3;
  else if ((c & 0xF8) == 0xF0) len = 4;
  if (c == 0) len = 0;
  if (len >= (int)size) len = size - 1;

  memcpy(buf, str, len);
  buf[len] = 0;
}

static lv_obj_t *labelDots(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
  lv_obj_t *lbl = uiCreateLabel(parent, text, font, color);

  // 한 줄, 넘치면 … (폭은 부모를 채우고 높이는 한 줄로 고정해야 DOTS 가 동작한다)
  lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(lbl, LV_PCT(100));
  lv_obj_set_height(lbl, lv_font_get_line_height(font));
  return lbl;
}

static lv_obj_t *column(lv_obj_t *parent, int32_t gap)
{
  lv_obj_t *obj = uiCreateBox(parent);

  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(obj, gap, 0);
  return obj;
}

static lv_obj_t *row(lv_obj_t *parent, int32_t gap)
{
  lv_obj_t *obj = uiCreateBox(parent);

  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(obj, gap, 0);
  return obj;
}


//-- 카드
//
//-- 답장 없이 치우기 확인 창
//
static void confirmClose(void)
{
  if (confirm != NULL)
  {
    lv_obj_delete_async(confirm);          // 버튼 이벤트 안에서 불리므로 나중에 지운다
    confirm = NULL;
  }
}

typedef enum
{
  CONFIRM_CANCEL = 0,
  CONFIRM_ACK,              // 답장 없이 치우기
  CONFIRM_WIP,              // 처리 중 / 해제
} confirm_act_t;

static void onConfirm(lv_event_t *e)
{
  confirm_act_t act = (confirm_act_t)(intptr_t)lv_event_get_user_data(e);

  if (confirm == NULL)
    return;
  if (act == CONFIRM_ACK)
    inboxRequestAck(confirm_id);
  else if (act == CONFIRM_WIP)
    inboxRequestWip(confirm_id, !confirm_wip);
  confirmClose();
}

static lv_obj_t *confirmButton(lv_obj_t *parent, const char *text, bool primary, confirm_act_t act)
{
  lv_obj_t *btn = uiCreateBox(parent);
  lv_obj_t *lbl;

  lv_obj_set_size(btn, 150, UI_TOUCH_MIN);
  lv_obj_set_clickable(btn, true);
  lv_obj_set_style_border_width(btn, UI_BORDER, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(UI_COLOR_INK), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? UI_COLOR_INK : UI_COLOR_CARD), 0);
  lv_obj_set_style_translate_y(btn, 2, LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn, onConfirm, LV_EVENT_CLICKED, (void *)(intptr_t)act);

  lbl = uiCreateLabel(btn, text, uiFont(20), primary ? UI_COLOR_BG : UI_COLOR_INK);
  lv_obj_center(lbl);
  return btn;
}

/* 1 초 누르기 : 처리 중으로 둘지, 답장 없이 치울지 고른다 (치우기는 되돌릴 수 없으니 바로 하지 않는다).
   손가락은 아직 카드 위라 손을 떼도 버튼이 눌리지 않는다 */
static void confirmOpen(const card_t *p_card)
{
  lv_obj_t *box, *obj, *btns;
  char      buf[160];

  confirmClose();
  strncpy(confirm_id, p_card->id, sizeof(confirm_id) - 1);
  confirm_id[sizeof(confirm_id) - 1] = 0;
  confirm_ms  = millis();
  confirm_wip = p_card->wip;

  // 화면 전체를 덮는 반투명 막 : 뒤의 카드가 눌리지 않게 하고, 막을 누르면 취소
  confirm = uiCreateBox(scr);
  lv_obj_set_floating(confirm, true);
  lv_obj_set_size(confirm, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(confirm, 0, 0);
  lv_obj_set_clickable(confirm, true);
  lv_obj_set_style_bg_color(confirm, lv_color_hex(UI_COLOR_INK), 0);
  lv_obj_set_style_bg_opa(confirm, LV_OPA_50, 0);
  lv_obj_add_event_cb(confirm, onConfirm, LV_EVENT_CLICKED, (void *)(intptr_t)CONFIRM_CANCEL);

  box = uiCreateBox(confirm);
  lv_obj_set_size(box, 560, LV_SIZE_CONTENT);
  lv_obj_center(box);
  lv_obj_set_clickable(box, true);              // 창 안을 눌러도 취소되지 않게
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(box, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_border_width(box, UI_BORDER, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COLOR_INK), 0);
  lv_obj_set_style_pad_all(box, UI_SPACE_XL, 0);
  lv_obj_set_style_pad_row(box, UI_SPACE_MD, 0);
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  uiCreateLabel(box, "이 요청을 어떻게 할까요?", uiFont(24), UI_COLOR_INK);

  obj = uiCreateLabel(box, lv_label_get_text(p_card->lbl_title), uiFont(18), UI_COLOR_INK);
  lv_obj_set_width(obj, LV_PCT(100));
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_DOTS);

  snprintf(buf, sizeof(buf), "%s", lv_label_get_text(p_card->lbl_text));
  obj = uiCreateLabel(box, buf, uiFont(16), UI_COLOR_INK_DIM);
  lv_obj_set_width(obj, LV_PCT(100));
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_DOTS);

  obj = uiCreateLabel(box, confirm_wip ? "처리 중 해제 : 다시 답장 대기로\n치우기 : 답장 없이 목록에서 빼요 (슬랙에는 안 보내요)"
                                       : "처리 중 : 보고 있어요. 답답이가 울지 않아요\n치우기 : 답장 없이 목록에서 빼요 (슬랙에는 안 보내요)",
                      uiFont(15), UI_COLOR_INK_DIM);
  lv_obj_set_width(obj, LV_PCT(100));
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);

  btns = row(box, UI_SPACE_LG);
  lv_obj_set_size(btns, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(btns, UI_SPACE_SM, 0);
  confirmButton(btns, "취소", false, CONFIRM_CANCEL);
  confirmButton(btns, confirm_wip ? "처리 중 해제" : "처리 중", false, CONFIRM_WIP);
  confirmButton(btns, "치우기", true, CONFIRM_ACK);
}

/* 열린 채 오래 두었거나, 그 사이 답장해서 목록에서 빠졌으면 닫는다 */
static void confirmUpdate(void)
{
  bool found = false;

  if (confirm == NULL)
    return;

  for (int i=0; i<view.count; i++)
  {
    if (strcmp(view.item[i].id, confirm_id) == 0)
    {
      found = true;
      break;
    }
  }
  if (found == false || millis() - confirm_ms >= CONFIRM_MS)
    confirmClose();
}

static void onCard(lv_event_t *e)
{
  card_t         *p_card = (card_t *)lv_event_get_user_data(e);
  lv_event_code_t code   = lv_event_get_code(e);
  lv_indev_t     *indev  = lv_indev_active();

  // 스크롤하다 손을 뗀 것이면 누른 것으로 보지 않는다 (stm32h7-gfx 13 편 "스크롤했는데 선택된다").
  // 길게 누르기는 "답장 없이 처리" 라 잘못 눌리면 곤란하다
  if (indev != NULL && lv_indev_get_scroll_obj(indev) != NULL)
    return;

  // TODO: 누른 결과를 화면 아래에 2 초 알림으로 보여 준다. 지금은 맥 화면을 보지 않으면 눌렸는지 모른다
  //   연결됨 "맥의 Slack 에서 열었어요" / 끊김 "맥과 연결되지 않았어요" / 데모 "데모: 맥에서 대화가 열려요"
  //   확인 창에서 치우기 "답장 없이 치웠어요"
  if (code == LV_EVENT_SHORT_CLICKED)
    inboxRequestOpen(p_card->id);
  else if (code == LV_EVENT_LONG_PRESSED)
    confirmOpen(p_card);
}

static void cardStyle(card_t *p_card, int level)
{
  bool     old    = (level == 2);
  uint32_t border = old ? UI_COLOR_ERROR : UI_COLOR_INK;

  if (level == LEVEL_WIP)       // 처리 중 : 흐린 테두리 · 아바타, 경과 시간도 흐리게
  {
    p_card->level = level;
    lv_obj_set_style_border_color(p_card->obj, lv_color_hex(UI_COLOR_LINE_DASH), 0);
    lv_obj_set_style_bg_color(p_card->obj, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_color(p_card->avatar, lv_color_hex(UI_COLOR_INK_DIM), 0);
    lv_obj_set_style_text_color(p_card->lbl_avatar, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_text_color(p_card->lbl_text, lv_color_hex(UI_COLOR_INK_DIM), 0);
    lv_obj_set_style_text_color(p_card->lbl_age, lv_color_hex(UI_COLOR_INK_DIM), 0);
    return;
  }

  p_card->level = level;
  lv_obj_set_style_border_color(p_card->obj, lv_color_hex(border), 0);
  lv_obj_set_style_bg_color(p_card->obj, lv_color_hex(old ? UI_COLOR_ERROR_BG : UI_COLOR_CARD), 0);
  lv_obj_set_style_bg_color(p_card->avatar, lv_color_hex(old ? UI_COLOR_ERROR : UI_COLOR_INK), 0);
  lv_obj_set_style_text_color(p_card->lbl_avatar, lv_color_hex(old ? UI_COLOR_TEXT_ON_ERROR : UI_COLOR_BG), 0);
  lv_obj_set_style_text_color(p_card->lbl_text, lv_color_hex(old ? UI_COLOR_ERROR_TEXT : UI_COLOR_INK_DIM), 0);
  lv_obj_set_style_text_color(p_card->lbl_age,
                              lv_color_hex(level == 2 ? UI_COLOR_ERROR : level == 1 ? UI_COLOR_WARN : UI_COLOR_INK), 0);
}

static int ageLevel(uint32_t sec)
{
  if (sec > INBOX_CRY_SEC)   return 2;
  if (sec >= INBOX_WARN_SEC) return 1;
  return 0;
}

static void cardCreate(lv_obj_t *parent, const inbox_item_t *p_item, bool compact)
{
  card_t   *p_card = &cards[card_cnt++];
  lv_obj_t *col;
  char      buf[128];
  int32_t   avatar = compact ? 40 : 44;

  memset(p_card, 0, sizeof(card_t));
  strncpy(p_card->id, p_item->id, sizeof(p_card->id) - 1);
  p_card->ts  = p_item->ts;
  p_card->wip = (p_item->wip != 0);

  p_card->obj = row(parent, compact ? 10 : 12);
  lv_obj_set_width(p_card->obj, LV_PCT(100));
  lv_obj_set_height(p_card->obj, LV_SIZE_CONTENT);
  lv_obj_set_style_border_width(p_card->obj, UI_BORDER, 0);
  lv_obj_set_style_bg_opa(p_card->obj, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_ver(p_card->obj, compact ? 10 : 12, 0);
  lv_obj_set_style_pad_hor(p_card->obj, compact ? 12 : 14, 0);
  lv_obj_set_style_translate_y(p_card->obj, 2, LV_STATE_PRESSED);      // 누르면 살짝 들어간다
  lv_obj_set_clickable(p_card->obj, true);
  lv_obj_add_event_cb(p_card->obj, onCard, LV_EVENT_SHORT_CLICKED, p_card);
  lv_obj_add_event_cb(p_card->obj, onCard, LV_EVENT_LONG_PRESSED, p_card);

  p_card->avatar = uiCreateBox(p_card->obj);
  lv_obj_set_size(p_card->avatar, avatar, avatar);
  lv_obj_set_style_bg_opa(p_card->avatar, LV_OPA_COVER, 0);
  firstChar(buf, sizeof(buf), p_item->from);
  p_card->lbl_avatar = uiCreateLabel(p_card->avatar, buf, uiFont(compact ? 18 : 20), UI_COLOR_BG);
  lv_obj_center(p_card->lbl_avatar);

  col = column(p_card->obj, 2);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  if (p_card->wip)
  {
    char title[112];
    itemTitle(title, sizeof(title), p_item->kind, p_item->from, p_item->where);
    snprintf(buf, sizeof(buf), "처리 중 · %s", title);
  }
  else
  {
    itemTitle(buf, sizeof(buf), p_item->kind, p_item->from, p_item->where);
  }
  p_card->lbl_title = labelDots(col, buf, uiFont(compact ? 17 : 18), UI_COLOR_INK);
  p_card->lbl_text  = labelDots(col, p_item->text, uiFont(compact ? 15 : 16), UI_COLOR_INK_DIM);

  p_card->lbl_age = uiCreateLabel(p_card->obj, "", uiFontPixel(compact ? 24 : 26), UI_COLOR_INK);

  cardStyle(p_card, -1);
}

static void cardsRefresh(void)
{
  char buf[16];

  for (int i=0; i<card_cnt; i++)
  {
    uint32_t sec = (view.now > cards[i].ts) ? view.now - cards[i].ts : 0;
    int      lv  = cards[i].wip ? LEVEL_WIP : ageLevel(sec);

    ageText(buf, sizeof(buf), sec);
    lv_label_set_text(cards[i].lbl_age, buf);
    if (lv != cards[i].level)
      cardStyle(&cards[i], lv);
  }
}

/* 아래에 가려진 카드 수 → "▼ N건 더". 끝까지 내리면 숨긴다 */
static void moreRefresh(void)
{
  lv_area_t view_area, a;
  int       hidden = 0;

  if (card_list == NULL || more_row == NULL)
    return;

  lv_obj_get_coords(card_list, &view_area);
  for (int i=0; i<card_cnt; i++)
  {
    lv_obj_get_coords(cards[i].obj, &a);
    if (a.y2 > view_area.y2 + 2)       // 아래쪽이 조금이라도 잘린 카드
      hidden++;
  }
  lv_obj_set_hidden(more_row, hidden == 0);
  if (hidden > 0)
    lv_label_set_text_fmt(lbl_more, "%d건 더 (아래로 밀어 보기)", hidden);
}

static void onListScroll(lv_event_t *e)
{
  LV_UNUSED(e);
  scroll_ms = millis();
  moreRefresh();
}

/* 카드 목록 : 모두 넣고 넘치면 위아래로 스크롤한다 */
static lv_obj_t *cardList(lv_obj_t *parent, bool compact)
{
  card_list = column(parent, compact ? 8 : 12);
  lv_obj_set_width(card_list, LV_PCT(100));
  lv_obj_set_flex_grow(card_list, 1);
  lv_obj_set_scrollable(card_list, true);
  lv_obj_set_scroll_dir(card_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(card_list, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_bg_color(card_list, lv_color_hex(UI_COLOR_INK), LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(card_list, 6, 0);           // 스크롤 막대 자리
  lv_obj_add_event_cb(card_list, onListScroll, LV_EVENT_SCROLL, NULL);

  for (int i=0; i<view.count && card_cnt<CARD_MAX; i++)
  {
    cardCreate(card_list, &view.item[i], compact);
  }

  // ▼ N건 더 (목록 바로 아래)
  more_row = row(parent, 6);
  lv_obj_set_size(more_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_t *sym = uiCreateLabel(more_row, LV_SYMBOL_DOWN, &lv_font_montserrat_20, UI_COLOR_INK_DIM);
  LV_UNUSED(sym);
  lbl_more = uiCreateLabel(more_row, "", uiFont(15), UI_COLOR_INK_DIM);
  lv_obj_set_hidden(more_row, true);
  return card_list;
}


//-- 오른쪽 패널 (상태마다)
//
static lv_obj_t *dashLine(lv_obj_t *parent)
{
  static const lv_point_precise_t pts[2] = {{0, 0}, {420, 0}};
  lv_obj_t *line = lv_line_create(parent);

  lv_line_set_points(line, pts, 2);
  lv_obj_set_width(line, LV_PCT(100));
  lv_obj_set_style_line_width(line, 2, 0);
  lv_obj_set_style_line_color(line, lv_color_hex(UI_COLOR_LINE_DASH), 0);
  lv_obj_set_style_line_dash_width(line, 6, 0);
  lv_obj_set_style_line_dash_gap(line, 4, 0);
  return line;
}

static void statBox(lv_obj_t *parent, const char *value, const char *label)
{
  lv_obj_t *box = column(parent, 4);

  lv_obj_set_flex_grow(box, 1);
  lv_obj_set_height(box, LV_SIZE_CONTENT);
  lv_obj_set_style_border_width(box, UI_BORDER, 0);
  lv_obj_set_style_border_color(box, lv_color_hex(UI_COLOR_INK), 0);
  lv_obj_set_style_pad_all(box, 12, 0);
  uiCreateLabel(box, value, uiFontPixel(34), UI_COLOR_INK);
  uiCreateLabel(box, label, uiFont(15), UI_COLOR_INK_DIM);
}

static void buildIdle(void)
{
  lv_obj_t *obj;
  char      buf[128], buf2[32];

  uiCreateLabel(right, "오늘 받은 밥", uiFont(20), UI_COLOR_INK);

  obj = row(right, 12);
  lv_obj_set_width(obj, LV_PCT(100));
  lv_obj_set_height(obj, LV_SIZE_CONTENT);
  snprintf(buf, sizeof(buf), "%d", view.stats.handled);
  statBox(obj, buf, "답장한 요청");
  if (view.stats.handled > 0)
    snprintf(buf, sizeof(buf), "%dm", view.stats.avg_wait_min);
  else
    snprintf(buf, sizeof(buf), "-");
  statBox(obj, buf, "평균 응답 시간");

  obj = uiCreateLabel(right, "최근 처리", uiFont(17), UI_COLOR_INK_DIM);
  lv_obj_set_style_margin_top(obj, 4, 0);

  if (view.stats.recent_cnt == 0)
  {
    uiCreateLabel(right, "아직 처리한 요청이 없어요", uiFont(17), UI_COLOR_INK_DIM);
    return;
  }

  lv_obj_t *list = column(right, 0);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_height(list, LV_SIZE_CONTENT);

  for (int i=0; i<view.stats.recent_cnt; i++)
  {
    const inbox_recent_t *p = &view.stats.recent[i];
    uint8_t hour, min;

    if (i > 0) dashLine(list);

    obj = uiCreateBox(list);
    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_height(obj, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(obj, 9, 0);

    itemTitle(buf, sizeof(buf), p->kind, p->from, p->where);
    lv_obj_t *l = uiCreateLabel(obj, buf, uiFont(17), UI_COLOR_INK);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);

    localTime(p->at, view.tz, &hour, &min);
    snprintf(buf2, sizeof(buf2), "%02d:%02d 완료", hour, min);
    l = uiCreateLabel(obj, buf2, uiFont(17), UI_COLOR_INK_DIM);
    lv_obj_align(l, LV_ALIGN_RIGHT_MID, 0, 0);
  }
}

static void buildWaiting(bool cry)
{
  bool compact = cry || view.count > 2;

  if (cry == false)
    uiCreateLabel(right, view.active > 0 ? "밥 주세요 (답장 대기)" : "처리 중인 요청", uiFont(20), UI_COLOR_INK);

  cardList(right, compact);

  if (compact == false)
    uiCreateLabel(right, "카드를 누르면 맥의 슬랙에서 해당 대화가 열려요", uiFont(15), UI_COLOR_INK_DIM);
}

static void checkIcon(lv_obj_t *parent)
{
  // 24 x 24 상자 안의 체크 표시 (목업 M4 12 l5 5 L20 6) 를 28 px 로
  static const lv_point_precise_t pts[3] = {{5, 14}, {11, 20}, {23, 7}};
  lv_obj_t *line = lv_line_create(parent);

  lv_line_set_points(line, pts, 3);
  lv_obj_set_size(line, 28, 28);
  lv_obj_set_style_line_width(line, 4, 0);
  lv_obj_set_style_line_color(line, lv_color_hex(UI_COLOR_OK), 0);
}

static void buildEat(void)
{
  lv_obj_t *banner, *col;
  char      buf[160];
  int       gain = view.eat_full_to - view.eat_full_from;

  banner = row(right, 12);
  lv_obj_set_width(banner, LV_PCT(100));
  lv_obj_set_height(banner, LV_SIZE_CONTENT);
  lv_obj_set_style_border_width(banner, UI_BORDER, 0);
  lv_obj_set_style_border_color(banner, lv_color_hex(UI_COLOR_OK), 0);
  lv_obj_set_style_bg_color(banner, lv_color_hex(UI_COLOR_OK_BG), 0);
  lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_ver(banner, 12, 0);
  lv_obj_set_style_pad_hor(banner, 14, 0);
  checkIcon(banner);

  col = column(banner, 2);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  if (view.eat_item.kind == INBOX_KIND_DM)
    snprintf(buf, sizeof(buf), "%s님 DM에 답장했어요", view.eat_item.from);
  else if (view.eat_item.kind == INBOX_KIND_MPIM)
    snprintf(buf, sizeof(buf), "%s님 그룹 DM에 답장했어요", view.eat_item.from);
  else
    snprintf(buf, sizeof(buf), "%s %s님께 답장했어요", view.eat_item.where, view.eat_item.from);
  labelDots(col, buf, uiFont(18), UI_COLOR_OK_TEXT);
  snprintf(buf, sizeof(buf), "%lu분 기다림 · 밥 +%d", (unsigned long)(view.eat_waited / 60), gain);
  uiCreateLabel(col, buf, uiFont(15), UI_COLOR_OK);

  col = uiCreateLabel(right, view.count > 0 ? "남은 요청" : "남은 요청이 없어요", uiFont(17), UI_COLOR_INK_DIM);
  lv_obj_set_style_margin_top(col, 4, 0);

  if (view.count > 0)
    cardList(right, view.count > 1);
}

void pingpetBuildRight(void)
{
  int32_t scroll = 0;

  // 같은 상태에서 목록만 바뀌었으면 보던 위치를 지킨다 (새 요청이 와도 맨 위로 튀지 않게)
  if (card_list != NULL && (int)view.state == shown_state)
    scroll = lv_obj_get_scroll_y(card_list);

  lv_obj_clean(right);
  card_cnt  = 0;
  card_list = NULL;
  more_row  = NULL;
  lbl_more  = NULL;

  switch (view.state)
  {
    case INBOX_STATE_IDLE:   buildIdle();          break;
    case INBOX_STATE_HUNGRY: buildWaiting(false);  break;
    case INBOX_STATE_CRY:    buildWaiting(true);   break;
    case INBOX_STATE_EAT:    buildEat();           break;
    case INBOX_STATE_WORK:   buildWaiting(false);  break;
  }
  cardsRefresh();

  if (card_list != NULL)
  {
    lv_obj_update_layout(right);
    if (scroll > 0)
      lv_obj_scroll_to_y(card_list, scroll, LV_ANIM_OFF);
    moreRefresh();
  }
  saved_scroll = scroll;
}


//-- 상태가 바뀔 때 : 테두리, 상단 바, 캐릭터, 대사
//
static void applyState(void)
{
  bool     cry      = (view.state == INBOX_STATE_CRY);
  uint32_t bar_text = cry ? UI_COLOR_TEXT_ON_ERROR : UI_COLOR_INK;

  lv_obj_set_style_border_width(scr, cry ? 6 : 0, 0);
  lv_obj_set_style_border_color(scr, lv_color_hex(UI_COLOR_ERROR), 0);

  lv_obj_set_style_bg_color(bar, lv_color_hex(UI_COLOR_ERROR), 0);
  lv_obj_set_style_bg_opa(bar, cry ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bar, cry ? 0 : UI_BORDER, 0);
  lv_obj_set_style_text_color(lbl_clock, lv_color_hex(bar_text), 0);
  lv_obj_set_style_text_color(lbl_date, lv_color_hex(cry ? UI_COLOR_TEXT_ON_ERROR : UI_COLOR_INK_DIM), 0);
  lv_obj_set_style_text_color(lbl_summary, lv_color_hex(bar_text), 0);

  // 작업 중은 업데이트 때와 같은 얼굴 (웃는 눈 + 오물거리는 입 : 열심히 먹는 중)
  petSetState(view.state == INBOX_STATE_WORK ? PET_STATE_UPDATE : (pet_state_t)view.state);
  lv_label_set_text(lbl_msg, pet_msg[view.state]);

  lv_obj_set_hidden(badge, view.state != INBOX_STATE_EAT);
  if (view.state == INBOX_STATE_EAT)
    lv_label_set_text_fmt(lbl_badge, "+%d YUM", view.eat_full_to - view.eat_full_from);
}


//-- 매번 (0.2 초) : 시계, 요약, 연결, 배부름, 카드 경과 시간
//
static void refreshBar(void)
{
  bool cry = (view.state == INBOX_STATE_CRY);
  char buf[112];
  int  len;

  if (view.time_valid)
  {
    uint16_t y;
    uint8_t  mo, d, h, mi, s;
    uint32_t local = view.now + view.tz;

    utilCivilFromEpoch(local, &y, &mo, &d, &h, &mi, &s);
    lv_label_set_text_fmt(lbl_clock, "%02d:%02d", h, mi);
    // 글자를 키우면 가운데 요약과 겹치지 않게 짧게 쓴다
    if (uiThemeGetFontLevel() == 0)
      lv_label_set_text_fmt(lbl_date, "%d월 %d일 (%s)", mo, d, weekday_str[(local / 86400 + 4) % 7]);
    else
      lv_label_set_text_fmt(lbl_date, "%d/%d (%s)", mo, d, weekday_str[(local / 86400 + 4) % 7]);
  }
  else
  {
    lv_label_set_text(lbl_clock, "--:--");
    lv_label_set_text(lbl_date, "");
  }
  lv_obj_align_to(lbl_date, lbl_clock, LV_ALIGN_OUT_RIGHT_BOTTOM, UI_SPACE_MD, 0);

  if (view.count == 0)
  {
    snprintf(buf, sizeof(buf), "미응답 0건");
    lv_obj_set_style_text_color(lbl_summary, lv_color_hex(UI_COLOR_INK_DIM), 0);
  }
  else if (view.active == 0)
  {
    snprintf(buf, sizeof(buf), "처리 중 %d건", view.wip);
    lv_obj_set_style_text_color(lbl_summary, lv_color_hex(UI_COLOR_INK), 0);
  }
  else
  {
    uint32_t min = view.oldest_sec / 60;

    if (min < 60)
      len = snprintf(buf, sizeof(buf), "미응답 %d건 · 최장 %lu분", view.active, (unsigned long)min);
    else
      len = snprintf(buf, sizeof(buf), "미응답 %d건 · 최장 %lu시간", view.active, (unsigned long)(min / 60));
    if (view.wip > 0 && len > 0 && len < (int)sizeof(buf))
      snprintf(&buf[len], sizeof(buf) - len, " · 처리 중 %d", view.wip);
    lv_obj_set_style_text_color(lbl_summary, lv_color_hex(cry ? UI_COLOR_TEXT_ON_ERROR : UI_COLOR_INK), 0);
  }
  lv_label_set_text(lbl_summary, buf);

  switch (view.link)
  {
    case INBOX_LINK_OK:
      lv_label_set_text(lbl_link, "SLACK · USB");
      lv_obj_set_style_text_color(lbl_link, lv_color_hex(cry ? UI_COLOR_TEXT_ON_ERROR : UI_COLOR_INK_DIM), 0);
      break;
    case INBOX_LINK_DEMO:
      lv_label_set_text(lbl_link, "DEMO");
      lv_obj_set_style_text_color(lbl_link, lv_color_hex(cry ? UI_COLOR_TEXT_ON_ERROR : UI_COLOR_WARN), 0);
      break;
    case INBOX_LINK_LOST:
      lv_label_set_text(lbl_link, "OFFLINE");
      lv_obj_set_style_text_color(lbl_link, lv_color_hex(cry ? UI_COLOR_TEXT_ON_ERROR : UI_COLOR_ERROR), 0);
      break;
    default:
      lv_label_set_text(lbl_link, "NO LINK");
      lv_obj_set_style_text_color(lbl_link, lv_color_hex(cry ? UI_COLOR_TEXT_ON_ERROR : UI_COLOR_INK_DIM), 0);
      break;
  }
}

static void refreshGauge(void)
{
  bool    cry  = (view.state == INBOX_STATE_CRY);
  bool    eat  = (view.state == INBOX_STATE_EAT);
  uint8_t full = eat ? view.eat_full_to : view.fullness;
  uint8_t from = eat ? view.eat_full_from : full;

  for (int i=0; i<INBOX_FULL_MAX; i++)
  {
    lv_obj_t *cell = gauge_cell[i];
    bool      on   = (i < full);
    uint32_t  color;

    if (eat && i >= from && i < full)  color = UI_COLOR_GAUGE_NEW;   // 방금 먹어서 찬 칸
    else if (cry)                      color = UI_COLOR_ERROR;
    else                               color = UI_COLOR_INK;

    lv_obj_set_style_bg_color(cell, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(cell, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cell, on ? 0 : 2, 0);
  }
}

void pingpetRefresh(bool force)
{
  bool changed;

  inboxGetView(&view);

  changed = force || (view.version != shown_version) || ((int)view.state != shown_state);
  if (changed)
  {
    if ((int)view.state != shown_state)
      applyState();
    shown_version = view.version;
    pingpetBuildRight();                  // shown_state 를 보고 스크롤을 지킬지 정한다
    shown_state   = view.state;
  }

  refreshBar();
  refreshGauge();
  cardsRefresh();
  moreRefresh();
}


static void onBarLongPress(lv_event_t *e)
{
  LV_UNUSED(e);
  launcherRequestApp("settings");
}

/* 미응답 0 건으로 10 분 동안 손대지 않으면 어둡게, 새 요청이 오거나 만지면 원래 밝기로 */
/* 백라이트 : 기다리는 미응답(처리 중 제외)이 없고, 마지막 일 뒤로 10 분이 지나면 설정한 밝기(기본 30 %)로.
   일 = 터치, 새 요청, 답장 · 치우기, 처리 중 표시 · 해제, 상태가 바뀜 (시계 · 경과 시간은 아니다) */
static void backlightUpdate(void)
{
  bool     active = false;
  uint32_t sig    = (uint32_t)view.count | ((uint32_t)view.wip << 8) | ((uint32_t)view.state << 16);

  if (lv_display_get_inactive_time(NULL) < 1000)    active = true;   // 방금 만졌다
  if (sig != last_sig)                              active = true;   // 목록 · 상태가 바뀌었다
  last_sig = sig;

  if (active)
  {
    active_ms = millis();
    if (is_dimmed)
    {
      is_dimmed = false;
      lcdSetBackLight(uiThemeGetBrightness());
    }
  }
  else if (is_dimmed == false && uiThemeGetDim() > 0 && view.active == 0 && millis() - active_ms >= DIM_AFTER_MS)
  {
    is_dimmed = true;
    lcdSetBackLight(LV_MIN(uiThemeGetDim(), uiThemeGetBrightness()));   // 설정 앱에서 고른 밝기 (기본 30 %)
  }
}


//-- app
//
static bool pingpetEnter(lv_obj_t *p_scr)
{
  lv_obj_t *obj, *body, *gauge;

  scr     = p_scr;
  confirm = NULL;                         // 이전 화면과 함께 지워졌다
  uiApplyScreen(scr);
  lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);

  // 상단 바
  bar = uiCreateBox(scr);
  lv_obj_set_size(bar, LV_PCT(100), BAR_H);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(bar, UI_BORDER, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(UI_COLOR_INK), 0);
  lv_obj_set_style_pad_hor(bar, UI_SPACE_LG, 0);
  lv_obj_set_clickable(bar, true);
  lv_obj_add_event_cb(bar, onBarLongPress, LV_EVENT_LONG_PRESSED, NULL);    // 길게 누르면 설정

  lbl_clock = uiCreateLabel(bar, "--:--", uiFontPixel(PIXEL_SIZE_CLOCK), UI_COLOR_INK);
  lv_obj_align(lbl_clock, LV_ALIGN_LEFT_MID, 0, 0);
  // 날짜는 보조 정보라 시계보다 작게, 바닥선을 맞춘다 (Silkscreen 글자 높이 0.62 em, Jua 한글 약 0.8 em)
  lbl_date = uiCreateLabel(bar, "", uiFont(16), UI_COLOR_INK_DIM);
  lbl_summary = uiCreateLabel(bar, "", uiFont(18), UI_COLOR_INK);
  lv_obj_align(lbl_summary, LV_ALIGN_CENTER, 40, 0);
  lbl_link = uiCreateLabel(bar, "", uiFontPixel(14), UI_COLOR_INK_DIM);
  lv_obj_align(lbl_link, LV_ALIGN_RIGHT_MID, 0, 0);

  // 아래 (왼쪽 + 오른쪽)
  body = row(scr, 0);
  lv_obj_set_width(body, LV_PCT(100));
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  left = column(body, 14);
  lv_obj_set_size(left, LEFT_W, LV_PCT(100));
  lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_width(left, UI_BORDER, 0);
  lv_obj_set_style_border_color(left, lv_color_hex(UI_COLOR_INK), 0);

  badge = uiCreateBox(left);
  lv_obj_set_size(badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(badge, lv_color_hex(UI_COLOR_INK), 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_ver(badge, 4, 0);
  lv_obj_set_style_pad_hor(badge, 10, 0);
  lbl_badge = uiCreateLabel(badge, "", uiFontPixel(18), UI_COLOR_BG);

  petCreate(left);
  lbl_msg = uiCreateLabel(left, "", uiFont(26), UI_COLOR_INK);

  obj = column(left, 6);
  lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  uiCreateLabel(obj, "배부름", uiFont(15), UI_COLOR_INK_DIM);
  gauge = row(obj, 4);
  lv_obj_set_size(gauge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  for (int i=0; i<INBOX_FULL_MAX; i++)
  {
    gauge_cell[i] = uiCreateBox(gauge);
    lv_obj_set_size(gauge_cell[i], 18, 14);
    lv_obj_set_style_border_color(gauge_cell[i], lv_color_hex(UI_COLOR_INK), 0);
  }

  right = column(body, 12);
  lv_obj_set_flex_grow(right, 1);
  lv_obj_set_height(right, LV_PCT(100));
  lv_obj_set_style_pad_ver(right, UI_SPACE_LG, 0);
  lv_obj_set_style_pad_hor(right, UI_SPACE_XL, 0);

  shown_version = 0xFFFFFFFF;
  shown_state   = -1;
  active_ms     = millis();
  pingpetRefresh(true);
  return true;
}

static void pingpetUpdate(void)
{
  if (millis() - view_ms < VIEW_PERIOD_MS)
    return;
  view_ms = millis();

  backlightUpdate();

  // 30 초 동안 손대지 않으면 맨 위(가장 오래된 요청)로 돌아간다
  if (card_list != NULL && lv_obj_get_scroll_y(card_list) > 0 &&
      millis() - scroll_ms >= SCROLL_HOME_MS && lv_display_get_inactive_time(NULL) >= SCROLL_HOME_MS)
  {
    lv_obj_scroll_to_y(card_list, 0, LV_ANIM_ON);
  }

  // 화면을 누르고 있는 동안은 패널을 다시 만들지 않는다 (누르던 카드가 지워지면 길게 누르기가 끊긴다)
  lv_indev_t *indev = lv_port_indev_get_touch();
  if (indev != NULL && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED)
  {
    inboxGetView(&view);
    refreshBar();
    refreshGauge();
    cardsRefresh();
    return;
  }

  pingpetRefresh(false);
  confirmUpdate();
}

APP_DEF(pingpet)
{
  .name   = "pingpet",
  .order  = 0,
  .icon   = LV_SYMBOL_BELL,
  .enter  = pingpetEnter,
  .update = pingpetUpdate,
};

#endif
