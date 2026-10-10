# 27. USB HS CDC — CLI 와 버튼 없는 다운로드

> HS USB 커넥터 하나로 CLI · 로그 · 굽기를 한다. 나중에 맥 중계(cmd)와 웹 설정(HID)도 같은 커넥터로 간다.
> 관련: [01](01-board.md) 2절 (커넥터 두 개), [21](21-uart-cli.md)
>
> **상태: 보드에서 확인 (2026-10-10).** HS 열거, CDC CLI, DTR/RTS 로 ROM 다운로드 진입, `tools/flash.py` 로 굽기(약 8 초).
> CLI `boot` 명령은 **시험하지 않았다.**

---

## 1. 쓰는 법

```bash
python3 tools/flash.py      # 버튼 없이 굽기
```

CLI 는 HS 포트를 115200 으로 연다. `usb info` 는 키를 누를 때까지 갱신된다.

---

## 2. 장치

| 인터페이스 | 엔드포인트 | |
|---|---|---|
| ITF0 CDC Comm | `0x81` notif | |
| ITF1 CDC Data | `0x02` OUT / `0x82` IN (FS 64 B, **HS 512 B**) | CLI 또는 cmd |
| (ITF2 HID) | (`0x03` / `0x83`) | 다음 단계. 웹 설정 채널, usage page `0xFF00` |

| VID:PID | 구성 | |
|---|---|---|
| **`1209:E324`** | CDC | 앱 (지금) |
| `1209:E325` | CDC + HID | 예약 (웹 설정 채널을 붙일 때) |
| `1209:E326` ~ `E327` | — | 예약 |
| `303A:0012` | — | **ROM USB 다운로드 모드** (칩이 정한다) |

- VID `0x1209` 는 pid.codes. 내 저장소에서 쓰는 PID 는 `B563`~`B566`(stm32h563-core), `B750`~`B752`(weact-h750) 뿐이고,
  `E324`~`E327` 은 레지스트리에서도 비어 있다 (`gh api repos/pidcodes/pidcodes.github.com/contents/1209/E324` → 404, 등록된 `B010` 은 파일이 나온다)
- h563 규칙대로 **PID 는 USB 구성을 따른다.** HID 를 붙이면 `E325` 로 바꾼다
- 처음에는 Espressif 예제 `303A:4001` 이었다 (`34a5f06` 까지)
- **시리얼은 MAC** (`e8:f6:0a:e8:b4:1d`). ROM 다운로드 장치와 같은 형식이라 macOS 포트 이름이 앱 · ROM 모두 `usbmodem114101` 로 같다.
  PID 를 `303A:4001` → `1209:E324` 로 바꿨을 때도 포트 이름은 그대로였다

디스크립터는 `usb_desc.c` 가 esp_tinyusb 설정 구조체(`tinyusb_desc_config_t`)에 채운다. HS 장치라 qualifier 와 HS 구성 디스크립터가 따로 있다.

---

## 3. CDC 의 주인 — 호스트가 연 보율로 가른다

stm32h563-core 와 같다.

| 호스트가 연 보율 | CDC 의 주인 |
|---|---|
| **115200** | CLI. cli 모듈이 CLI · 로그 포트를 CDC 로 옮긴다 |
| 그 외 | cmd (맥 중계 스크립트, 다음 단계). CLI 는 UART0 에 남는다 |
| 닫힘 | CLI 는 UART0 로 돌아온다 |

---

## 4. 버튼 없이 ROM 다운로드

### 왜 필요한가

esp_tinyusb 에는 다운로드 모드로 가는 옵션이 없다. IDF 의 USB CDC 콘솔 자동 리셋(`ESP_CONSOLE_USB_CDC`)은 **S2/S3 전용**이고
ROM CDC 를 쓰는 방식이라 TinyUSB 와 같이 쓸 수 없다 (`esp_system/Kconfig` : `depends on (IDF_TARGET_ESP32S2 || IDF_TARGET_ESP32S3) && !TINY_USB`).

### 구현

P4 에도 강제 다운로드 부팅 비트가 있다. 세우고 재시작하면 BOOT 버튼을 누른 것과 같다. LP 영역 레지스터라 소프트웨어 리셋 뒤에도 남는다.

```c
REG_SET_BIT(LP_SYSTEM_REG_SYS_CTRL_REG, LP_SYSTEM_REG_FORCE_DOWNLOAD_BOOT);
esp_restart();
```

`cdc.c` 의 라인 상태 콜백이 esptool 클래식 리셋 시퀀스를 본다. IDF S2/S3 `usb_console.c` 와 같은 판정이다.

| RTS 1 → 0 일 때 | IDF S2/S3 | 여기 |
|---|---|---|
| DTR = 1 | 다운로드 모드 | **다운로드 모드** |
| DTR = 0 | 일반 리셋 | **아무것도 안 한다** — macOS 에서 포트를 닫을 때마다 보드가 재시작하지 않게 |

플래시 뒤 앱으로 돌아가는 리셋은 ROM 스텁이 워치독으로 한다 (`Hard resetting with a watchdog`).

콜백은 USB 태스크 문맥이라 `esp_timer` 로 50 ms 뒤에 재시작한다. CLI `boot` 도 같은 함수를 부른다.

### `tools/flash.py`

esptool 은 리셋 뒤 USB 가 다시 열거되는 것을 기다리지 못한다.

```
Connecting.....
A serial exception error occurred: Could not configure port: (6, 'Device not configured')
```

(이때 보드는 이미 다운로드 모드에 들어가 있다.) 그래서 나눠서 한다.

1. baram-term 이 포트를 쥐고 있으면 `baram-ctl release`
2. 앱(`1209:E324`)이면 DTR/RTS 리셋 신호
3. ROM 장치(`303A:0012`, 같은 시리얼)가 나타날 때까지 기다린다
4. `esptool --before no_reset --after hard_reset write_flash @flash_args`
5. 앱이 다시 나타나는 것을 확인, `baram-ctl resume`

```
board      : /dev/cu.usbmodem114101  1209:E324  SN=e8:f6:0a:e8:b4:1d
baram-term : 포트를 잠시 놓음
reset      : 다운로드 모드로
download   : /dev/cu.usbmodem114101
app        : /dev/cu.usbmodem114101
baram-term : 포트 다시 엶
```

---

## 5. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| 첫 빌드 펌웨어가 HS 포트에 안 보임 | 처음 커넥터가 USB-Serial-JTAG 였다 | 다른 커넥터가 HS ([01](01-board.md) 2절) |
| `sdkconfig` 의 `TINYUSB_DESC_*` 문자열이 적용됐는지 불확실 | esp_tinyusb 2.x 는 기본 디스크립터를 경고와 함께 쓴다 | 디스크립터를 코드(`usb_desc.c`)로 직접 지정 |
| `idf.py flash` 가 `Device not configured` | esptool 이 USB 재열거를 못 기다림 | `tools/flash.py` |
| 포트 이름 `usbmodem1234561` | TinyUSB 기본 시리얼 `123456` | 시리얼을 MAC 으로 |

---

## 6. 남은 것

- [ ] CLI `boot` 명령 시험
- [ ] HID (웹 설정 채널) — PID `E325`, usage page `0xFF00`, h563 `usb_hid.c` · `drv_hid.c`
- [ ] cmd 채널 — 115200 이 아닌 보율의 CDC 를 맥 중계 스크립트가 쓴다
- [ ] pid.codes 에 `1209:E324` ~ `E327` 등록 (사용자)
- [ ] Windows 에서 CDC 가 드라이버 없이 붙는지
