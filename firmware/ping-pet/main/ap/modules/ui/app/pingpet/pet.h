#ifndef PET_H_
#define PET_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"

#ifdef _USE_HW_LVGL


/* 답답이 — 로고 캐릭터 (몸, 눈 두 개, 입 막대, 발 두 개)
 *
 * 부품마다 LVGL 객체로 나눠 상태에 따라 보이고 숨기고 움직인다.
 * 좌표는 목업 SVG (viewBox -20 -15 330 320, 로고 288.72 정사각형) 를 그대로 쓴다.
 */

typedef enum
{
  PET_STATE_IDLE = 0,     // 여유   : 동그란 눈 + 웃는 곡선(∩), 볼터치, 숨쉬기
  PET_STATE_HUNGRY,       // 배고픔 : 동그란 눈 + 두리번거리는 눈동자, 벌린 입, 땀방울, 발 동동
  PET_STATE_CRY,          // 울음   : 탁한 초록 몸, 가는 선 눈, 눈물 · 떨어지는 방울, 찡그린 입, 흐느낌
  PET_STATE_EAT,          // 냠냠   : 반원 눈, 오물거리는 입, 주먹밥
  PET_STATE_MAX,
} pet_state_t;


#define PET_WIDTH       210
#define PET_HEIGHT      204


lv_obj_t   *petCreate(lv_obj_t *parent);
void        petSetState(pet_state_t state);
pet_state_t petGetState(void);
const char *petGetStateName(pet_state_t state);


#endif

#ifdef __cplusplus
}
#endif

#endif
