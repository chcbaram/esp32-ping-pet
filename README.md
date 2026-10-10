# Ping Pet

**답장을 기다리는 요청(핑)을 먹고 사는 책상 위 펫.** 캐릭터 이름은 **답답이**다.

Slack 에서 나에게 온 DM 과 멘션 중 아직 답장하지 않은 것을 책상 위 4.3" 화면에 보여 준다.
답장을 미룰수록 답답이가 배고파하고, 30 분을 넘기면 운다. 답장하면 밥을 먹는다.

| 여유 | 배고픔 | 울음 | 냠냠 |
|---|---|---|---|
| 미응답 0 건 | 미응답 1 건 이상 | 가장 오래된 요청 30 분 넘음 | 답장한 순간 4 초 |
| 오늘 답장 수, 평균 응답 시간, 최근 처리 | 답장 대기 카드 | 화면이 빨갛게, 오래된 카드 강조 | 완료 카드 + 남은 요청 |

---

## 구조

```
Slack ──(Socket Mode)──▶ 맥 / PC 중계 ──(USB, 줄 단위 JSON)──▶ 보드 (화면 · 터치)
```

- **보드는 네트워크에 붙지 않고 토큰도 갖지 않는다.** 사내 Wi-Fi 에 IoT 기기를 붙일 필요가 없다
- 맥 / PC 의 중계가 Slack 이벤트를 받아 "미응답 목록" 을 만들고 USB 로 보낸다. 토큰은 OS 의 비밀 저장소(키체인 등)에만 둔다
- 카드를 누르면 맥 / PC 의 Slack 앱에서 그 대화가 열린다

---

## 기능

- **4 상태 대시보드** — 시계 · 날짜 · 요일, 미응답 요약, 연결 상태, 배부름 10 칸
- **답답이 애니메이션** — 숨쉬기, 두리번 · 땀 · 발 동동, 흐느낌 · 눈물방울, 김밥 베어 먹기
- **답장 대기 카드** — 경과 시간에 따라 색이 바뀐다 (10 분 주황, 30 분 빨강)
  - 짧게 누르기 : 맥 / PC 의 Slack 에서 그 대화 열기
  - 1 초 누르기 : 처리 중 / 답장 없이 치우기
- **처리 중** — 봤지만 시간이 걸리는 요청. 보드에서 고르거나 Slack 에서 👀. 울음 계산에서 빠진다
- **Slack 반응으로 처리** — 👀 처리 중, ✅ 완료
- **시작할 때 훑기** — 중계를 켜기 전에 와 있던 미응답 (기본 3 일, 트레이에서 고른다)
- **설정** (상단 바 길게 누르기) — 글자 크기 3 단계, 화면 밝기, 데모 모드
- **트레이 앱** — 답답이 로고 (미응답이 있으면 빨간 점), 미응답 목록, 보드 펌웨어 업데이트 (USB OTA)
- **데모 모드** — Slack 없이 가상의 메시지로 4 상태를 돌려 본다
- 미응답이 없으면 10 분 뒤 화면을 어둡게 한다

---

## 준비물

| | |
|---|---|
| 보드 | **Guition JC4880P443C** (ESP32-P4 + ESP32-C6, 4.3" 480×800 MIPI-DSI, 정전식 터치) |
| 케이블 | USB-C 하나 — 보드의 **HS USB 포트**에 꽂는다 (굽기 · 통신 · 전원) |
| PC | macOS (확인됨), Windows · Linux (준비 중) |
| Slack | 사용자 앱을 설치할 수 있는 워크스페이스 |

---

## 설치

### 1. 펌웨어 굽기

보드의 **HS USB 포트**에 케이블을 꽂고 굽는다. `flash.py` 가 버튼 없이 보드를 다운로드 모드로 넣고, 굽고 나면 다시 시작한다.
보드가 응답하지 않으면 **BOOT 를 누른 채 RST** 를 눌렀다 뗀 뒤 다시 실행한다.

#### 릴리즈 파일로 (ESP-IDF 필요 없음)

Releases 에서 세 파일을 한 폴더에 받는다.

| 파일 | 내용 |
|---|---|
| `ping-pet-fw-<버전>.bin` | 펌웨어 (부트로더 + 파티션 테이블 + 앱) |
| `ping-pet-fonts-<버전>.bin` | 폰트 (Jua, Silkscreen) |
| `flash.py` | 굽기 도구 |

```bash
pip install "esptool<5" pyserial
python3 flash.py --fw ping-pet-fw-<버전>.bin --fonts-bin ping-pet-fonts-<버전>.bin   # 처음
python3 flash.py --fw ping-pet-fw-<버전>.bin                                        # 업데이트 (폰트가 같으면)
```

글자 크기 · 밝기 설정은 지우지 않고 남긴다. **그 뒤의 업데이트는 중계 트레이의 "보드 펌웨어 업데이트" 로 한다** (USB OTA, 보드 화면에 진행이 보인다).
받은 파일은 `SHA256SUMS` 로 확인할 수 있다 (`shasum -a 256 -c SHA256SUMS`).

#### 소스에서 빌드

ESP-IDF **v5.5.5** 가 필요하다 ([설치 · 개발 환경](firmware/docs/10-dev-environment.md)).

```bash
cd firmware/ping-pet
idf.py build
python3 tools/flash.py --fonts     # 처음 한 번 : 앱 + 폰트 파티션
python3 tools/flash.py             # 그 뒤로는 앱만
python3 tools/release.py           # 릴리즈 파일을 dist/ 에 만든다
```

전원을 켜면 Ping Pet 화면이 바로 뜬다. 중계 없이 보려면 상단 바를 길게 눌러 설정 → **데모 모드**.

### 2. 중계 앱 설치

[Releases](https://github.com/chcbaram/esp32-ping-pet/releases) 에서 OS 에 맞는 파일을 받는다.

| OS | 파일 | 설치 |
|---|---|---|
| macOS | `PingPet-<버전>-macos.dmg` | 열어서 Ping Pet 을 응용 프로그램으로 끌어다 놓는다. 서명하지 않은 앱이라 처음엔 **우클릭 → 열기** |
| Windows | `PingPet-<버전>-windows-Setup.exe` | 실행해 설치. SmartScreen 이 막으면 **추가 정보 → 실행** |
| Linux | `PingPet-<버전>-linux-x86_64.AppImage` | `chmod +x` 뒤 실행 |

실행하면 메뉴 막대 · 트레이에 답답이 아이콘이 뜬다. 앱 안에 같은 버전의 보드 펌웨어가 들어 있어 트레이 → **보드 펌웨어 업데이트** 로 보드를 올린다.
맥은 처음 Slack 토큰을 읽을 때 키체인 접근을 묻는다 → **항상 허용**.

#### 소스에서 실행

```bash
cd host
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python ping_pet.py --simulate             # Slack 없이 보드 연결 시험
.venv/bin/python ping_pet.py                        # Slack 연결 (아래 설정 뒤). 메뉴 막대 · 트레이에 아이콘
.venv/bin/python ping_pet.py --no-tray              # 트레이 없이 (서비스 등)
```

설치 파일은 `bash packaging/build_mac.sh` (맥), `bash packaging/build_linux.sh` (리눅스), 윈도우는 PyInstaller + Inno Setup (`packaging/ping_pet.iss`).
태그 `v*` 를 올리면 GitHub Actions 가 펌웨어와 세 OS 의 설치 파일을 빌드해 Release 를 만든다 (`.github/workflows/release.yml`).

중계는 보드를 USB VID:PID `1209:E324` 로 찾는다. 포트 이름을 신경 쓸 필요가 없다.

---

## Slack 설정

1. <https://api.slack.com/apps> → **Create New App → From a manifest** → 워크스페이스 선택
2. [`host/slack-app-manifest.yaml`](host/slack-app-manifest.yaml) 내용을 붙여 넣고 만든다 (앱 이름 Ping Pet, Socket Mode, 읽기 전용 사용자 스코프)
3. **Basic Information → App-Level Tokens** 에서 토큰을 만든다. Token Name 은 아무거나 (예: `ping-pet`), 스코프 `connections:write` → `xapp-…`
   - 같은 페이지 위쪽 **App Credentials** 의 값(Verification Token, Signing Secret 등)은 쓰지 않는다
4. **Install App** 에서 워크스페이스에 설치한다 → **User OAuth Token** `xoxp-…`
5. 두 토큰을 OS 의 비밀 저장소에 넣는다. 코드나 파일에는 쓰지 않는다

> **Event Subscriptions → Subscribe to events on behalf of users** 에 `message.im` `message.mpim` `message.channels` `message.groups` `reaction_added` `reaction_removed` 6 개가 있는지 확인한다. 매니페스트를 붙여 넣어도 이벤트가 빠질 때가 있다 (반응 👀 · ✅ 가 바로 반영되지 않으면 이것).
>
> 매니페스트(권한 · 이벤트)가 바뀌면 앱 설정의 **App Manifest** 에 새 내용을 붙여 넣고 **Reinstall to Workspace** 로 다시 설치한다. 토큰이 바뀌었으면 키체인 값도 바꾼다.

토큰은 모두 서비스 이름 `ping-pet`, 계정 이름 `xoxp` · `xapp` 으로 넣는다. 중계가 이 이름으로 찾는다.
아래 명령은 실행하면 토큰을 묻는다. 거기에 붙여 넣는다 (화면에 보이지 않는다).

**macOS** (키체인)

```bash
security add-generic-password -s ping-pet -a xoxp -w    # xoxp-… (User OAuth Token)
security add-generic-password -s ping-pet -a xapp -w    # xapp-… (App-Level Token)

# 잘못 넣었으면 지우고 다시 넣는다
security delete-generic-password -s ping-pet -a xoxp
security delete-generic-password -s ping-pet -a xapp
```

**Linux** (Secret Service — GNOME 키링, KWallet 등). `host` 폴더에서

```bash
.venv/bin/python -m keyring set ping-pet xoxp    # xoxp-… (User OAuth Token)
.venv/bin/python -m keyring set ping-pet xapp    # xapp-… (App-Level Token)

# 잘못 넣었으면 지우고 다시 넣는다
.venv/bin/python -m keyring del ping-pet xoxp
.venv/bin/python -m keyring del ping-pet xapp
```

**Windows** (자격 증명 관리자). `host` 폴더에서 PowerShell 로 (명령 프롬프트는 `#` 주석을 빼고)

```powershell
.venv\Scripts\python -m keyring set ping-pet xoxp    # xoxp-… (User OAuth Token)
.venv\Scripts\python -m keyring set ping-pet xapp    # xapp-… (App-Level Token)

# 잘못 넣었으면 지우고 다시 넣는다
.venv\Scripts\python -m keyring del ping-pet xoxp
.venv\Scripts\python -m keyring del ping-pet xapp
```

### 무엇을 미응답으로 보나

| 메시지 | 미응답 추가 | 해제 |
|---|---|---|
| DM, 그룹 DM 에서 남이 보낸 것 | 대화마다 하나 | 내가 그 대화에 쓰면 |
| 채널에서 나를 멘션한 것 (`@나`) | 스레드마다 하나 | 내가 그 스레드에 쓰면 |
| 내가 그 메시지에 ✅ 반응 | — | 바로 (답장으로) |
| 보드에서 카드 1 초 누르기 → 치우기 | — | 바로 |

봇 · 앱이 보낸 DM 은 빼는 것이 기본이다 (`--include-bots` 로 포함).
Slackbot 리마인더(`/remind`)는 이벤트로 오지 않아 미응답에 들어가지 않는다.

중계를 켜면 **최근 3 일**의 DM · 멘션을 훑어 켜기 전에 와 있던 미응답도 찾는다 (트레이 → 시작할 때 훑기 에서 끄기 · 1 · 3 · 7 일, 명령줄 `--scan-days N`).

### 혼자 시험하기

내가 쓴 메시지는 모두 "내 답장" 으로 보므로 나에게 DM 을 보내도 카드가 생기지 않는다. `--self-test` 로 실행하면

```bash
.venv/bin/python ping_pet.py --self-test
```

| 나에게 보내는 DM 에 쓴 글 | 결과 |
|---|---|
| `핑 샘플 보드 확인 부탁` ("핑" 으로 시작) | 남이 보낸 DM 처럼 카드가 생긴다 (메시지마다 하나) |
| 그 밖의 글 | 가장 오래된 시험 카드부터 하나씩 답장 처리 → 냠냠 |

---

## 사용

| 하고 싶은 것 | 방법 |
|---|---|
| 대화 열기 | 카드를 짧게 누른다 |
| 처리 중 / 답장 없이 치우기 | 카드를 1 초 누른다 → 창에서 고른다 |
| 글자 크기 · 밝기 · 데모 | 상단 바를 길게 눌러 설정 |
| 화면 캡처 | `python3 firmware/ping-pet/tools/capture.py -o shot.png` |
| 보드 CLI | 보드 USB 포트를 **115200** 으로 연다 (`help`, `demo on`, `inbox info`, `ui font 0~2` …) |

### Slack 에서 처리하면 보드는

보드를 만지지 않아도 Slack 에서 하는 일이 그대로 보드에 반영된다. **내가 한 것만** 본다.

| Slack 에서 | 보드 화면 |
|---|---|
| 누가 DM · 그룹 DM 을 보냄, 채널에서 나를 멘션 | 카드가 생긴다. 답답이가 배고파한다 (가장 오래된 것이 30 분을 넘으면 운다) |
| 그 대화에 답장 (멘션은 **그 스레드에**) | 냠냠 4 초 → 카드가 빠진다. 오늘 답장 수 · 평균 응답 시간에 들어간다 |
| 그 메시지에 ✅ 반응 (`:white_check_mark:` `:heavy_check_mark:` `:ballot_box_with_check:` `:done:`) | 답장한 것과 같다 (냠냠, 카드가 빠진다) |
| 그 메시지에 👀 반응 (`:eyes:`) | **처리 중** : 카드가 흐려져 목록 아래로 간다. 울음 · 배부름 계산에서 빠진다 |
| 👀 반응을 다시 뺌 | 처리 중 해제 : 다시 답장 대기 |
| 처리 중인 것만 남음 | 답답이가 웃는 눈으로 오물거린다 ("하나씩 처리하는 중"), 상단에 "처리 중 N건" |

보드에서 1 초 눌러 고르는 **처리 중 / 치우기** 도 같다. 단 보드에서 고른 처리 중은 Slack 에 반응을 달지 않는다.
보드에서 치운 것은 중계를 다시 켜도 되살아나지 않는다 (치운 뒤 새 글이 오면 다시 카드가 된다).

### USB 포트

보드는 호스트가 연 **보율로 USB 의 주인을 가른다.** 115200 은 CLI, 그 외(중계는 921600)는 중계 링크다. 터미널과 중계는 같은 포트를 동시에 열 수 없다.

---

## 저장소

```
firmware/
├── ping-pet/        ESP-IDF 프로젝트 (보드 펌웨어)
└── docs/            개발 기록 — 보드, 개발 환경, USB, LCD · 터치, LVGL, UI
host/                맥 · PC 중계 (Python)
hardware/            보드 회로도
```

개발 기록은 [firmware/docs](firmware/docs/README.md) 에 단계별로 있다. 알려진 보드 문제(ESP32-P4 rev 3 의 DSI 클럭, ST7701 초기화, GT911 리셋 등)는 [22-lcd-touch.md](firmware/docs/22-lcd-touch.md).

---

## 만든 것들

- [LVGL](https://lvgl.io) 9.6, [ESP-IDF](https://github.com/espressif/esp-idf) 5.5.5, [esp_tinyusb](https://components.espressif.com/components/espressif/esp_tinyusb)
- 폰트 [Jua](https://fonts.google.com/specimen/Jua), [Silkscreen](https://fonts.google.com/specimen/Silkscreen) — SIL Open Font License ([firmware/ping-pet/main/ap/modules/ui/fonts](firmware/ping-pet/main/ap/modules/ui/fonts))
- 보드 초기화 값 참고 : [ESPHome mipi_dsi](https://github.com/esphome/esphome), [ultramcu/guition-jc4880p4-bsp](https://github.com/ultramcu/guition-jc4880p4-bsp)
