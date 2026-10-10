#ifndef LVGL_H_
#define LVGL_H_

#ifdef __cplusplus
 extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_LVGL
// 이 헤더와 LVGL 컴포넌트의 헤더 이름이 같다 (lvgl.h). 다음 include 경로의 것(컴포넌트)을 가져온다.
// stm32h7-lvgl 은 lib/ 아래라 "lvgl/lvgl.h" 로 구분한다.
#include_next "lvgl.h"


bool lvglInit(void);
bool lvglUpdate(void);
bool lvglSetEnable(bool enable);
bool lvglGetEnable(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
