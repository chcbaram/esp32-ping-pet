#include "touch.h"


#ifdef _USE_HW_TOUCH
#include "touch/gt911.h"
#include "cli.h"


/* 좌표는 화면(LCD_WIDTH x LCD_HEIGHT, 가로 800x480) 기준으로 돌려준다.

   GT911 은 패널 원래 방향(세로 480x800)으로 준다. 화면 회전(HW_LCD_ROTATION)에 맞춰 여기서 한 번만 바꾼다.
   LVGL 은 800x480 화면으로 알고 그리고, 패널로 보낼 때 PPA 가 돌린다 (lcd.c).
   여기서 돌린 좌표를 LVGL 이 또 돌리면 터치가 엉뚱한 곳에 떨어진다 (docs/22 알려진 문제 #6). */


#if CLI_USE(HW_TOUCH)
static void cliCmd(cli_args_t *args);
#endif
static void touchRotate(int16_t *p_x, int16_t *p_y);


static bool is_init = false;
static bool is_enable = false;
static uint16_t touch_width  = HW_LCD_WIDTH;
static uint16_t touch_height = HW_LCD_HEIGHT;

#if defined(_USE_HW_GT911)
#define ic_info_t     gt911_info_t
#define ic_init       gt911Init
#define ic_get_info   gt911GetInfo
#endif


bool touchInit(void)
{
  bool ret = false;


  ret = ic_init();
  if (ret == true)
  {
    is_init = true;
    is_enable = true;
  }

  logPrintf("[%s] touchInit()\n", ret ? "OK":"NG");

#if CLI_USE(HW_TOUCH)
  cliAdd("touch", cliCmd);
#endif
  return ret;
}

bool touchClear(void)
{
  touch_info_t info;

  touchGetInfo(&info);

  return true;
}

static void touchRotate(int16_t *p_x, int16_t *p_y)
{
  int16_t px = *p_x;    // 패널 세로 좌표 0 ~ HW_LCD_HEIGHT-1 (480)
  int16_t py = *p_y;    //                 0 ~ HW_LCD_WIDTH-1  (800)

#if HW_LCD_ROTATION == 90
  *p_x = (HW_LCD_WIDTH - 1) - py;
  *p_y = px;
#elif HW_LCD_ROTATION == 270
  *p_x = py;
  *p_y = (HW_LCD_HEIGHT - 1) - px;
#else
  *p_x = px;
  *p_y = py;
#endif
}

bool touchGetInfo(touch_info_t *p_info)
{
  bool ret;
  ic_info_t ts_info;

  if (is_init == false) return false;
  if (is_enable == false)
  {
    p_info->count = 0;
    return false;
  }

  ret = ic_get_info(&ts_info);
  if (ret == true)
  {
    p_info->count = ts_info.count;
    for (int i=0; i<ts_info.count; i++)
    {
      p_info->point[i].event = ts_info.point[i].event;
      p_info->point[i].id    = ts_info.point[i].id;
      p_info->point[i].x     = ts_info.point[i].x;
      p_info->point[i].y     = ts_info.point[i].y;
      p_info->point[i].w     = ts_info.point[i].area;
      touchRotate(&p_info->point[i].x, &p_info->point[i].y);
    }
  }

  return ret;
}

bool touchSetEnable(bool enable)
{
  is_enable = enable;
  return true;
}

bool touchGetEnable(void)
{
  return is_enable;
}

#if CLI_USE(HW_TOUCH)
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("is_init      : %s\n", is_init ? "True" : "False");
    cliPrintf("touch max ch : %d\n", TOUCH_MAX_CH);
    cliPrintf("touch width  : %d\n", touch_width);
    cliPrintf("touch height : %d\n", touch_height);
    cliPrintf("rotation     : %d\n", HW_LCD_ROTATION);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "get") && args->isStr(1, "info"))
  {
    touch_info_t info;
    uint32_t pre_time;
    uint32_t exe_time;

    while(cliKeepLoop())
    {
      pre_time = millis();
      if (touchGetInfo(&info) == true)
      {
        exe_time = millis()-pre_time;

        if (info.count > 0)
        {
          cliPrintf("cnt : %d %3dms, ", info.count, exe_time);

          for (int i=0; i<info.count; i++)
          {
            cliPrintf(" - ");
            cliPrintf("id=%d evt=%2d x=%3d y=%3d w=%3d ",
              info.point[i].id,
              info.point[i].event,
              info.point[i].x,
              info.point[i].y,
              info.point[i].w
              );
          }
          cliPrintf("\n");
        }
      }
      else
      {
        cliPrintf("touchGetInfo() Fail\n");
        break;
      }
      delay(10);
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("touch info\n");
    cliPrintf("touch get info\n");
  }
}
#endif

#endif
