#ifndef MODULE_H_
#define MODULE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


typedef enum
{
  MODULE_PRI_HIGH = 1,
  MODULE_PRI_1,
  MODULE_PRI_2,
  MODULE_PRI_3,
  MODULE_PRI_4,
  MODULE_PRI_NORMAL,
  MODULE_PRI_LOWEST,
  MODULE_PRI_MAX,
} ModulePriority_t;


typedef struct
{
  const char       name[32];
  ModulePriority_t priority;

  //-- 한 번만 부른다. moduleInit() 이 우선순위 순으로.
  bool           (*init)(void);

  //-- 주기적으로 부른다. moduleUpdate() 가 등록 순으로.
  //   스레드를 따로 만들 만큼 무겁지 않은 일을 여기 둔다.
  void           (*update)(void const *arg);
  void            *arg;

#ifdef _USE_HW_EVENT
  //-- 이벤트 구독자. init() 이 성공하면 moduleInit() 이 자동으로 등록한다.
  //   모듈이 직접 eventSub() 를 부를 필요가 없다.
  event_func_t     event_cb;
#endif

} module_t;


/*
 * 모듈 자기 등록.
 *
 * 파일을 추가하는 것만으로 등록되므로 목록을 따로 관리할 필요가 없다.
 * moduleInit() 이 등록된 모듈을 우선순위 순으로 init() 한다.
 *
 * STM32 프로젝트는 디스크립터를 .module 섹션에 심고 링커 심볼(_smodule ~ _emodule)로 훑는다.
 * ESP-IDF 는 링커 스크립트를 생성하므로, 대신 constructor 함수가 app_main() 전에 moduleAdd() 로 등록한다.
 * 쓰는 쪽(MODULE_DEF)은 같다. main 컴포넌트를 WHOLE_ARCHIVE 로 묶어야 링커가 모듈 파일을 버리지 않는다.
 */
#define MODULE_DEF(x_name) \
  static const module_t module_##x_name; \
  __attribute__((constructor)) static void module_add_##x_name(void) { moduleAdd(&module_##x_name); } \
  static const module_t module_##x_name =


bool moduleInit(void);
bool moduleUpdate(void);
bool moduleAdd(const module_t *p_module);


#ifdef __cplusplus
}
#endif

#endif
