#include "ui_theme.h"


#ifdef _USE_HW_LVGL


/* 펌웨어에 넣은 TTF (main/CMakeLists.txt EMBED_FILES). 둘 다 OFL. */
extern const uint8_t jua_ttf_start[]        asm("_binary_Jua_Regular_ttf_start");
extern const uint8_t jua_ttf_end[]          asm("_binary_Jua_Regular_ttf_end");
extern const uint8_t silkscreen_ttf_start[] asm("_binary_Silkscreen_Regular_ttf_start");
extern const uint8_t silkscreen_ttf_end[]   asm("_binary_Silkscreen_Regular_ttf_end");


#define FONT_MAX          16
#define FONT_CACHE_CNT    256     // 크기마다 글리프 캐시 (한 화면에 나오는 한글은 수십 자)


typedef struct
{
  bool       is_pixel;
  int32_t    size;
  lv_font_t *font;
} font_entry_t;


static const ui_palette_t palette =
{
  .bg            = 0xDCE8C8,
  .ink           = 0x1F2A1A,
  .ink_dim       = 0x3E5234,
  .line_dash     = 0x8FA27E,
  .card          = 0xEAF2DA,
  .warn          = 0xB5541B,
  .error         = 0xA8321F,
  .error_bg      = 0xF6DED6,
  .error_text    = 0x6E2A1E,
  .text_on_error = 0xFFF6EE,
  .ok            = 0x2F6E3B,
  .ok_bg         = 0xD2EBC9,
  .ok_text       = 0x1E4A27,
  .gauge_new     = 0x3C8A4A,
};

static font_entry_t font_tbl[FONT_MAX];
static int          font_cnt = 0;




bool uiThemeInit(void)
{
  logPrintf("[OK] uiThemeInit()\n");
  logPrintf("     Jua %d KB, Silkscreen %d KB\n",
            (int)(jua_ttf_end - jua_ttf_start) / 1024,
            (int)(silkscreen_ttf_end - silkscreen_ttf_start) / 1024);
  return true;
}

const ui_palette_t *uiPalette(void)
{
  return &palette;
}

/* 같은 (종류, 크기) 는 한 번만 만든다. Tiny TTF 는 처음 그리는 글자를 그때 래스터화해 캐시에 둔다. */
static lv_font_t *uiFontGet(bool is_pixel, int32_t size)
{
  lv_font_t *font;

  for (int i=0; i<font_cnt; i++)
  {
    if (font_tbl[i].is_pixel == is_pixel && font_tbl[i].size == size)
    {
      return font_tbl[i].font;
    }
  }
  if (font_cnt >= FONT_MAX)
  {
    return NULL;
  }

  if (is_pixel)
    font = lv_tiny_ttf_create_data_ex(silkscreen_ttf_start, silkscreen_ttf_end - silkscreen_ttf_start,
                                      size, LV_FONT_KERNING_NONE, FONT_CACHE_CNT);
  else
    font = lv_tiny_ttf_create_data_ex(jua_ttf_start, jua_ttf_end - jua_ttf_start,
                                      size, LV_FONT_KERNING_NONE, FONT_CACHE_CNT);
  if (font == NULL)
  {
    return NULL;
  }

  /* 줄 상자의 세로 중심을 실제 글자의 중심에 맞춘다.
   *
   * Tiny TTF 는 줄 높이 = ascent - descent + line_gap, 기준선은 line_gap 을 모두 아래에 붙여 잡는다.
   * 그래서 상자를 가운데 정렬해도 글자가 치우친다 (1000 em 기준, 기준선 위).
   *
   *              줄 상자 중심   글자 중심        결과
   *   Jua        175            한글 약 370      위로 뜬다 (18 px 에서 약 3.5 px)
   *   Silkscreen 390            숫자 약 312      아래로 처진다
   *
   * 상자 [-base, height - base] 의 중심 = height/2 - base 가 글자 중심이 되게 base 를 고른다.
   *   Jua        높이 1.10 em, base 0.18 em → 중심 0.37 em (한글 아래 끝 -0.04, 영문 g 등 -0.2 까지 거의 들어간다)
   *   Silkscreen 높이 1.00 em, base 0.19 em → 중심 0.31 em (대문자 · 숫자만 있어 아래로 내려가는 글자가 없다)
   */
  if (is_pixel)
  {
    font->line_height = size;
    font->base_line   = (size * 19 + 50) / 100;
  }
  else
  {
    font->line_height = (size * 110 + 50) / 100;
    font->base_line   = (size * 18 + 50) / 100;
  }

  font_tbl[font_cnt].is_pixel = is_pixel;
  font_tbl[font_cnt].size     = size;
  font_tbl[font_cnt].font     = font;
  font_cnt++;

  return font;
}

/* Jua 에는 … (U+2026) 와 · (U+00B7) 이 없다. 같은 크기 Silkscreen 을 fallback 으로 단다.
 * 반대(Silkscreen → Jua)는 달지 않는다. 서로 물리면 둘 다 없는 글자에서 끝없이 돈다. */
const lv_font_t *uiFont(int32_t size)
{
  lv_font_t *font = uiFontGet(false, size);

  if (font == NULL)
    return &lv_font_montserrat_20;

  if (font->fallback == NULL)
    font->fallback = uiFontGet(true, size);

  return font;
}

const lv_font_t *uiFontPixel(int32_t size)
{
  lv_font_t *font = uiFontGet(true, size);

  return (font != NULL) ? font : &lv_font_montserrat_20;
}

void uiApplyScreen(lv_obj_t *scr)
{
  lv_obj_set_style_bg_color(scr, lv_color_hex(UI_COLOR_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_set_scrollable(scr, false);
}

lv_obj_t *uiCreateLabel(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);

  return label;
}

lv_obj_t *uiCreateBox(lv_obj_t *parent)
{
  lv_obj_t *obj = lv_obj_create(parent);

  lv_obj_remove_style_all(obj);
  lv_obj_set_scrollable(obj, false);
  lv_obj_set_clickable(obj, false);

  return obj;
}

#endif
