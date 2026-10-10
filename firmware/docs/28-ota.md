# 28. USB OTA — 중계 앱에서 펌웨어 · 폰트 업데이트

> 맥 · PC 중계가 USB 링크(줄 단위 JSON)로 이미지를 보내고, 돌고 있는 펌웨어가 받아 쓴다.
> 보드 화면에 진행을 보여 주고, 실패하면 지금 펌웨어로 계속 돈다. esptool · 다운로드 모드가 필요 없다.
> 관련: [27](27-usb.md) (ROM 다운로드, 처음 굽기), [24](24-ui.md) (update 앱)
>
> **상태: 보드에서 확인 (2026-10-10).** 트레이 메뉴로 앱 R2 → R6 (약 11 초), 폰트 + 앱 (16 + 11 초).
> **롤백(새 펌웨어가 정상 표시 전에 리셋되면 되돌리기)은 시험하지 않았다.**

---

## 1. 쓰는 법

```bash
cd firmware/ping-pet
idf.py build
python3 tools/release.py          # dist/ping-pet-fw-<버전>.bin, ping-pet-fonts-<버전>.bin
```

중계 트레이 → **보드 펌웨어 업데이트** → `<버전> 로 업데이트` (또는 `+ 폰트 업데이트`).
중계는 `dist/` 에서 가장 새 파일을 쓴다 (묶은 앱은 앱 안의 `firmware/`). 버전은 `hw_def.h` 의 `_DEF_FIRMWATRE_VERSION`.

`tools/flash.py` (esptool) 는 처음 굽기, 파티션 표 · 부트로더가 바뀔 때, OTA 를 받지 못하는 옛 펌웨어, 복구용으로 남겼다.

---

## 2. 파티션 표

```
nvs       0x009000   16 KB
otadata   0x00d000    8 KB    어느 앱 자리로 부팅할지
phy_init  0x00f000    4 KB
ota_0     0x010000    4 MB    앱 (처음 굽기는 여기)
ota_1     0x410000    4 MB    앱
fonts     0x810000    3 MB    폰트 (자리는 그대로)
```

앱 한 자리(8 MB)를 두 자리로 나눴다. 앱은 약 1.0 MB. **파티션 표가 바뀌었으므로 이 펌웨어로 처음 갈 때는 `flash.py` 로 한 번 굽는다** (부트로더도 롤백 설정으로 바뀐다).

---

## 3. 링크 메시지 (`service/link/link.c`)

| 방향 | 메시지 | |
|---|---|---|
| 맥 → 보드 | `hello {}` | 보드에 hello 를 청한다 (아래 6 절) |
| 보드 → 맥 | `hello {fw, ota: 1}` | `ota` 가 없으면 옛 펌웨어 → 중계가 업데이트 메뉴를 막는다 |
| 맥 → 보드 | `ota_begin {target: app \| fonts, size, sha256, version}` | → `ota_ready` / `ota_error {msg}` |
| 맥 → 보드 | `ota_data {off, data}` | 원본 2 KB, base64 2732 B. → `ota_ack {off: 다음 위치}` |
| 맥 → 보드 | `ota_end {}` | sha256 확인, 앱이면 이미지 확인 · 부팅 자리 바꾸기 → `ota_done` |
| 맥 → 보드 | `ota_abort {}` | 그만두기 |

- **한 조각씩 주고받는다.** 줄 하나(4 KB)와 CDC 수신 큐(4 KB)에 조각 하나만 들어가도록 2 KB 로 정했다. 앱 1 MB 가 약 11 초.
- 앱은 합친 릴리즈 이미지에서 `0x10000` 부터 잘라 보낸다. 파일을 따로 두지 않는다.
- 폰트와 앱을 함께 보낼 때는 폰트 → (보드 다시 시작, 다시 연결) → 앱.
- 10 초 동안 데이터가 없거나 링크가 끊기면 보드가 그만둔다.

---

## 4. 보드 (`service/ota/ota.c`)

| 대상 | 쓰는 곳 | 다 받으면 |
|---|---|---|
| app | `esp_ota_get_next_update_partition()` — 지금 돌지 않는 자리. `OTA_WITH_SEQUENTIAL_WRITES` 로 쓰면서 지운다 | `esp_ota_end` (이미지 확인) → `esp_ota_set_boot_partition` → 1.5 초 뒤 다시 시작 |
| fonts | fonts 파티션. 64 KB 씩 지우며 쓴다 | 매직을 쓰고 다시 시작 |

**롤백** : `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. 새 펌웨어가 10 초(`OTA_VALID_MS`) 돌면 `esp_ota_mark_app_valid_cancel_rollback()`. 그 전에 리셋되면 부트로더가 이전 자리로 되돌린다. **시험하지 않았다.**
업데이트 직후 10 초 안에 전원을 끄면 이전 펌웨어로 돌아갈 수 있다.

**폰트는 매직을 마지막에 쓴다.** 폰트를 쓰는 동안 끊기면 반쯤 쓴 폰트가 남는다. 첫 조각의 매직(`PPFT`) 4 B 는 0xFF 로 두고, 다 받고 sha256 이 맞으면 그 자리에 쓴다 (지우지 않고 1 → 0 으로만 쓰므로 된다).
매직이 없으면 다음 부팅에서 `ui_theme.c` 가 폰트 파티션을 쓰지 않는다. 폰트를 지우기 시작한 뒤 실패하면 지금 화면이 쓰는 폰트 데이터가 깨졌으므로 6 초 뒤 다시 시작한다.

CLI : `ota info` — 지금 자리, 다음 자리, 진행 상태.

---

## 5. 보드 화면 (`ui/app/update/update.c`)

`ui.c` 가 `otaIsActive()` 를 보고 update 앱으로 바꾸고, 끝나면 update 앱이 기본 app 으로 돌아간다.

- 왼쪽 : 답답이 `PET_STATE_UPDATE` — 웃는 눈 + 오물거리는 입 (펌웨어를 먹는 중). 실패하면 울음
- 오른쪽 : 제목, `지금 V… / 새 V…`, 진행 막대, 퍼센트, 안내 ("USB 를 뽑거나 전원을 끄지 마세요" / "다시 시작합니다" / 실패 이유)
- **폰트 업데이트 중에는 LVGL 내장 Montserrat 로 영문만 쓴다.** Jua · Silkscreen 은 지금 지우고 있는 파티션을 읽기 때문이다. Jua 에 화살표 글자가 없어 버전은 `지금 / 새` 로 쓴다

---

## 6. 막혔던 것

### 6.1 OTA 중 화면이 심하게 깜빡임

플래시를 지우고 쓰는 동안 캐시가 멈추면서 DPI 인터럽트도 멈췄다. DPI 프레임버퍼 두 장은 프레임마다 인터럽트에서 DMA 를 다시 걸므로, 인터럽트가 늦으면 화면이 깨진다.
`CONFIG_LCD_DSI_ISR_CACHE_SAFE=y` 로 인터럽트가 캐시 없이도 돌게 하고, 등록한 `on_vsync` 콜백(`lcdOnVsync`)을 `IRAM_ATTR` 로 옮겨 해결했다. 더블 버퍼링 문제는 아니었다 (교체는 vsync 를 기다린다, `lcd.c`).
NVS 에 설정을 저장할 때의 순간 깜빡임도 같은 원인이었을 것이다 (확인하지 않았다).

**OTA 중에 깜빡이는지는 받는 펌웨어가 아니라 지금 돌고 있는 펌웨어가 정한다.** 고친 펌웨어(R5)로 한 번 올린 뒤의 업데이트(R5 → R6)에서 확인했다.

### 6.2 250 초 걸림

중계가 `ser.read(256)` 으로 읽어, 30 B 쯤인 `ota_ack` 마다 timeout(0.5 초)까지 기다렸다 (495 조각 × 0.5 초). `ser.read(ser.in_waiting or 1)` 로 바꿔 11 초.

### 6.3 보드가 먼저 보내는 hello 가 오지 않음

보드는 링크가 열리면 hello 를 보내지만 맥 중계에는 한 번도 닿지 않았다 (포트를 여는 사이에 사라지는 것으로 본다. 원인은 확인하지 않았다). 중계가 연결 직후 `hello {}` 를 보내 청하고, 보드가 답하게 했다.

### 6.4 esptool (flash.py)

처음 굽기 · 복구에 쓰는 `flash.py` 에서 겪은 것.

- **esptool 5.x** 는 이 보드(USB-OTG ROM 다운로드)에서 큰 조각을 쓰다 `No more data to read from the serial port` 로 끊긴다. 4.x(ESP-IDF 5.5 와 같은 4.12)를 쓴다 → `pip install "esptool<5"`
- **같은 프로세스에서 `esptool.main()`** 을 부르면 쓰는 도중 `Invalid head of packet (0x01)` 로 끊긴다. 따로 실행하면 된다
- **esptool 을 다운로드 모드에 넣은 뒤에 찾으면** 없을 때 보드가 다운로드 모드(화면 꺼짐)에 남는다. 먼저 확인한다
- 실패해 다운로드 모드에 남은 보드는 `python -m esptool --chip esp32p4 -p <포트> --before no_reset --after hard_reset chip_id` 로 다시 시작된다. USB 에서 사라졌으면 BOOT 를 누른 채 RST

---

## 7. 확인하지 못한 것

- 롤백 (새 펌웨어가 10 초 안에 리셋될 때)
- 업데이트 도중 USB 를 뽑았을 때 (앱 : 지금 펌웨어로 계속, 폰트 : 다시 시작해 내장 폰트로 — 코드로만 확인)
- Windows · Linux 중계에서의 OTA
