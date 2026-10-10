#include "ui.h"
#include "module.h"
#include "launcher.h"


#ifdef _USE_HW_LVGL

#define UI_THREAD_STACK   (8*1024)
#define UI_THREAD_PRI     3


static void uiThread(void *arg);




bool uiInit(void)
{
  bool ret;


  lvglInit();
  uiThemeInit();
  ret = launcherInit();

  ret &= xTaskCreatePinnedToCore(uiThread, "ui", UI_THREAD_STACK, NULL, UI_THREAD_PRI, NULL, 1) == pdPASS;

  logPrintf("[%s] uiInit()\n", ret ? "OK":"NG");
  return ret;
}

/* LVGL 은 이 태스크에서만 부른다. */
void uiThread(void *arg)
{
  (void)arg;

  while(1)
  {
    launcherUpdate();
    lvglUpdate();
    delay(5);
  }
}


MODULE_DEF(ui)
{
  .name     = "ui",
  .priority = MODULE_PRI_NORMAL,
  .init     = uiInit,
};

#endif
