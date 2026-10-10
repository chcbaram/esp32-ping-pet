#include "ui.h"
#include "module.h"
#include "launcher.h"


#ifdef _USE_HW_LVGL

#define UI_THREAD_STACK   (8*1024)
#define UI_THREAD_PRI     3


static void uiThread(void *arg);
static void cliUi(cli_args_t *args);




bool uiInit(void)
{
  bool ret;


  lvglInit();
  uiThemeInit();
  ret = launcherInit();

  ret &= xTaskCreatePinnedToCore(uiThread, "ui", UI_THREAD_STACK, NULL, UI_THREAD_PRI, NULL, 1) == pdPASS;

  cliAdd("ui", cliUi);

  logPrintf("[%s] uiInit()\n", ret ? "OK":"NG");
  return ret;
}

/* 전환은 요청만 남긴다. LVGL 은 ui 태스크에서만 부른다 */
void cliUi(cli_args_t *args)
{
  bool ret = false;
  static char app_name[32];

  if (args->argc == 2 && args->isStr(0, "app"))
  {
    strncpy(app_name, args->getStr(1), sizeof(app_name) - 1);
    launcherRequestApp(app_name);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "font"))
  {
    uiThemeSetFontLevel((uint8_t)constrain(args->getData(1), 0, UI_FONT_LEVEL_MAX - 1));
    launcherRequestApp(launcherGetAppName() != NULL ? launcherGetAppName() : UI_APP_DEFAULT);
    cliPrintf("font level : %d (%s)\n", uiThemeGetFontLevel(), uiThemeGetFontLevelName(uiThemeGetFontLevel()));
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("app        : %s\n", launcherGetAppName() != NULL ? launcherGetAppName() : "-");
    cliPrintf("font level : %d (%s)\n", uiThemeGetFontLevel(), uiThemeGetFontLevelName(uiThemeGetFontLevel()));
    cliPrintf("brightness : %d %%\n", uiThemeGetBrightness());
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("ui info\n");
    cliPrintf("ui app pingpet:settings\n");
    cliPrintf("ui font 0~2\n");
  }
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
