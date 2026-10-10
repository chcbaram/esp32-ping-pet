# 22. LCD (ST7701S MIPI-DSI) + 터치 (GT911) + 백라이트

> 화면과 터치를 올린다. LVGL 구조는 `~/hdd/git/stm32h7-gfx/firmware/stm32h7-lvgl` 을 따른다.
> 관련: [01](01-board.md) 3절
>
> **상태: 보드에서 확인 (2026-10-10).** 화면 켜기 · 시험 화면 · 백라이트 밝기 · 터치 좌표 · 회전 방향. LVGL 은 남았다.

---

## 1. 알려진 문제와 대응

| # | 문제 | 출처 | 대응 |
|---|---|---|---|
| 1 | **rev 3 칩에서 DSI PHY 클럭 기본값이 `abort()`** — `MIPI_DSI_PHY_CLK_SRC_DEFAULT` 는 이전 호환 별칭이고 값은 rev < 3 전용 `PLL_F20M` 이다 | [SonosESP #203](https://github.com/OpenSurface/SonosESP/issues/203), IDF v5.5.5 `soc/esp32p4/include/soc/clk_tree_defs.h` 에서 확인 | `phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT` (XTAL). 이 보드는 rev v3.2 |
| 2 | **기본 `esp_lcd_st7701` 컴포넌트로는 화면이 검게 남는다** (원인은 적혀 있지 않다) | [ultramcu BSP](https://github.com/ultramcu/guition-jc4880p4-bsp) README | 컴포넌트를 쓰지 않고 직접 올린다 (2절 순서). 초기화 테이블은 `esp_lcd_panel_io_tx_param()` 으로 보낸다 |
| 3 | 초기화 테이블에 **SLPOUT / DISPON 이 없는 버전**이 있다 (ESPHome 은 드라이버가 따로 보낸다) | ESPHome `mipi_dsi/models/guition.py`, ultramcu `board_p4_st7701_init.h` | 테이블 끝에 `0x11` (+120 ms), `0x29` (+20 ms). 두 출처의 나머지 값은 같다 |
| 4 | **플래시 / NVS 쓰기 중 화면이 하얗게 번쩍인다** — 플래시 쓰기가 캐시를 끄면 PSRAM 프레임버퍼 DMA 가 멈춰 DPI 언더런 | ultramcu README | 화면이 켜진 동안 플래시 쓰기를 피한다 (이 앱은 런타임에 쓸 일이 거의 없다). 근본 대책 `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` 는 부작용 보고가 있어 필요할 때 시험한다 |
| 5 | **찢어짐 (tearing)** | ultramcu README | DPI `num_fbs = 2` 로 완성된 프레임만 스캔. CPU 로 쓴 영역은 `esp_cache_msync(C2M)` |
| 6 | **회전한 좌표를 LVGL 에 주면 두 번 회전해 터치가 죽는다** | ultramcu README | GT911 은 패널 원래 좌표(480×800 세로)를 그대로 주고 회전은 LVGL 에 맡긴다. 직접 바꿀 때는 `lx = py`, `ly = 479 - px` (270°) |
| 7 | **GT911 이 부팅마다 안정적으로 안 올라온다** — 리셋 뒤 대기 부족, INT 레벨에 따라 주소가 0x5D / 0x14 로 갈린다 | [esphome #15019](https://github.com/esphome/esphome/issues/15019) (다른 보드, 확정 진단 아님) | 리셋 펄스 후 충분히 기다리고 (ultramcu 50 ms, esphome 보고 300 ms), **0x5D → 0x14 순서로 probe**. 실패하면 재시도 |
| 8 | **화면 모서리 버튼이 잘 안 눌린다** — 터치가 모서리 바로 바깥에 떨어진다 | SonosESP #203 | 모서리 근처 위젯은 `ext_click_area` 를 넉넉히 |
| 9 | 백라이트 PWM 주파수에 따라 **코일 소음** | SonosESP #203 (5 kHz → 25 kHz 로 해결) | 25 kHz 부터 시험. ESPHome 은 100 Hz, ultramcu 는 5 kHz |
| 10 | (참고) ST7701 **깜빡임은 VCOM 조정**으로 줄인다 | [Nicolai-Electronics ST7701 v1.1.0](https://github.com/Nicolai-Electronics/esp32-component-mipi-dsi-abstraction/releases/tag/v1.1.0) | 이 보드에서 보고된 것은 아니다. 깜빡이면 그때 본다 |

---

## 2. 올리는 순서 (ultramcu 에서 검증된 순서)

1. LDO 채널 3, 2500 mV (DSI PHY 전원)
2. DSI 버스 — 2 레인, 500 Mbps, **`phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT`** (1절 #1)
3. DBI IO (명령 채널)
4. DPI 패널 — 480×800, RGB565, 34 MHz, `num_fbs = 2`, `use_dma2d = 1`
   타이밍 HSYNC 12 / HBP 42 / HFP 42, VSYNC 2 / VBP 8 / VFP 166
5. **패널 리셋 GPIO5 — DPI 를 만든 뒤, 초기화 테이블 전에.** low 20 ms → high 120 ms
6. ST7701S 초기화 테이블 (`tx_param`, SLPOUT / DISPON 포함)
7. `esp_lcd_panel_init()`
8. 백라이트 GPIO23 (LEDC)

---

## 3. 핀 (ultramcu `board_p4_pins.h` "on-hardware VERIFIED")

| 기능 | 핀 | ESPHome 설정과 비교 |
|---|---|---|
| 패널 리셋 | GPIO5 | 같다 |
| 백라이트 | GPIO23 → MP3202 부스트. 0 이면 꺼짐 | 같다 |
| I2C SDA / SCL | GPIO7 / GPIO8 (**ES8311 코덱과 같은 버스**) | 같다 |
| **GT911 리셋** | **GPIO3** | ESPHome 설정에는 없다 |
| GT911 INT | 쓰지 않음 (폴링). 커뮤니티 문서는 GPIO21 ⚠️ | — |
| BOOT 버튼 | GPIO35 | — |
| UART0 | GPIO37 TX / GPIO38 RX | 21 의 ⚠️ 를 뒷받침 |

---

## 4. 구현

| 파일 | 출처 | |
|---|---|---|
| `hw/driver/lcd.c`, `common/hw/include/lcd.h` | stm32h7-lvgl `lcd.h` 의 이름 · 색 정의 | DSI / DPI 올리기, 더블버퍼, 백라이트, 그리기 몇 개, CLI. 그리기 라이브러리 전체(폰트 등)는 가져오지 않았다 |
| `hw/driver/lcd/st7701.c` | ESPHome · ultramcu 초기화 테이블 | `tx_param` 으로 보낸다 |
| `hw/driver/touch.c`, `common/hw/include/touch.h` | stm32h7-lvgl | 패널 좌표 → 화면 좌표 변환을 여기서 한 번만. `touch gui` 는 `cli_gui` 가 없어 뺐다 |
| `hw/driver/touch/gt911.c`, `touch/gt911.h` | stm32h7-lvgl | **설정 테이블(480x480)을 쓰지 않는다**, 리셋 GPIO3, INT 대신 상태 레지스터 폴링, 0x5D / 0x14 probe |
| `hw/driver/i2c.c`, `common/hw/include/i2c.h` | stm32h7-lvgl `i2c.h` API | ESP-IDF `i2c_master` 위에. 주소마다 장치 핸들을 만들어 둔다 |

### 화면 좌표와 회전

프레임버퍼는 패널 방향(세로 480×800)이다. 그리기 함수와 터치는 화면 좌표(가로 800×480)를 쓰고, `HW_LCD_ROTATION`(270) 으로 바꾼다.
`lcd.c` 의 `lcdIndex()` 와 `touch.c` 의 `touchRotate()` 가 서로 반대 방향이다.

### 화면 갱신

`lcdUpdateDraw()` : 그린 버퍼를 `esp_lcd_panel_draw_bitmap()` 에 넘기면 DPI 드라이버가 복사 없이 캐시만 내리고 표시 버퍼를 바꾼다.
다음 vsync 를 기다린 뒤 방금 표시한 내용을 다음 버퍼로 복사한다 (이어 그리기). 한 번에 **38 ms** — 대부분 PSRAM 768 KB 복사다. LVGL 단계에서 바꾼다.

---

## 5. 검증

```
[OK] lcdInit()
     ST7701S 480x800, MIPI-DSI 2 lane 500 Mbps, 34 MHz
     screen 800x480, rotation 270
[OK] gt911Init()
     addr 0x5D, ID 911, FW 0x1060, 1085x600, 50 ms

cli# lcd info
fb        : 0x48003040, 0x480be880 (draw 0)
backlight : 100 %, 25000 Hz
update    : 38 ms
fps       : 60
```

| 항목 | 결과 |
|---|---|
| 화면 | `lcd test` 컬러 바 · 모서리 표식이 보인다 (사용자 확인) |
| rev 3 DSI 클럭 | `MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT` 로 정상 (기본값 abort 재현은 하지 않았다) |
| 백라이트 | `lcd bl 30` 동작, 25 kHz. 코일 소음은 확인하지 않았다 |
| GT911 | **0x5D**, 리셋 후 50 ms 첫 probe 에 응답 |
| GT911 설정 해상도 | **1085×600** 으로 읽힌다 — 패널과 다르다. 그러나 실제 좌표는 패널 범위로 나온다 (아래). 설정은 건드리지 않는다 |
| 터치 좌표 | 쓸어 본 1,251 점이 x 8~748, y 34~474 — 화면 800×480 안 |
| **회전 (270)** | 시험 화면의 네 모서리 표식(60×60)을 누른 점이 각 모서리 영역에 들어온다 — 왼쪽 위 20, 오른쪽 위 23, 왼쪽 아래 24, 오른쪽 아래 67 점 |

---

## 6. 남은 것

- [x] 회전 방향 대조
- [ ] 백라이트 코일 소음
- [ ] LVGL (stm32h7-lvgl 구조), 800×480 가로 회전 (PPA), 갱신 시간 줄이기
