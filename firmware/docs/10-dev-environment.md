# 10. 개발 환경

> macOS 기준. ESP-IDF 는 툴 폴더 규칙(`~/hdd/tools`)에 둔다.
>
> **상태: 확인 (2026-10-10).** 설치, 빌드, `tools/flash.py` 로 굽기, baram-term 연동.

---

## 1. ESP-IDF

| 항목 | 값 |
|---|---|
| 버전 | **v5.5.5** |
| 소스 (`IDF_PATH`) | `~/hdd/tools/esp/esp-idf-v5.5.5` |
| 툴체인 (`IDF_TOOLS_PATH`) | `~/hdd/tools/esp/.espressif` (riscv32-esp-elf gcc 14.2.0) |
| 설치 | `git clone --depth 1 -b v5.5.5 --recursive --shallow-submodules`, `./install.sh esp32p4` |

`~/.zshrc` 별칭:

```bash
alias get_idf="export IDF_TOOLS_PATH=$HOME/hdd/tools/esp/.espressif; export IDF_PATH=$HOME/hdd/tools/esp/esp-idf-v5.5.5;. $HOME/hdd/tools/esp/esp-idf-v5.5.5/export.sh"
alias get_idf5.5="..."   # 같은 내용
```

### 왜 v5.5

| | v5.5.5 | v6.x |
|---|---|---|
| 컴포넌트 | `esp_lvgl_port`, `esp_lcd_st7701`, `esp_lcd_touch_gt911`, P4 예제 · 커뮤니티 설정이 대부분 v5.4~5.5 기준 | API 가 깨지는 변경이 있었다 (6.0) |
| 칩 리비전 | rev < 3 · rev 3 둘 다 (설정으로 고른다) | 기본이 rev 3 |

처음에는 엔지니어링 샘플(rev < 3)로 알고 골랐는데, 보드는 rev v3.2 였다 ([01](01-board.md)). v5.5 는 rev 3 도 지원하므로 그대로 쓴다.

---

## 2. 빌드 · 굽기

```bash
get_idf
cd firmware/ping-pet
idf.py build
python3 tools/flash.py
```

`tools/flash.py` 가 하는 일 → [27](27-usb.md) 4절. `idf.py flash` 는 리셋 뒤 USB 재열거를 기다리지 못해 실패한다.

보드가 다운로드 모드에 들어가지 않을 때 (앱이 죽었거나 USB 를 안 여는 이미지일 때):
**BOOT 를 누른 채 RST 를 눌렀다 떼고, 1 초 뒤 BOOT 를 뗀다.** 그 다음 `tools/flash.py` 는 ROM 장치를 바로 찾아 쓴다.

---

## 3. baram-term

CLI 는 baram-term 으로 HS USB 포트를 **115200** 으로 연다 (CDC 주인 판정 → [27](27-usb.md) 3절).
Claude 가 명령을 보낼 때도 포트를 직접 열지 않고 `baram-ctl` 로 보낸다.

```bash
baram-ctl list
baram-ctl --match e8:f6:0a:e8:b4:1d send "module info" --until 'cli# $' --timeout 5
```

포트 경로 대신 **USB 시리얼(MAC)로 찾는다.** 앱과 ROM 다운로드 모드의 시리얼이 같아서 포트 이름도 같게 나온다.

---

## 4. 막혔던 지점

| 증상 | 원인 | 해결 |
|---|---|---|
| `rm -rf` 가 Claude 자동 모드에서 막힘 | 되돌릴 수 없는 삭제로 분류 | 커밋된 파일은 `git rm` 으로 지운다 |
| 쉘 변수에 담은 `--match X` 가 인자 하나로 넘어감 | zsh 는 변수를 단어로 쪼개지 않는다 | 인자를 직접 쓰거나 `"$M"` 을 값에만 쓴다 |
