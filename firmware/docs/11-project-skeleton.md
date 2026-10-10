# 11. ping-pet 프로젝트 골격

> `firmware/ping-pet` — JC4880P443C 용 ESP-IDF 프로젝트
> 참고: `~/hdd/git/stm32h563-core/firmware/core/stm32h5-fw` (2026-10-03)
>
> **상태: 보드에서 확인 (2026-10-10).** 모듈 2 개(usb, cli) 등록 · 초기화, CLI, 로그.

---

## 1. 거쳐 온 길

| 커밋 | 골격 |
|---|---|
| `83739ac` | `esp32-p4-lcd` 의 예전 골격 (ap/bsp/common/hw, `main.cpp`, 구버전 cli · log · uart) |
| `34a5f06` | **stm32h563-core 구조로 교체** — 공용 계층 최신, uart 드라이버, USB 분리, 모듈 시스템, `main.c` / `ap.c` |

---

## 2. 디렉터리 구조

```
firmware/ping-pet/
├── CMakeLists.txt                 project(ping-pet)
├── sdkconfig.defaults             P4 rev 3, 400 MHz, PSRAM, 16 MB, 파티션, TinyUSB CDC
├── partitions.csv                 앱 8 MB, fonts 3 MB (data 0x40)
├── tools/
│   ├── flash.py                   버튼 없이 굽기 ([27](27-usb.md)), --fonts
│   ├── fonts.py                   TTF 묶음 → fonts.bin ([24](24-ui.md) 5절)
│   └── capture.py                 화면 캡처 → PNG
└── main/
    ├── CMakeLists.txt             GLOB, WHOLE_ARCHIVE, -Wno-expansion-to-defined
    ├── idf_component.yml          esp_tinyusb ~2.4.0
    ├── main.c / main.h            app_main → bspInit, hwInit, apInit, apMain
    ├── ap/
    │   ├── ap.c / ap.h / ap_def.h
    │   └── modules/
    │       ├── module.c / module.h        MODULE_DEF 자기 등록
    │       ├── common/
    │       │   ├── cli/cli.c              CLI 포트 전환 (UART0 ↔ CDC)
    │       │   └── usb/usb_task.c         usbInit + cdcInit
    │       ├── inbox/                     미응답 목록 모델, 데모 모드 ([24](24-ui.md))
    │       └── ui/                        테마, 런처, app (pingpet, settings), 폰트 ([24](24-ui.md))
    ├── bsp/bsp.c / bsp.h          delay, millis, micros, CPU 클럭
    ├── common/                    ← stm32h563-core 그대로
    │   ├── def.h, err_code.h, evt_code.h
    │   ├── core/qbuffer, util_core
    │   └── hw/include/{cdc,cli,log,uart}.h, hw/src/cli.c
    └── hw/
        ├── hw.c / hw.h / hw_def.h
        └── driver/
            ├── log.c              ← h563 (ESP 에서는 _write 제외)
            ├── uart.c             UART0 + 드라이버 훅 (ESP 로 다시 씀)
            ├── cdc.c              esp_tinyusb CDC + cdc_uart_driver
            ├── usb/usb.c, usb.h, usb_desc.c
            ├── i2c.c, nvs.c
            ├── lcd.c, lcd/st7701.c        ([22](22-lcd-touch.md))
            ├── touch.c, touch/gt911.c     ([22](22-lcd-touch.md))
            └── lvgl.c, lvgl/lv_port_*.c   ([23](23-lvgl.md))
```

---

## 3. stm32h563-core 와 달라진 점

| | h563 | 여기 | 이유 |
|---|---|---|---|
| 모듈 등록 | `.module` 링커 섹션 + `_smodule`/`_emodule` | **`constructor` 함수가 `moduleAdd()`** | ESP-IDF 는 링커 스크립트를 생성한다. 쓰는 쪽(`MODULE_DEF`)은 같다 |
| 링크 | — | main 컴포넌트 `WHOLE_ARCHIVE` | 아무도 참조하지 않는 모듈 파일을 링커가 아카이브에서 빼지 않게 |
| `tud_task()` | `usbUpdate()` 가 메인 루프에서 | **esp_tinyusb 태스크** | `usbUpdate()` 는 빈 함수 |
| TinyUSB 콜백 | 직접 정의 | esp_tinyusb 가 갖는다 | 디스크립터는 설정 구조체로, CDC 수신은 esp_tinyusb 콜백 → qbuffer |
| `log.c` `_write` | printf 를 로그 채널로 | **빼냈다 (`#ifndef ESP_PLATFORM`)** | ESP-IDF 는 newlib `_write` 를 VFS(콘솔)가 갖는다. 중복 정의 |
| `apMain()` | `moduleUpdate()` 만 | `moduleUpdate(); delay(1);` | FreeRTOS 태스크라 양보해야 IDLE 워치독이 걸리지 않는다 |
| `.version` / TAG | 있음 (부트로더가 본다) | 없음 | IDF 2 단 부트로더와 앱 헤더가 대신한다 |
| 경고 | — | `-Wno-expansion-to-defined` | `CLI_USE(x)` 가 매크로 안에서 `defined` 를 펼친다. GCC 에서 의도대로 동작 |

---

## 4. 모듈 시스템

```c
MODULE_DEF(cli)
{
  .name     = "cli",
  .priority = MODULE_PRI_NORMAL,
  .init     = cliModuleInit,
  .update   = cliModuleUpdate,
};
```

`MODULE_DEF(x)` 는 디스크립터를 선언하고, `app_main()` 전에 도는 `constructor` 가 `moduleAdd()` 로 표에 올린다.
`moduleInit()` 이 우선순위 순으로 `init()` 을 부르고, `moduleUpdate()` 가 등록 순으로 `update()` 를 부른다.

```
cli# module info        (2 단계 당시. 지금은 usb, inbox, cli, ui)
count : 2
idx  name             pri  update init
0    usb              1    yes    OK
1    cli              6    yes    OK
```

UI app 도 같은 방식이다 — `APP_DEF(x)` 의 constructor 가 `launcherAddApp()` ([24](24-ui.md) 1 절).

---

## 5. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| `TINYUSB_DEFAULT_CONFIG` implicit declaration | esp_tinyusb 2.x 는 별도 헤더 | `tinyusb_default_config.h` include |
| `this use of "defined" may not be portable` 다수 | `CLI_USE()` 매크로 | 경고만 끔 (코드는 h563 그대로) |
