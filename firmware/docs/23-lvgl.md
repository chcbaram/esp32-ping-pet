# 23. LVGL 9.6 — PPA 로 돌려 그리기

> LVGL 을 올린다. 구조는 `~/hdd/git/stm32h7-gfx/firmware/stm32h7-lvgl` (LVGL 9.5) 을 따른다.
> 관련: [22](22-lcd-touch.md) (화면 좌표, 회전, 더블버퍼)
>
> **상태: 보드에서 확인 (2026-10-10).** 시험 화면 표시, 버튼 짧게 · 길게 누르기, 슬라이더, 터치 좌표, 성능 표시.

---

## 1. 버전 — 9.6.0

참고 프로젝트는 9.5.0 이다. 9.6.0 (2026-09-16, 레지스트리 `9.6.0~1`) 을 골랐다.

| 9.6 에서 바뀐 것 | 이 프로젝트에 |
|---|---|
| ESP32-P4 회전 장면 그리기 66 % 빨라짐, PPA 그리기 유닛 수정 | 이 보드가 P4 이고 화면을 돌려 쓴다 |
| binfont 동적 글리프 로딩 | 한글 폰트(KS X 1001 2,350 자)를 통째로 메모리에 올리지 않아도 된다 |
| tiny_ttf 경쟁 상태 · use-after-free 수정 | TTF 폰트를 쓸 때 |
| 설정 방법 세 가지를 하나로 (깨지는 변경) | `lv_conf.h` 대신 ESP-IDF Kconfig 를 쓰므로 영향이 작다 |

---

## 2. 구조

| 파일 | 출처 | |
|---|---|---|
| `common/hw/include/lvgl.h` | stm32h7-lvgl | **`#include_next "lvgl.h"`** — 이 헤더와 컴포넌트 헤더의 이름이 같다. 참고 프로젝트는 `lib/` 아래라 `"lvgl/lvgl.h"` 로 구분했다 |
| `hw/driver/lvgl.c` | stm32h7-lvgl | DMA2D · 파일시스템 부분을 뺐다 |
| `hw/driver/lvgl/lv_port_disp.c` | stm32h7-lvgl | **DIRECT 모드 + PPA 회전** (3 절) |
| `hw/driver/lvgl/lv_port_indev.c` | stm32h7-lvgl | 그대로. 좌표는 touch.c 가 이미 화면 좌표로 준다 |
| `ap/modules/ui/ui.c` | stm32h7-lvgl | `MODULE_DEF(ui)`, "ui" 태스크(우선순위 3, 8 KB)에서만 LVGL 을 부른다. 지금은 시험 화면 |

설정은 `sdkconfig.defaults` 에 참고 프로젝트 `lv_conf.h` 값을 옮겼다 — RGB565, C 표준 malloc(PSRAM), 33 ms, Montserrat 20 / 28 / 40, 기본 28, 성능 표시.

---

## 3. 화면 출력 — DIRECT + PPA

참고 프로젝트는 LTDC 프레임버퍼 두 장을 그대로 LVGL DIRECT 버퍼로 넘긴다. 이 보드는 패널이 세로(480×800), 화면은 가로(800×480)라 그럴 수 없다.

```
LVGL ──(DIRECT, 바뀐 영역만)──▶ 화면 버퍼 800×480 (PSRAM, 128 B 정렬, 한 장)
                                   │ 마지막 flush
                                   ▼ lcdDrawScreen()
                       PPA 회전 (270, 블로킹) ──▶ DPI 프레임버퍼 [다음]
                                   │ draw_bitmap = 표시 버퍼 바꾸기 (복사 없음)
                                   ▼ vsync 대기
```

- PPA 각도는 **반시계 방향**이다. 화면 좌표 → 패널 좌표가 시계 방향 90° 라 `PPA_SRM_ROTATION_ANGLE_270`
- 입력 캐시 쓰기(C2M)와 출력 캐시 무효화(M2C)는 PPA 드라이버가 한다
- PPA 가 블로킹으로 끝난 뒤 돌아오므로 LVGL 화면 버퍼는 한 장이면 된다
- LVGL 자체의 PPA 렌더러(`LV_USE_PPA`)는 아직 끄고 있다

---

## 4. 터치 — 길게 누르기가 끊기지 않게

GT911 은 INT 를 쓰지 않아 상태 레지스터를 폴링한다. 새 좌표가 아직 없을 때 "0 개" 를 돌려주면, 누르고 있는 동안에도
LVGL 이 뗀 것으로 보아 **길게 누르기가 끊긴다.** 새 데이터가 없으면 **마지막 상태를 그대로** 돌려주게 고쳤다 (`gt911.c`).
→ 시험 화면의 `long pressed` 로 확인.

---

## 5. 측정

```
cli# lcd info
update    : 28 ms (ppa 16634 us)
fps       : 60

cli# lvgl info
ver       : 9.6.0
heap psram: 30503 KB free
heap int  : 514 KB free
```

| | CPU 복사 (22) | PPA 회전 |
|---|---|---|
| 화면 갱신 1 회 | 38 ms | **28 ms** (PPA 16.6 ms + vsync 대기) |

앱 크기 0xd3720 (약 846 KB) / 8 MB.

---

## 6. 남은 것

- [ ] 갱신 시간 줄이기 — PPA 가 PSRAM 대역폭에 묶여 있다 (지금 80 MHz). PSRAM 200 MHz (ESPHome 설정), 바뀐 영역만 회전
- [ ] `LV_USE_PPA` (LVGL PPA 렌더러) 효과 측정
- [ ] 한글 폰트, 4 상태 UI, 런처 구조 (HG-T113-S3 참고)
