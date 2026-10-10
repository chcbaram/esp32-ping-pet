#include "ui_theme.h"


#ifdef _USE_HW_LVGL


#include "esp_partition.h"


/* 폰트는 fonts 파티션에 따로 둔다 (tools/fonts.py 로 묶고 flash.py --fonts 로 쓴다). 둘 다 OFL.
 * 펌웨어에 넣으면 앱이 3 MB 가 되어 굽기가 느려진다 (docs/24).
 * 부팅할 때 파티션을 메모리에 매핑해 이름으로 찾는다. 펌웨어에 넣었을 때와 같이 플래시를 그대로 읽는다. */
#define FONTS_MAGIC         "PPFT"
#define FONTS_ENTRY_LEN     48

typedef struct
{
  const uint8_t *data;
  size_t         size;
} ttf_t;

static ttf_t ttf_jua;
static ttf_t ttf_silkscreen;


#define FONT_MAX          40      // (종류, 크기) 조합. 글자 크기 단계를 바꾸면 새 크기가 더 생긴다
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

static const uint8_t font_pct[UI_FONT_LEVEL_MAX]   = {100, 115, 130};
static const char   *font_name[UI_FONT_LEVEL_MAX]  = {"보통", "크게", "아주 크게"};
static uint8_t       font_level = 0;
static uint8_t       brightness = 100;




static bool fontsFind(const uint8_t *p_img, size_t img_size, const char *name, ttf_t *p_ttf)
{
  uint32_t count = *(const uint32_t *)(p_img + 8);

  for (uint32_t i=0; i<count && 16 + (i + 1) * FONTS_ENTRY_LEN <= img_size; i++)
  {
    const uint8_t *p_ent = p_img + 16 + i * FONTS_ENTRY_LEN;
    uint32_t       off   = *(const uint32_t *)(p_ent + 32);
    uint32_t       size  = *(const uint32_t *)(p_ent + 36);

    if (strncmp((const char *)p_ent, name, 32) == 0 && off + size <= img_size)
    {
      p_ttf->data = p_img + off;
      p_ttf->size = size;
      return true;
    }
  }
  return false;
}

bool uiThemeInit(void)
{
  const esp_partition_t  *p_part;
  const void             *p_img = NULL;
  esp_partition_mmap_handle_t handle;
  bool ret = false;

  p_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "fonts");
  if (p_part != NULL &&
      esp_partition_mmap(p_part, 0, p_part->size, ESP_PARTITION_MMAP_DATA, &p_img, &handle) == ESP_OK &&
      memcmp(p_img, FONTS_MAGIC, 4) == 0)
  {
    ret  = fontsFind(p_img, p_part->size, "Jua-Regular.ttf", &ttf_jua);
    ret &= fontsFind(p_img, p_part->size, "Silkscreen-Regular.ttf", &ttf_silkscreen);
  }

  // 저장해 둔 글자 크기 · 밝기
  if (nvsGet("font", &font_level, 1) != true || font_level >= UI_FONT_LEVEL_MAX)
    font_level = 0;
  if (nvsGet("bl", &brightness, 1) != true || brightness < 10 || brightness > 100)
    brightness = 100;
  lcdSetBackLight(brightness);

  logPrintf("[%s] uiThemeInit()\n", ret ? "OK" : "E_");
  logPrintf("     font level %d (%d %%), brightness %d %%\n", font_level, font_pct[font_level], brightness);
  if (ret)
    logPrintf("     fonts : Jua %d KB, Silkscreen %d KB\n", (int)ttf_jua.size / 1024, (int)ttf_silkscreen.size / 1024);
  else
    logPrintf("     fonts 파티션이 비어 있다 : python3 tools/flash.py --fonts\n");
  return ret;
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

  const ttf_t *p_ttf = is_pixel ? &ttf_silkscreen : &ttf_jua;

  if (p_ttf->data == NULL)
    return NULL;      // 폰트 파티션이 비어 있다 → 부르는 쪽이 Montserrat 로
  font = lv_tiny_ttf_create_data_ex(p_ttf->data, p_ttf->size, size, LV_FONT_KERNING_NONE, FONT_CACHE_CNT);
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
static int32_t uiFontScale(int32_t size)
{
  return (size * font_pct[font_level] + 50) / 100;
}

uint8_t uiThemeGetFontLevel(void)
{
  return font_level;
}

void uiThemeSetFontLevel(uint8_t level)
{
  if (level >= UI_FONT_LEVEL_MAX || level == font_level)
    return;
  font_level = level;
  nvsSet("font", &font_level, 1);
}

const char *uiThemeGetFontLevelName(uint8_t level)
{
  return (level < UI_FONT_LEVEL_MAX) ? font_name[level] : "-";
}

uint8_t uiThemeGetBrightness(void)
{
  return brightness;
}

void uiThemeSetBrightness(uint8_t percent)
{
  brightness = constrain(percent, 10, 100);
  lcdSetBackLight(brightness);
  nvsSet("bl", &brightness, 1);
}

const lv_font_t *uiFont(int32_t size)
{
  size = uiFontScale(size);

  lv_font_t *font = uiFontGet(false, size);

  if (font == NULL)
    return &lv_font_montserrat_20;

  if (font->fallback == NULL)
    font->fallback = uiFontGet(true, size);

  return font;
}

const lv_font_t *uiFontPixel(int32_t size)
{
  lv_font_t *font = uiFontGet(true, uiFontScale(size));

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
