/*
 * 런처 — HG-T113-S3 launcher 의 뼈대
 *
 * app 을 띄우고 거둔다. app 목록은 APP_DEF 로 등록된 것을 쓴다.
 * 화면 구조는 스크린을 갈아 끼우지 않고 컨테이너를 겹친다 (HG-T113-S3 와 같다).
 *
 *   scr_root
 *     +-- app_cont    실행 중인 app
 *
 * 전원을 켜면 홈 화면 없이 기본 app(UI_APP_DEFAULT, Ping Pet)을 바로 띄운다.
 * 홈 그리드 · 제스처 · 오버레이는 app 이 늘어나면 HG-T113-S3 에서 가져온다.
 */

#ifndef LAUNCHER_H_
#define LAUNCHER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"
#include "app.h"
#include "ui_theme.h"

#ifdef _USE_HW_LVGL


bool  launcherInit(void);
void  launcherUpdate(void);

bool  launcherRunApp(const char *name);
void  launcherRequestApp(const char *name);   /* 다른 태스크(cli 등)나 이벤트 안에서 : 다음 launcherUpdate() 에서 전환 */
void  launcherExitApp(void);
const char *launcherGetAppName(void); /* 실행중인 app 이 없으면 NULL */


#endif

#ifdef __cplusplus
}
#endif

#endif
