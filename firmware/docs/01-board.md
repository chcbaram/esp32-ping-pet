# 01. 보드 — Guition JC4880P443C

> 회로도 `hardware/JC4880P443_V1.0.pdf` 는 한 장짜리라 해상도가 낮다. 블록 구성은 그림으로, 넷 이름은 `pdftotext` 로 확인했다.
> 넷과 GPIO 의 연결을 회로도로 확정하지 못한 값은 ⚠️ 로 표시하고 출처를 적었다.

---

## 1. SoC

| 항목 | 값 | 출처 |
|---|---|---|
| 메인 | ESP32-P4 (RISC-V 듀얼코어), 모듈 `JC-ESP32P4-M3` | 회로도 |
| **칩 리비전** | **v3.2** — 엔지니어링 샘플(rev < 3) 이 아니다 | esptool `chip_id`, 부팅 로그 `Chip rev: v3.2` |
| CPU | **400 MHz** (rev 3.x 최대). rev < 3 이면 360 MHz | `cpu freq: 400000000 Hz` |
| PSRAM | 32 MB | 부팅 로그 |
| Flash | 16 MB, BOYA 칩 (IDF 는 generic 드라이버, DIO) | 부팅 로그 `Detected boya flash chip` |
| 무선 | ESP32-C6 (SDIO, ESP-Hosted). 이 프로젝트는 쓰지 않는다 | 회로도, ESPHome |

ESPHome 설정은 `engineering_sample: true` 지만 이 보드는 rev v3.2 였다. 같은 모델도 생산 시기에 따라 리비전이 다를 수 있다.
rev < 3 용으로 빌드한 이미지는 rev 3 칩에서 부팅하지 않는다 → `sdkconfig.defaults` 에서 `ESP32P4_SELECTS_REV_LESS_V3` 를 끈다.

---

## 2. USB 커넥터 두 개

회로도에 **Full Speed USB** 블록과 **High speed USB** 블록이 따로 있다 (넷 `USB1P1_P/N`, `ESP_USB_P/N`).
어느 넷이 어느 커넥터인지는 회로도로 확정하지 못했고, **보드에 꽂아서 확인했다.**

| 커넥터 | 평소 (앱 실행 중) | BOOT 버튼 + 리셋 |
|---|---|---|
| 처음 꽂았던 쪽 | USB-Serial-JTAG `303A:1001` (FS) | ROM USB 다운로드 `303A:0012` (FS) |
| **다른 쪽 (HS)** | **앱 TinyUSB CDC `1209:E324`, USBSpeed 3 = High-Speed** | ROM USB 다운로드 `303A:0012` (FS) |

- 두 커넥터 모두 BOOT 모드에서 ROM USB 다운로드 장치가 잡혔다. ⚠️ 처음 쪽은 FS PHY 를 USB-Serial-JTAG 와 OTG FS 가 나눠 쓰는 것으로 보인다 (확인하지 않음)
- **HS 커넥터 하나로 앱 통신과 플래시를 다 한다** → [27](27-usb.md)

---

## 3. 화면 · 터치 · 백라이트

| 항목 | 값 | 출처 |
|---|---|---|
| 패널 | 4.3" 480×800, **ST7701**, MIPI-DSI 2 레인 500 Mbps, PCLK 34 MHz | ESPHome `guition.py` |
| 타이밍 | HSYNC 12 / HBP 42 / HFP 42, VSYNC 2 / VBP 8 / VFP 166 | ESPHome `guition.py` |
| 패널 리셋 | GPIO5 ⚠️ | ESPHome `reset_pin=5` |
| MIPI PHY 전원 | 내부 LDO 채널 3, 2.5 V | ESPHome `esp_ldo` |
| 백라이트 | GPIO23 LEDC, ESPHome 은 100 Hz · 최대 80 % ⚠️ | ESPHome, 회로도 넷 `LCD_PWM` / `LEDK` |
| 터치 | GT911, I2C SDA GPIO7 / SCL GPIO8, 400 kHz, **리셋 GPIO3**, INT 미사용 ⚠️ | ESPHome, [ultramcu BSP](https://github.com/ultramcu/guition-jc4880p4-bsp), 회로도 넷 `TOUCH_RST` / `TOUCH_INT` |

초기화 시퀀스는 ESPHome `guition.py` 의 `JC4880P443` 항목에 있다. ESPHome 은 SLPOUT(0x11) / DISPON(0x29) 을 따로 보내므로,
두 명령을 시퀀스 끝에 붙여야 한다. 이 보드의 알려진 문제와 올리는 순서는 [22](22-lcd-touch.md).

---

## 4. 기타 (이 프로젝트는 쓰지 않는다)

회로도 블록 : ES8311 코덱 + 스피커 앰프, CSI 카메라, MAX485, 배터리 충전, Expand IO, ADC&AEC, 넷 `RMII_*` (이더넷).
UART0 는 넷 `UART0_TXD` / `UART0_RXD` 가 있다. ⚠️ 커넥터 위치는 확인하지 않았다 (IDF 콘솔 기본 핀 GPIO37 / GPIO38).
