# ping-pet 펌웨어 문서

**Ping Pet** — 답장을 기다리는 요청(핑)을 먹고 사는 책상 위 펫. 캐릭터 이름은 **답답이**다.

Guition JC4880P443C (ESP32-P4 + ESP32-C6, 4.3" 480×800 MIPI-DSI) 보드 기준 펌웨어 개발 기록.
맥이 Slack 미응답 요청을 USB 로 보내고, 보드는 표시와 터치만 맡는다.

## 현재 상태 (2026-10-10)

| | |
|---|---|
| 보드 | JC4880P443C V1.0 (회로도 `hardware/JC4880P443_V1.0.pdf`) — **ESP32-P4 rev v3.2** (esptool 로 확인) |
| 펌웨어 | `ping-pet` — UART0 + 로그 + CLI, **USB HS CDC (esp_tinyusb)**, 모듈 시스템, **버튼 없이 다운로드**, **LCD · 터치 · 백라이트**, **LVGL 9.6**, **Ping Pet 대시보드 · 답답이 · 데모 모드 · 설정**, **맥 중계 링크 · Slack**, **USB OTA**. 보드에서 동작 확인 |
| 클럭 | CPU 400 MHz, PSRAM 32 MB **200 MHz**, Flash 16 MB |
| 빌드 | ESP-IDF v5.5.5, `-Og` — 앱 약 1.0 MB / 4 MB (OTA 자리 두 개), 폰트 2.1 MB / 3 MB (fonts 파티션) |
| USB | HS 커넥터 하나로 플래시 · CLI · 로그. 앱 `1209:E324`, ROM 다운로드 `303A:0012`, 시리얼 = MAC |

### 바로 다시 시작하기

```bash
get_idf                                  # ~/.zshrc 별칭 (IDF v5.5.5, ~/hdd/tools/esp)
cd firmware/ping-pet
idf.py build
python3 tools/flash.py                   # 버튼 없이 굽기. baram-term 이 포트를 열고 있으면 잠시 놓게 한다
python3 tools/flash.py --fonts           # 처음 한 번, 그리고 폰트가 바뀔 때 (fonts 파티션)
python3 tools/capture.py -o shot.png     # 화면 캡처
python3 tools/release.py                 # 릴리즈 파일 (dist/) — 중계 트레이의 "보드 펌웨어 업데이트" 가 이것을 OTA 로 보낸다
```

전원을 켜면 Ping Pet 화면이 바로 뜬다. 맥 중계 없이 보려면 CLI `demo on` 또는 설정 앱(상단 바 길게 누르기)의 데모 모드.

CLI 는 보드의 HS USB 포트(`/dev/cu.usbmodem…`)를 **115200** 으로 열면 된다 (baram-term). 프롬프트 `cli# `.

### 다음 작업

1. 중계 시작할 때 훑기 (켜기 전에 와 있던 미응답), 중계 앱 묶기 (.app / .dmg)
2. 답답이 이름을 어디에 보여 줄지 (보류, [24](24-ui.md) 8 절)
3. HID (웹 설정 채널, PID `E325`) → [27](27-usb.md) 6절
4. pid.codes 에 `1209:E324` ~ `E327` 등록 (사용자)

## 폴더 구성

```
firmware/
├── docs/                 이 문서
└── ping-pet/             ESP-IDF 프로젝트 (보드 하나, 부트로더는 IDF 2단 부트로더)
host/                     맥 · PC 중계 (Slack Socket Mode → USB CDC, 트레이, OTA)
hardware/                 회로도
```

## 문서 번호 규칙

stm32h563-core 와 같다.

| 대역 | 성격 |
|---|---|
| `00~09` | 하드웨어 레퍼런스 — 회로도·데이터시트에서 확정한 사실 |
| `10~19` | 개발 환경 / 프로젝트 구조 |
| `20~` | **기능별 구현 기록** — 기능 하나당 문서 하나 |

기능 문서는 "무엇을 왜 그렇게 했는지 + 막혔던 지점 + 검증 방법"을 남긴다.
**확인하지 못한 것은 확인하지 못했다고 적는다.**

## 목차

### 레퍼런스

| 문서 | 내용 |
|---|---|
| [01-board.md](01-board.md) | 보드 — SoC, 칩 리비전, USB 커넥터 두 개, 화면 · 터치 · 백라이트 핀 |

### 개발 환경

| 문서 | 내용 | 상태 |
|---|---|---|
| [10-dev-environment.md](10-dev-environment.md) | ESP-IDF 설치 경로, `get_idf`, 빌드 · 굽기, baram-term | ✅ |
| [11-project-skeleton.md](11-project-skeleton.md) | 디렉터리 구조, stm32h563-core 에서 가져온 것과 달라진 점, 모듈 시스템 | ✅ |

### 구현 기록

| 문서 | 기능 | 상태 |
|---|---|---|
| [21-uart-cli.md](21-uart-cli.md) | UART0 + 로그 + CLI, uart 드라이버 구조 | ✅ (UART0 핀은 보드에서 미확인) |
| [22-lcd-touch.md](22-lcd-touch.md) | LCD · 터치 · 백라이트 — **알려진 문제 조사** (rev 3 DSI 클럭, 기본 st7701 드라이버 검은 화면, GT911 리셋 · 주소), 더블버퍼, 회전 | ✅ |
| [23-lvgl.md](23-lvgl.md) | **LVGL 9.6** — DIRECT 모드 + PPA 회전 (바뀐 영역만, PSRAM 200 MHz 에서 0.74 ms), 길게 누르기 | ✅ |
| [24-ui.md](24-ui.md) | **UI** — inbox 모델, 4 상태 대시보드, 답답이 애니메이션, 폰트(Tiny TTF, fonts 파티션), 글자 크기, 설정 앱, 데모 모드, 캡처 | ✅ (맥 중계 남음) |
| [27-usb.md](27-usb.md) | **USB HS CDC** — CDC 주인을 보율로 가름, **버튼 없이 ROM 다운로드**, VID/PID | ✅ (CLI `boot` 명령 미확인) |
| [28-ota.md](28-ota.md) | **USB OTA** — 링크로 앱 · 폰트 받기, 보드 화면에 진행, 롤백, OTA 중 깜빡임 (DSI ISR cache safe), 릴리즈 파일 | ✅ (롤백 미확인) |

## 출처

| 약칭 | 문서 | 비고 |
|---|---|---|
| 회로도 | `hardware/JC4880P443_V1.0.pdf` | 한 장. 해상도가 낮아 핀 번호는 텍스트 레이어(`pdftotext`)로 넷 이름만 확인 |
| ESPHome | [jtenniswood/esphome-lvgl JC4880P443](https://github.com/jtenniswood/esphome-lvgl/tree/main/guition-esp32-p4-jc4880p443), ESPHome `mipi_dsi/models/guition.py` | 화면 타이밍, 초기화 시퀀스, 백라이트 · I2C 핀 |
| 참고 프로젝트 | `~/hdd/git/stm32h563-core/firmware/core/stm32h5-fw` | 공용 계층, uart 드라이버, USB, 모듈 시스템, 문서 형식 |
| 인계 문서 | 슬랙 다마고치 알림 모니터 — 작업 인계 문서 (claude.ai 문서) | 시스템 구조, 프로토콜, UI 사양 |
