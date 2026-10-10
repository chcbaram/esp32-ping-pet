#ifndef UI_THEME_H_
#define UI_THEME_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"

#ifdef _USE_HW_LVGL


/* 테마 — HG-T113-S3 ui_theme 의 형태를 따른다 (팔레트, 간격, uiFont*).
 *
 * 색은 목업(Slack 다마고치 UI 캔버스)의 LCD 연두 팔레트 하나뿐이다.
 * 폰트는 TTF 를 펌웨어에 넣고 Tiny TTF 로 크기마다 만든다.
 *   본문 : Jua (한글)
 *   숫자 : Silkscreen (픽셀 폰트) — 시계, 경과 시간, 통계
 */

typedef struct
{
  uint32_t bg;            /* 화면 바탕 (LCD 연두)           */
  uint32_t ink;           /* 글자, 선, 채운 칸               */
  uint32_t ink_dim;       /* 보조 설명                       */
  uint32_t line_dash;     /* 점선 구분선                     */
  uint32_t card;          /* 카드 바탕                       */
  uint32_t warn;          /* 10 ~ 30 분 경과 숫자            */
  uint32_t error;         /* 30 분 넘음, 울음 화면 테두리    */
  uint32_t error_bg;      /* 오래된 카드 바탕                */
  uint32_t error_text;    /* 오래된 카드 보조 글자           */
  uint32_t text_on_error; /* 빨강 위 글자                    */
  uint32_t ok;            /* 완료 카드 테두리 · 아이콘       */
  uint32_t ok_bg;         /* 완료 카드 바탕                  */
  uint32_t ok_text;       /* 완료 카드 글자                  */
  uint32_t gauge_new;     /* 방금 채워진 배부름 칸           */
} ui_palette_t;

const ui_palette_t *uiPalette(void);

#define UI_COLOR_BG             (uiPalette()->bg)
#define UI_COLOR_INK            (uiPalette()->ink)
#define UI_COLOR_INK_DIM        (uiPalette()->ink_dim)
#define UI_COLOR_LINE_DASH      (uiPalette()->line_dash)
#define UI_COLOR_CARD           (uiPalette()->card)
#define UI_COLOR_WARN           (uiPalette()->warn)
#define UI_COLOR_ERROR          (uiPalette()->error)
#define UI_COLOR_ERROR_BG       (uiPalette()->error_bg)
#define UI_COLOR_ERROR_TEXT     (uiPalette()->error_text)
#define UI_COLOR_TEXT_ON_ERROR  (uiPalette()->text_on_error)
#define UI_COLOR_OK             (uiPalette()->ok)
#define UI_COLOR_OK_BG          (uiPalette()->ok_bg)
#define UI_COLOR_OK_TEXT        (uiPalette()->ok_text)
#define UI_COLOR_GAUGE_NEW      (uiPalette()->gauge_new)


#define UI_SPACE_XS           4
#define UI_SPACE_SM           8
#define UI_SPACE_MD           14
#define UI_SPACE_LG           20
#define UI_SPACE_XL           24

#define UI_BORDER             3     /* 목업의 굵은 선 */
#define UI_TOUCH_MIN          64


/* 글꼴 크기는 목업 기준 (px). 글자 크기 단계(보통 · 크게 · 아주 크게)의 비율을 곱해 만든다. */
const lv_font_t *uiFont(int32_t size);        /* Jua, 한글. 없는 기호(… ·)는 같은 크기 Silkscreen 으로 */
const lv_font_t *uiFontPixel(int32_t size);   /* Silkscreen, 숫자 · 영문 */

#define UI_FONT_LEVEL_MAX     3
#define UI_FONT_LEVEL_DEFAULT 1     /* 크게 (115 %). 책상 위에서 조금 떨어져 보므로 */
uint8_t     uiThemeGetFontLevel(void);
void        uiThemeSetFontLevel(uint8_t level);    /* 저장한다. 화면은 부르는 쪽이 다시 만든다 */
const char *uiThemeGetFontLevelName(uint8_t level);

/* 화면 밝기 (사용자가 고른 값, %). 자동으로 어둡게 할 때도 이 값으로 돌아온다 */
uint8_t     uiThemeGetBrightness(void);
void        uiThemeSetBrightness(uint8_t percent); /* 바로 적용하고 저장한다 */
uint8_t     uiThemeGetDim(void);                   /* 자동으로 어둡게 할 밝기 %, 0 이면 어둡게 하지 않는다 */
void        uiThemeSetDim(uint8_t percent);        /* 저장한다 */

bool      uiThemeInit(void);
void      uiApplyScreen(lv_obj_t *scr);
lv_obj_t *uiCreateLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color);
lv_obj_t *uiCreateBox(lv_obj_t *parent);      /* 스타일을 모두 지운 빈 객체 */

#endif

#ifdef __cplusplus
}
#endif

#endif
