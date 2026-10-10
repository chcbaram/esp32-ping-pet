# 21. UART + 로그 + CLI

> 부팅 로그와 CLI. 이후 모든 기능은 로그로 상태를 보고 CLI 로 시험한다.
> 관련: [27](27-usb.md) (CDC 를 CLI 로 쓰기)
>
> **상태: 보드에서 확인 (2026-10-10).** CDC 로 `help` / `module info` / `uart info` / `log boot` 동작.
> UART0 쪽 입출력은 커넥터를 확인하지 않아 **보드에서 시험하지 않았다.**

---

## 1. 채널

| 채널 | 장치 | 쓰임 |
|---|---|---|
| `_DEF_UART1` (`HW_UART_CH_DEBUG`) | UART0, GPIO37 TX / GPIO38 RX ⚠️ | 기본 CLI · 로그 채널. IDF 콘솔과 같은 UART |
| `_DEF_UART2` (`HW_UART_CH_USB`) | USB CDC (`cdc_uart_driver`) | 호스트가 115200 으로 열면 CLI · 로그가 여기로 옮겨 온다 |

```
cli# uart info
_DEF_UART1 : UART0 DEBUG  , 115200 bps, rx 0, tx 457
_DEF_UART2 : USB CDC      , 115200 bps, rx 59, tx 1265
```

---

## 2. uart 드라이버 구조

stm32h563-core `uart.h` 의 `uart_driver_t` 를 그대로 쓴다. 채널에 드라이버를 꽂으면 HW UART 대신 그 콜백으로 입출력한다.

```c
uartSetDriver(HW_UART_CH_USB, &cdc_uart_driver);   // cli 모듈 init 에서
```

cli · log 는 uart 함수만 부르므로 CDC 인지 UART 인지 모른다.

`uart.c` 는 h563 의 구조(채널 표, 드라이버 훅, rx/tx 카운터, 4 KB 수신 버퍼)를 두고 하드웨어 부분만 ESP-IDF UART 드라이버로 바꿨다.

| | h563 | 여기 |
|---|---|---|
| 수신 | GPDMA 원형 버퍼 + 폴링 | `uart_driver_install` 링 버퍼 4 KB, `uart_get_buffered_data_len` |
| 송신 | `HAL_UART_Transmit` 블로킹 | `uart_write_bytes` (송신 링 버퍼 4 KB) |
| 보율 바꾸기 | 수신 DMA 만 멈추고 UART 다시 | `uart_set_baudrate` |
| 콘솔과 공유 | — | IDF 콘솔이 같은 UART0 을 쓴다. 드라이버가 이미 설치돼 있으면 다시 설치하지 않는다 |

---

## 3. 로그

h563 `log.c` 그대로 — 링 버퍼(가득 차면 오래된 것부터 덮음), 줄마다 `[  초.밀리초]`, `log boot` / `log list`.
CDC 는 호스트가 열기 전에 찍힌 로그를 볼 수 없으므로 `log boot` 로 다시 본다.

```
cli# log boot
[    0.055]	[ Firmware Begin... ]
[    0.055]	Booting..Name 		: PING-PET
[    0.055]	Booting..Clock		: 400 Mhz
...
[    0.225]	[OK] usbInit()
[    0.225]	     SN  e8:f6:0a:e8:b4:1d
[    0.226]	[OK] moduleInit()
```

`_write` (printf 를 로그로) 는 ESP-IDF 에서 빼냈다 → [11](11-project-skeleton.md) 3절. `printf` / `ESP_LOGx` 는 IDF 콘솔(UART0)로 간다.

---

## 4. 남은 것

- [ ] UART0 커넥터 위치 확인, UART0 CLI 시험
