/*
 * app 정의 — HG-T113-S3 app.h 의 형태
 *
 * 각 app 은 APP_DEF 로 자기 자신을 등록하므로 런처에 목록을 손으로 나열할 필요가 없다.
 * 루프는 런처가 돈다. app 은 enter() 에서 받은 화면에 UI 를 구성하고 update() 에서 주기 작업만 한다.
 * 화면 객체의 생성/삭제는 런처가 책임진다.
 *
 * HG-T113-S3 는 app 섹션 + 링커 심볼로 훑는다. ESP-IDF 는 모듈과 같이 constructor 가 launcherAddApp() 으로 등록한다.
 */

#ifndef APP_H_
#define APP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


typedef struct app_info_t_
{
  const char name[32];
  uint16_t   order;                    /* 런처에 표시할 순서 (작을수록 앞) */

  const char *icon;                    /* LV_SYMBOL_* (홈 화면을 만들 때 쓴다) */
  uint32_t    icon_color;

  bool (*init)(void);                  /* 부팅 시 1회. 가벼운 준비만 한다.  */
  bool (*enter)(lv_obj_t *scr);        /* app 진입. scr 에 UI 를 구성한다.  */
  void (*update)(void);                /* 런처 루프에서 주기 호출           */
  void (*exit)(void);                  /* app 종료. 화면 외 자원을 정리한다.*/
} app_info_t;


bool launcherAddApp(const app_info_t *p_app);

#define APP_DEF(x_name) \
  static const app_info_t app_##x_name; \
  __attribute__((constructor)) static void app_add_##x_name(void) { launcherAddApp(&app_##x_name); } \
  static const app_info_t app_##x_name =


#ifdef __cplusplus
}
#endif

#endif
