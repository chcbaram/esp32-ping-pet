#include "lvgl.h"

#ifdef _USE_HW_LVGL
#include "lvgl/lv_port_disp.h"
#include "lvgl/lv_port_indev.h"
#include "cli.h"


#if CLI_USE(HW_LVGL)
static void cliCmd(cli_args_t *args);
#endif

static bool is_init = false;
static bool is_enable = true;




bool lvglInit(void)
{
  if (is_init == true)
    return true;

  lv_init();
  lv_tick_set_cb(millis);

  lv_port_disp_init();
  lv_port_indev_init();

  is_init = true;

  logPrintf("[OK] lvglInit()\n");
  logPrintf("     ver  : %d.%d.%d\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

#if CLI_USE(HW_LVGL)
  cliAdd("lvgl", cliCmd);
#endif
  return true;
}

bool lvglUpdate(void)
{
  if (is_init == false)
    return false;

  if (is_enable == true)
  {
    lv_timer_handler();
  }
  return true;
}

bool lvglSetEnable(bool enable)
{
  is_enable = enable;
  return true;
}

bool lvglGetEnable(void)
{
  return is_enable;
}

#if CLI_USE(HW_LVGL)
void cliCmd(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("ver       : %d.%d.%d\n", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    cliPrintf("is_init   : %s\n", is_init   ? "True":"False");
    cliPrintf("is_enable : %s\n", is_enable ? "True":"False");
    cliPrintf("heap psram: %d KB free\n", (int)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)/1024));
    cliPrintf("heap int  : %d KB free\n", (int)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)/1024));
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "enable"))
  {
    is_enable = args->isStr(1, "on") ? true : false;
    cliPrintf("is_enable : %s\n", is_enable ? "True":"False");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("lvgl info\n");
    cliPrintf("lvgl enable on:off\n");
  }
}
#endif

#endif
