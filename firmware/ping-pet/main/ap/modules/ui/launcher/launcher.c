#include "launcher.h"


#ifdef _USE_HW_LVGL

#define MAX_APPS          16


static const app_info_t *app_tbl[MAX_APPS];
static int               app_cnt = 0;

static lv_obj_t         *scr_root = NULL;
static lv_obj_t         *app_cont = NULL;
static const app_info_t *app_cur  = NULL;
static const char *volatile app_req = NULL;




bool launcherAddApp(const app_info_t *p_app)
{
  if (app_cnt >= MAX_APPS)
    return false;

  app_tbl[app_cnt++] = p_app;
  return true;
}

static const app_info_t *launcherFindApp(const char *name)
{
  for (int i=0; i<app_cnt; i++)
  {
    if (strcmp(app_tbl[i]->name, name) == 0)
      return app_tbl[i];
  }
  return NULL;
}

bool launcherInit(void)
{
  bool ret;

  scr_root = lv_screen_active();
  uiApplyScreen(scr_root);

  for (int i=0; i<app_cnt; i++)
  {
    if (app_tbl[i]->init != NULL)
      app_tbl[i]->init();
  }

  // 전원을 켜면 기본 app 을 바로 띄운다
  ret = launcherRunApp(UI_APP_DEFAULT);

  logPrintf("[%s] launcherInit()\n", ret ? "OK" : "NG");
  logPrintf("     apps : %d, run : %s\n", app_cnt, ret ? UI_APP_DEFAULT : "-");
  return ret;
}

void launcherRequestApp(const char *name)
{
  app_req = name;
}

void launcherUpdate(void)
{
  // 이벤트 콜백 안에서 화면을 지우면 안 되므로 전환은 여기서 한다
  const char *req = app_req;
  if (req != NULL)
  {
    app_req = NULL;
    launcherRunApp(req);
  }

  if (app_cur != NULL && app_cur->update != NULL)
  {
    app_cur->update();
  }
}

bool launcherRunApp(const char *name)
{
  const app_info_t *p_app = launcherFindApp(name);

  if (p_app == NULL)
    return false;

  launcherExitApp();

  app_cont = uiCreateBox(scr_root);
  lv_obj_set_size(app_cont, LV_PCT(100), LV_PCT(100));
  lv_obj_set_clickable(app_cont, true);

  app_cur = p_app;
  if (app_cur->enter != NULL && app_cur->enter(app_cont) != true)
  {
    launcherExitApp();
    return false;
  }
  return true;
}

void launcherExitApp(void)
{
  if (app_cur != NULL && app_cur->exit != NULL)
  {
    app_cur->exit();
  }
  app_cur = NULL;

  if (app_cont != NULL)
  {
    lv_obj_delete(app_cont);
    app_cont = NULL;
  }
}

const char *launcherGetAppName(void)
{
  return (app_cur != NULL) ? app_cur->name : NULL;
}

#endif
