#ifndef POWER_H_
#define POWER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


/* 화면 끄기 — PC 와 연결이 없는 채로 오래 두면 화면(백라이트 · LCD)을 끈다.
 *
 *   맥 중계가 살아 있지 않고(90 초 동안 sync · tick 없음), 데모 · OTA 가 아니고, 화면을 만지지 않은 채
 *   POWER_SCREEN_OFF_MS 가 지나면 끈다. 칩 · USB · 터치는 그대로 돈다.
 *   중계가 다시 연결되거나(첫 sync) 화면을 만지면 바로 켠다.
 *   포트가 열렸는지(DTR)로는 판단하지 않는다 : 맥은 포트를 닫아도 DTR 을 내리지 않는다. 켜는 터치는 화면을 켜기만 한다 (카드가 눌리지 않게).
 *
 *   딥슬립은 CLI `power sleep` 으로만. 깨우는 소스가 없어 RST 버튼이나 USB 를 다시 꽂아 깨운다
 *   (터치 INT 는 쓰지 않고, BOOT 버튼 GPIO35 는 딥슬립에서 깨울 수 있는 LP GPIO 가 아니다).
 */

#define POWER_SCREEN_OFF_MS     (10 * 60 * 1000)
#define POWER_WAKE_GUARD_MS     1500        // 터치로 켠 뒤 이만큼은 누름을 무시한다


void powerTouch(void);          // 화면을 만졌다 (ui 태스크)
bool powerIsScreenOff(void);
bool powerIsWakeTouch(void);    // 방금 터치로 화면을 켰다 → 이 터치로 누르지 않는다
void powerScreenOff(void);
void powerSleep(void);          // 딥슬립 (돌아오지 않는다)


#ifdef __cplusplus
}
#endif

#endif
