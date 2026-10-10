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

### 바뀐 영역만 돌린다

한 프레임의 flush 영역들을 합쳐 두었다가 마지막 flush 에서 그 영역만 PPA 로 돌린다 (`lcdDrawScreenArea()`).
화면 영역 (x, y, w, h) 은 패널 영역 (480 - y - h, x, h, w) 이 된다 (270).

**DPI 프레임버퍼가 두 장이라 이번에 쓸 버퍼에는 두 프레임 전 내용이 남아 있다.** 이번 영역만 돌리면 직전 프레임에서
바뀐 곳이 옛 내용으로 깜빡인다. 그래서 **이번 영역 ∪ 직전 영역** 을 돌린다 (ultramcu BSP 와 같은 방식).
`draw_bitmap` 에도 그 영역만 넘겨 캐시 내리기를 줄인다.

`lvgl flush full` 로 매번 전체를 돌리게 바꿔 비교할 수 있다 (`lvgl flush area` 로 되돌림).

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

| | CPU 복사 (22) | PPA 전체, PSRAM 80 MHz | PPA 전체, **PSRAM 200 MHz** | **바뀐 영역만**, 200 MHz |
|---|---|---|---|---|
| PPA 회전 | — | 16.6 ms | 7.7 ms | **0.74 ms** (화면 4 %, 성능 표시만 바뀔 때) |
| 화면 갱신 1 회 | 38 ms | 28 ms | 12 ms | 14 ms (대부분 vsync 대기) |

- **PPA 회전은 PSRAM 대역폭에 묶인다.** PSRAM 을 80 → 200 MHz 로 올리니 절반 이하가 됐다 (ESPHome 의 이 보드 설정이 200 MHz).
  rev 3 칩은 250 MHz 도 고를 수 있지만 검증된 200 MHz 에 둔다
- 갱신 1 회 시간에는 vsync 대기(최대 16.7 ms)가 들어 있다. 실제 처리 부담은 PPA 시간이다
- 버튼 · 슬라이더를 빠르게 움직여도 흔적이나 옛 내용 깜빡임이 없다 (사용자 확인)

앱 크기 0xd3720 (약 846 KB) / 8 MB.

---

## 6. 남은 것

- [x] 갱신 시간 줄이기 — PSRAM 200 MHz, 바뀐 영역만 회전
- [ ] `LV_USE_PPA` (LVGL PPA 렌더러) 효과 측정
- [x] 한글 폰트, 4 상태 UI, 런처 → [24](24-ui.md)
