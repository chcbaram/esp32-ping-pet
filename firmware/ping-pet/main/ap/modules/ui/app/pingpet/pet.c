#include "pet.h"
#include <math.h>


#ifdef _USE_HW_LVGL


/* 좌표 변환
 *
 * 목업 SVG 는 viewBox (-20, -15, 330, 320) 을 210 x 204 로 그린다.
 * 로고 좌표 (x, y) → 캐릭터 영역 픽셀 ((x + 20) * S, (y + 15) * S),  S = 210 / 330
 */
#define VB_X            (-20.0f)
#define VB_Y            (-15.0f)
#define VB_W            330.0f
#define S               ((float)PET_WIDTH / VB_W)

#define PX(x)           ((int32_t)(((x) - VB_X) * S + 0.5f))
#define PY(y)           ((int32_t)(((y) - VB_Y) * S + 0.5f))
#define PL(l)           ((int32_t)((l) * S + 0.5f))

#define C_BODY          0x00D473
#define C_BODY_CRY      0x5FA582
#define C_FACE          0xDBFFD3
#define C_DARK          0x1B1B2A
#define C_BLUSH         0xFF9AA8
#define C_TONGUE        0xE0566A
#define C_WATER         0x4A9FE0
#define C_RICE          0xFFFFFF

#define MOUTH_X         39.81f
#define MOUTH_Y         128.15f
#define MOUTH_W         209.12f
#define MOUTH_H         55.72f

#define BREATH_PX       4         // 숨쉬기 : 입 막대 높이 ±4 px, 2 초 주기 (인계 문서)

#define PUPIL_R         11.0f
#define CENTER_X        (MOUTH_X + MOUTH_W / 2)   // 몸 · 입 막대의 가운데 (144.4)
#define ONIGIRI_BITES   5         // 냠냠 4 초 동안 다섯 입. 네 입은 베어 문 자국, 다섯째에 사라진다
#define ONIGIRI_EAT_MS  4000
#define ONIGIRI_X       212.0f    // 김밥 캔버스 자리 (로고 좌표)
#define ONIGIRI_Y       116.0f
#define ONIGIRI_W       102.0f
#define ONIGIRI_H       100.0f
#define EAT_W           96.0f     // 냠냠 입. 목업은 x 84 (중심 132) 로 왼쪽에 치우쳐 있어 가운데로 옮겼다


typedef enum
{
  SHAPE_DROP = 0,
} shape_t;

#define SHAPE_DROP_X    (-20.0f)  // 땀방울 객체를 만드는 자리 (로고 좌표)
#define SHAPE_DROP_Y    (-3.0f)

/* 베어 문 자리 (로고 좌표, 반지름). 입 쪽(왼쪽)부터 먹어 들어가 마지막엔 오른쪽 끝만 남는다.
   한 입마다 큰 원 하나와, 그 오른쪽 가장자리에 작은 원 둘을 더 지워 이빨 자국처럼 물결지게 한다. */
static const struct { float x, y, r; } bite_tbl[ONIGIRI_BITES - 1] =
{
  {222.0f, 196.0f, 22.0f},    // 왼쪽 아래
  {236.0f, 150.0f, 24.0f},    // 왼쪽 위
  {252.0f, 186.0f, 24.0f},    // 가운데 아래
  {272.0f, 158.0f, 26.0f},    // 가운데 위 — 오른쪽 끝만 남는다
};


static lv_obj_t *root;
static lv_obj_t *body;
static lv_obj_t *mouth;
static lv_obj_t *eye_half[2];      // 냠냠 (^^)
static lv_obj_t *eye_full[2];      // 여유 · 배고픔
static lv_obj_t *pupil[2];         // 배고픔 : 위를 본다
static lv_obj_t *smile_eye[2];     // 여유 : 눈 안의 웃는 곡선 (∩)
static lv_point_precise_t smile_pts[2][9];
static lv_obj_t *eye_flat[2];      // 울음
static lv_obj_t *blush[2];
static lv_obj_t *mouth_open;       // 배고픔
static lv_obj_t *tongue;
static lv_obj_t *mouth_eat;        // 냠냠
static lv_obj_t *tear[4];          // 울음
static lv_obj_t *puddle[2];
static lv_obj_t *frown;
static lv_obj_t *sweat;            // 배고픔
static lv_obj_t *onigiri;          // 냠냠 (김 포함). 캔버스에 한 번 그리고 한 입마다 지운다
static lv_draw_buf_t *onigiri_buf = NULL;
static int32_t   onigiri_bite = 0; // 먹은 입 수. 0 이면 통째, ONIGIRI_BITES 이면 다 먹었다
static lv_obj_t *crumb[3];
static lv_obj_t *drop[2];          // 울음 : 떨어지는 눈물방울
static lv_obj_t *foot[2];          // 배고픔 : 발을 동동 구른다

static lv_point_precise_t frown_pts[9];
static pet_state_t        cur_state = PET_STATE_MAX;

static const char *state_name[PET_STATE_MAX] = {"idle", "hungry", "cry", "eat"};




//-- 부품 만들기
//
static lv_obj_t *partRect(float x, float y, float w, float h, float r, uint32_t color)
{
  lv_obj_t *obj = lv_obj_create(root);

  lv_obj_remove_style_all(obj);
  lv_obj_set_scrollable(obj, false);
  lv_obj_set_clickable(obj, false);
  lv_obj_set_pos(obj, PX(x), PY(y));
  lv_obj_set_size(obj, PL(w), PL(h));
  lv_obj_set_style_radius(obj, r < 0 ? LV_RADIUS_CIRCLE : PL(r), 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  return obj;
}

static lv_obj_t *partCircle(float cx, float cy, float r, uint32_t color)
{
  return partRect(cx - r, cy - r, r * 2, r * 2, -1, color);
}

/* 위로 볼록한 반원 (밑변 y = base). 반원 높이만 한 상자가 원을 잘라 보인다. */
static lv_obj_t *partHalfDisk(float cx, float base, float r, uint32_t color)
{
  lv_obj_t *clip = lv_obj_create(root);
  lv_obj_t *disk;

  lv_obj_remove_style_all(clip);
  lv_obj_set_scrollable(clip, false);
  lv_obj_set_clickable(clip, false);
  lv_obj_set_pos(clip, PX(cx - r), PY(base - r));
  lv_obj_set_size(clip, PL(r * 2), PY(base) - PY(base - r));

  disk = lv_obj_create(clip);
  lv_obj_remove_style_all(disk);
  lv_obj_set_scrollable(disk, false);
  lv_obj_set_clickable(disk, false);
  lv_obj_set_pos(disk, 0, 0);
  lv_obj_set_size(disk, PL(r * 2), PL(r * 2));
  lv_obj_set_style_radius(disk, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(disk, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(disk, LV_OPA_COVER, 0);

  return clip;
}

/* 땀방울 · 주먹밥 : 삼각형이 들어가는 모양은 그리기 이벤트로 직접 그린다. 좌표는 로고 좌표다. */
static void shapeDraw(lv_event_t *e)
{
  lv_obj_t   *obj   = lv_event_get_target(e);
  lv_layer_t *layer = lv_event_get_layer(e);
  shape_t     shape = (shape_t)(intptr_t)lv_event_get_user_data(e);
  lv_area_t   a;
  int32_t     ox, oy;

  // 객체가 만들어진 자리(로고 좌표)를 기준으로 그린다. 애니메이션으로 객체를 옮기면 그림도 같이 옮겨진다
  lv_obj_get_coords(obj, &a);
  ox = a.x1 - PX(SHAPE_DROP_X);
  oy = a.y1 - PY(SHAPE_DROP_Y);

#define SX(x)   (ox + PX(x))
#define SY(y)   (oy + PY(y))

  lv_draw_triangle_dsc_t tri;
  lv_draw_rect_dsc_t     rect;
  lv_area_t              ra;

  if (shape == SHAPE_DROP)
  {
    // 꼭지 (300, -2) 에서 아래 동그라미 (300, 25) r 10.5
    lv_draw_triangle_dsc_init(&tri);
    tri.color = lv_color_hex(C_WATER);
    tri.opa   = LV_OPA_COVER;
    tri.p[0].x = SX(300.0f);  tri.p[0].y = SY(-2.0f);
    tri.p[1].x = SX(290.0f);  tri.p[1].y = SY(23.0f);
    tri.p[2].x = SX(310.0f);  tri.p[2].y = SY(23.0f);
    lv_draw_triangle(layer, &tri);

    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = lv_color_hex(C_WATER);
    rect.bg_opa   = LV_OPA_COVER;
    rect.radius   = LV_RADIUS_CIRCLE;
    ra.x1 = SX(289.5f); ra.y1 = SY(14.5f); ra.x2 = SX(310.5f); ra.y2 = SY(35.5f);
    lv_draw_rect(layer, &rect, &ra);
  }
#undef SX
#undef SY
}

static lv_obj_t *partShape(shape_t shape, float x, float y, float w, float h)
{
  lv_obj_t *obj = lv_obj_create(root);

  lv_obj_remove_style_all(obj);
  lv_obj_set_scrollable(obj, false);
  lv_obj_set_clickable(obj, false);
  lv_obj_set_pos(obj, PX(x), PY(y));
  lv_obj_set_size(obj, PL(w), PL(h));
  lv_obj_add_event_cb(obj, shapeDraw, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)shape);
  return obj;
}

/* 여유의 웃는 눈 : 동그란 눈 안에 위로 볼록한 곡선 (∩). 눈웃음 칠 때 감은 눈 모양 */
static lv_obj_t *partSmileEye(int idx, float cx, float cy)
{
  const float r     = 15.0f;            // 곡선 반지름 (로고 좌표)
  const float y0    = cy + 7.0f;        // 곡선 중심을 눈 중심보다 조금 아래로
  const int   n     = sizeof(smile_pts[0]) / sizeof(smile_pts[0][0]);
  lv_obj_t   *line  = lv_line_create(root);

  for (int i=0; i<n; i++)
  {
    float a = 3.14159f * (1.0f + (float)i / (n - 1));    // 180° → 360° (위쪽 반원)

    smile_pts[idx][i].x = PX(cx + r * cosf(a));
    smile_pts[idx][i].y = PY(y0 + r * sinf(a));
  }
  lv_line_set_points(line, smile_pts[idx], n);
  lv_obj_set_style_line_width(line, PL(7), 0);
  lv_obj_set_style_line_color(line, lv_color_hex(C_DARK), 0);
  lv_obj_set_style_line_rounded(line, true, 0);
  lv_obj_set_clickable(line, false);
  return line;
}

/* 찡그린 입 : M90,170 c20,-24 88.7,-24 108.7,0 (3 차 베지어) */
static lv_obj_t *partFrown(void)
{
  const float p0x = 90.0f,  p0y = 170.0f;
  const float p1x = 110.0f, p1y = 146.0f;
  const float p2x = 178.7f, p2y = 146.0f;
  const float p3x = 198.7f, p3y = 170.0f;
  const int   n   = sizeof(frown_pts) / sizeof(frown_pts[0]);
  lv_obj_t   *line = lv_line_create(root);

  for (int i=0; i<n; i++)
  {
    float t = (float)i / (n - 1);
    float u = 1.0f - t;
    float x = u*u*u*p0x + 3*u*u*t*p1x + 3*u*t*t*p2x + t*t*t*p3x;
    float y = u*u*u*p0y + 3*u*u*t*p1y + 3*u*t*t*p2y + t*t*t*p3y;

    frown_pts[i].x = PX(x);
    frown_pts[i].y = PY(y);
  }
  lv_line_set_points(line, frown_pts, n);
  lv_obj_set_style_line_width(line, PL(12), 0);
  lv_obj_set_style_line_color(line, lv_color_hex(C_DARK), 0);
  lv_obj_set_style_line_rounded(line, true, 0);
  lv_obj_set_clickable(line, false);
  return line;
}


/* 김밥 : 캔버스에 그린 뒤 먹은 만큼 둥글게 지운다 (투명).
 *
 * 모서리가 둥근 삼각형 (목업 경로 x 218 ~ 306, y 128 ~ 210) + 김.
 * 작은 삼각형 T 를 반지름 R 만큼 부풀린 모양 = T 를 채우고 세 변을 굵기 2R, 끝이 둥근 선으로 덧그린다.
 * 검은 테두리(R 16)를 먼저, 흰 속(R 9)을 위에 그려 테두리 두께 7 이 된다.
 */
static void onigiriRender(void)
{
  const float tx[3] = {262.0f, 234.0f, 290.0f};
  const float ty[3] = {148.0f, 196.0f, 196.0f};
  const struct { uint32_t color; float r; } layer_tbl[2] = {{C_DARK, 16.0f}, {C_RICE, 9.0f}};
  int32_t  ox = PX(ONIGIRI_X);
  int32_t  oy = PY(ONIGIRI_Y);
  int32_t  w  = onigiri_buf->header.w;
  int32_t  h  = onigiri_buf->header.h;
  uint32_t stride = onigiri_buf->header.stride;
  lv_layer_t layer;

#define CX(x)   (PX(x) - ox)
#define CY(y)   (PY(y) - oy)

  lv_canvas_fill_bg(onigiri, lv_color_hex(0), LV_OPA_TRANSP);

  if (onigiri_bite < ONIGIRI_BITES)
  {
    lv_draw_triangle_dsc_t tri;
    lv_draw_line_dsc_t     line;
    lv_draw_rect_dsc_t     rect;
    lv_area_t              ra;

    lv_canvas_init_layer(onigiri, &layer);
    for (int l=0; l<2; l++)
    {
      lv_draw_triangle_dsc_init(&tri);
      tri.color = lv_color_hex(layer_tbl[l].color);
      tri.opa   = LV_OPA_COVER;
      for (int i=0; i<3; i++)
      {
        tri.p[i].x = CX(tx[i]);
        tri.p[i].y = CY(ty[i]);
      }
      lv_draw_triangle(&layer, &tri);

      lv_draw_line_dsc_init(&line);
      line.color       = lv_color_hex(layer_tbl[l].color);
      line.opa         = LV_OPA_COVER;
      line.width       = PL(layer_tbl[l].r * 2.0f);
      line.round_start = 1;
      line.round_end   = 1;
      for (int i=0; i<3; i++)
      {
        int j = (i + 1) % 3;

        line.p1.x = CX(tx[i]);  line.p1.y = CY(ty[i]);
        line.p2.x = CX(tx[j]);  line.p2.y = CY(ty[j]);
        lv_draw_line(&layer, &line);
      }
    }

    // 김 (240, 182, 44 x 28)
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = lv_color_hex(C_DARK);
    rect.bg_opa   = LV_OPA_COVER;
    ra.x1 = CX(240.0f);  ra.y1 = CY(182.0f);
    ra.x2 = CX(284.0f);  ra.y2 = CY(210.0f);
    lv_draw_rect(&layer, &rect, &ra);
    lv_canvas_finish_layer(onigiri, &layer);

    // 베어 문 자리를 투명하게 지운다 (ARGB8888). 한 입 = 큰 원 + 오른쪽 위 · 아래 작은 원
    for (int c=0; c<onigiri_bite * 3; c++)
    {
      const float k[3][3] = {{0.0f, 0.0f, 1.0f}, {0.62f, -0.62f, 0.5f}, {0.62f, 0.62f, 0.5f}};
      int     b  = c / 3;
      float   r  = bite_tbl[b].r;
      int32_t bx = CX(bite_tbl[b].x + k[c % 3][0] * r);
      int32_t by = CY(bite_tbl[b].y + k[c % 3][1] * r);
      int32_t br = PL(k[c % 3][2] * r);

      for (int32_t y = LV_MAX(by - br, 0); y < LV_MIN(by + br + 1, h); y++)
      {
        uint32_t *p_row = (uint32_t *)(onigiri_buf->data + y * stride);

        for (int32_t x = LV_MAX(bx - br, 0); x < LV_MIN(bx + br + 1, w); x++)
        {
          if ((x - bx) * (x - bx) + (y - by) * (y - by) <= br * br)
            p_row[x] = 0;
        }
      }
    }
  }
  lv_obj_invalidate(onigiri);
#undef CX
#undef CY
}

static lv_obj_t *partOnigiri(void)
{
  lv_obj_t *canvas = lv_canvas_create(root);

  onigiri_buf = lv_draw_buf_create(PL(ONIGIRI_W), PL(ONIGIRI_H), LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
  lv_canvas_set_draw_buf(canvas, onigiri_buf);
  lv_obj_set_pos(canvas, PX(ONIGIRI_X), PY(ONIGIRI_Y));
  lv_obj_set_clickable(canvas, false);
  return canvas;
}


//-- 애니메이션
//
static void animMouthBreath(void *var, int32_t v)
{
  // 가운데를 고정하고 높이만 바꾼다
  int32_t h  = PL(MOUTH_H) + v;
  int32_t cy = PY(MOUTH_Y) + PL(MOUTH_H) / 2;

  lv_obj_set_height((lv_obj_t *)var, h);
  lv_obj_set_y((lv_obj_t *)var, cy - h / 2);
}

static void animChew(void *var, int32_t v)
{
  int32_t w  = v;
  int32_t cx = PX(CENTER_X);

  lv_obj_set_width((lv_obj_t *)var, w);
  lv_obj_set_x((lv_obj_t *)var, cx - w / 2);
}

/* 배고픔 : 땀방울이 몸 오른쪽 옆을 따라 흘러내리며 흐려진다 (v : 0 ~ 100 %)
   머리 위 (300, -2) 에서 입 높이 근처까지 로고 좌표로 125 만큼 내려온다 */
#define SWEAT_FALL      125.0f

static void animSweat(void *var, int32_t v)
{
  lv_obj_set_y((lv_obj_t *)var, PY(-3.0f) + PL(SWEAT_FALL) * v / 100);
  lv_obj_set_style_opa((lv_obj_t *)var, v < 70 ? LV_OPA_COVER : LV_OPA_COVER * (100 - v) / 30, 0);
}

/* 배고픔 : 먹을 것을 찾아 두리번거린다 (v : 눈동자 가로 이동 px) */
static void animLook(void *var, int32_t v)
{
  LV_UNUSED(var);
  lv_obj_set_x(pupil[0], PX(86.94f  - PUPIL_R) + v);
  lv_obj_set_x(pupil[1], PX(202.11f - PUPIL_R) + v);
}

/* 배고픔 : 발을 동동 구른다 (v : 발을 드는 높이 px, 위로) */
static void animStomp(void *var, int32_t v)
{
  lv_obj_set_style_translate_y((lv_obj_t *)var, -v, 0);
}

/* 울음 : 흐느낀다 (v : 몸 전체 세로 이동 px) */
static void animSob(void *var, int32_t v)
{
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

/* 울음 : 눈물방울이 발 앞으로 떨어져 흐려진다 (v : 0 ~ 100 %) */
static void animDrop(void *var, int32_t v)
{
  lv_obj_t *obj = (lv_obj_t *)var;
  int       idx = (obj == drop[0]) ? 0 : 1;

  lv_obj_set_y(obj, PY(207.0f) + PL(80.0f) * v / 100);
  lv_obj_set_style_opa(obj, v < 60 ? LV_OPA_COVER : LV_OPA_COVER * (100 - v) / 40, 0);
  LV_UNUSED(idx);
}

/* 울음 : 물웅덩이가 조금씩 커졌다 작아진다 (v : 늘어나는 폭, 로고 좌표) */
static void animPuddle(void *var, int32_t v)
{
  lv_obj_t *obj = (lv_obj_t *)var;
  float     cx  = (obj == puddle[0]) ? 20.0f : 270.0f;

  lv_obj_set_width(obj, PL(68.0f + v));
  lv_obj_set_x(obj, PX(cx - (68.0f + v) / 2));
}

static void animBite(void *var, int32_t v)
{
  if (v != onigiri_bite)
  {
    onigiri_bite = v;
    onigiriRender();
  }
}

/* 처음부터 다시 (흘러내림처럼 한 방향) */
static void animLoop(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delay_ms)
{
  lv_anim_t a;

  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_delay(&a, delay_ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
  lv_anim_start(&a);
}

static void animStartDelay(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delay_ms)
{
  lv_anim_t a;

  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_reverse_duration(&a, ms);
  lv_anim_set_delay(&a, delay_ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

static void animStart(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms)
{
  lv_anim_t a;

  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_duration(&a, ms);
  lv_anim_set_reverse_duration(&a, ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

static void animStopAll(void)
{
  lv_anim_delete(mouth, animMouthBreath);
  lv_anim_delete(mouth_eat, animChew);
  lv_anim_delete(sweat, animSweat);
  lv_anim_delete(onigiri, animBite);
  lv_anim_delete(pupil[0], animLook);
  lv_anim_delete(root, animSob);
  lv_anim_delete(foot[0], animStomp);
  lv_anim_delete(foot[1], animStomp);
  lv_anim_delete(drop[0], animDrop);
  lv_anim_delete(drop[1], animDrop);
  lv_anim_delete(puddle[0], animPuddle);
  lv_anim_delete(puddle[1], animPuddle);

  // 움직이던 부품을 제자리로
  animMouthBreath(mouth, 0);
  animChew(mouth_eat, PL(EAT_W));
  animSweat(sweat, 0);
  animBite(onigiri, 0);
  animLook(pupil[0], 0);
  animSob(root, 0);
  animStomp(foot[0], 0);
  animStomp(foot[1], 0);
  animDrop(drop[0], 0);
  animDrop(drop[1], 0);
  animPuddle(puddle[0], 0);
  animPuddle(puddle[1], 0);
}




lv_obj_t *petCreate(lv_obj_t *parent)
{
  root = lv_obj_create(parent);
  lv_obj_remove_style_all(root);
  lv_obj_set_scrollable(root, false);
  lv_obj_set_clickable(root, false);
  lv_obj_set_size(root, PET_WIDTH, PET_HEIGHT);

  // 만드는 순서가 겹치는 순서다 (뒤 → 앞). 눈물은 입 막대 아래로 지나간다 (목업과 같다)
  body = partRect(0.0f, 4.67f, 288.72f, 288.72f, 37.73f, C_BODY);

  tear[0]   = partRect(66.0f,  84.0f,  16.0f, 44.0f, 0, C_WATER);
  tear[1]   = partRect(207.0f, 84.0f,  16.0f, 44.0f, 0, C_WATER);
  tear[2]   = partRect(66.0f,  184.0f, 16.0f, 30.0f, 0, C_WATER);
  tear[3]   = partRect(207.0f, 184.0f, 16.0f, 30.0f, 0, C_WATER);
  puddle[0] = partRect(-14.0f, 291.0f, 68.0f, 14.0f, -1, C_WATER);
  puddle[1] = partRect(236.0f, 291.0f, 68.0f, 14.0f, -1, C_WATER);

  eye_half[0] = partHalfDisk(86.94f,  93.3f, 31.97f, C_FACE);
  eye_half[1] = partHalfDisk(202.11f, 93.3f, 31.97f, C_FACE);
  eye_full[0] = partCircle(86.94f,  77.31f, 31.97f, C_FACE);
  eye_full[1] = partCircle(202.11f, 77.31f, 31.97f, C_FACE);
  eye_flat[0] = partRect(54.97f,  70.0f, 63.94f, 14.0f, 7.0f, C_FACE);
  eye_flat[1] = partRect(170.14f, 70.0f, 63.94f, 14.0f, 7.0f, C_FACE);

  mouth = partRect(MOUTH_X, MOUTH_Y, MOUTH_W, MOUTH_H, 0, C_FACE);
  foot[0] = partHalfDisk(86.94f,  247.57f, 34.76f, C_FACE);     // 발
  foot[1] = partHalfDisk(199.32f, 247.57f, 34.76f, C_FACE);

  pupil[0]   = partCircle(86.94f,  64.0f, PUPIL_R, C_DARK);
  pupil[1]   = partCircle(202.11f, 64.0f, PUPIL_R, C_DARK);
  smile_eye[0] = partSmileEye(0, 86.94f,  77.31f);
  smile_eye[1] = partSmileEye(1, 202.11f, 77.31f);
  mouth_open = partRect(62.0f,  140.0f, 164.7f, 32.0f, 16.0f, C_DARK);
  tongue     = partRect(110.0f, 156.0f, 68.7f,  16.0f, 8.0f,  C_TONGUE);
  mouth_eat  = partRect(CENTER_X - EAT_W / 2, 144.0f, EAT_W, 24.0f, 12.0f, C_DARK);
  blush[0]   = partCircle(40.0f,  108.0f, 11.0f, C_BLUSH);
  blush[1]   = partCircle(249.0f, 108.0f, 11.0f, C_BLUSH);
  frown      = partFrown();

  drop[0]    = partCircle(74.0f,  214.0f, 8.0f, C_WATER);      // 발 앞으로 떨어진다
  drop[1]    = partCircle(215.0f, 214.0f, 8.0f, C_WATER);
  sweat      = partShape(SHAPE_DROP,    SHAPE_DROP_X, SHAPE_DROP_Y, 330.0f, 40.0f);
  onigiri    = partOnigiri();
  crumb[0]   = partCircle(150.0f, -2.0f, 5.0f, C_DARK);
  crumb[1]   = partCircle(186.0f, -8.0f, 4.0f, C_DARK);
  crumb[2]   = partCircle(114.0f, -8.0f, 4.0f, C_DARK);

  onigiriRender();
  petSetState(PET_STATE_IDLE);

  return root;
}

void petSetState(pet_state_t state)
{
  bool idle   = (state == PET_STATE_IDLE);
  bool hungry = (state == PET_STATE_HUNGRY);
  bool cry    = (state == PET_STATE_CRY);
  bool eat    = (state == PET_STATE_EAT);

  if (state >= PET_STATE_MAX || state == cur_state)
    return;
  cur_state = state;

  animStopAll();

  lv_obj_set_style_bg_color(body, lv_color_hex(cry ? C_BODY_CRY : C_BODY), 0);

  for (int i=0; i<2; i++)
  {
    lv_obj_set_hidden(eye_half[i], !eat);
    lv_obj_set_hidden(eye_full[i], !(idle || hungry));
    lv_obj_set_hidden(pupil[i],    !hungry);
    lv_obj_set_hidden(smile_eye[i], !idle);
    lv_obj_set_hidden(eye_flat[i], !cry);
    lv_obj_set_hidden(blush[i],    !(idle || eat));
    lv_obj_set_hidden(puddle[i],   !cry);
    lv_obj_set_hidden(drop[i],     !cry);
  }
  for (int i=0; i<4; i++)
  {
    lv_obj_set_hidden(tear[i], !cry);
  }
  for (int i=0; i<3; i++)
  {
    lv_obj_set_hidden(crumb[i], !eat);
  }
  lv_obj_set_hidden(mouth_open, !hungry);
  lv_obj_set_hidden(tongue,     !hungry);
  lv_obj_set_hidden(sweat,      !hungry);
  lv_obj_set_hidden(mouth_eat,  !eat);
  lv_obj_set_hidden(onigiri,    !eat);
  lv_obj_set_hidden(frown,      !cry);

  if (idle)   animStart(mouth,     animMouthBreath, -BREATH_PX, BREATH_PX, 1000);    // 2 초 주기
  if (eat)
  {
    lv_anim_t a;

    animStart(mouth_eat, animChew, PL(EAT_W), PL(56.0f), 220);

    // 한 입씩 : 0 → ONIGIRI_BITES 를 ONIGIRI_EAT_MS 동안 한 번 (계단처럼 정수로 바뀐다)
    lv_anim_init(&a);
    lv_anim_set_var(&a, onigiri);
    lv_anim_set_exec_cb(&a, animBite);
    lv_anim_set_values(&a, 0, ONIGIRI_BITES);
    lv_anim_set_duration(&a, ONIGIRI_EAT_MS);
    lv_anim_start(&a);
  }
  if (hungry)
  {
    animLoop(sweat, animSweat, 0, 100, 1800, 300);
    animStart(pupil[0], animLook, -PL(10.0f), PL(10.0f), 900);

    // 발 동동 : 두 발이 번갈아 들린다 (한 발 160 ms 올라갔다 내려옴, 반 박자 어긋나게)
    animStartDelay(foot[0], animStomp, 0, PL(12.0f), 160, 0);
    animStartDelay(foot[1], animStomp, 0, PL(12.0f), 160, 160);
  }
  if (cry)
  {
    animStart(root, animSob, 0, 4, 280);
    animLoop(drop[0], animDrop, 0, 100, 900, 0);
    animLoop(drop[1], animDrop, 0, 100, 900, 450);     // 번갈아 떨어진다
    animStart(puddle[0], animPuddle, 0, 14, 1400);
    animStart(puddle[1], animPuddle, 0, 14, 1400);
  }
}

pet_state_t petGetState(void)
{
  return cur_state;
}

const char *petGetStateName(pet_state_t state)
{
  return (state < PET_STATE_MAX) ? state_name[state] : "-";
}

#endif
