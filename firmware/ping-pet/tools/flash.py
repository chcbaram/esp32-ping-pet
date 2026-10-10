#!/usr/bin/env python3
"""USB(HS OTG) 하나로 굽기 — 버튼 없이.

  앱(1209:E324)이 돌고 있으면 DTR/RTS 리셋 신호로 ROM 다운로드 모드에 넣고 (cdc.c 의 cdcLineStateCallback),
  ROM 다운로드 장치(303A:0012)가 다시 열거되면 esptool 로 쓴 뒤 앱으로 리셋한다.

  esptool 혼자서는 이 사이의 USB 재열거를 기다리지 못해 'Device not configured' 로 끝난다.
  그래서 리셋과 쓰기를 나눠서 한다.

  baram-term 이 포트를 열고 있으면 baram-ctl 로 잠시 놓게 한다 (--no-baram 으로 끈다).

  쓰는 법
    개발 (ESP-IDF 환경)     python3 tools/flash.py [--fonts]          # build/ 의 이미지
    릴리즈 (ESP-IDF 없이)   pip install "esptool<5" pyserial
                           python3 flash.py --fw ping-pet-fw-V….bin [--fonts-bin ping-pet-fonts-V….bin]

  평소 업데이트는 중계 앱이 USB OTA 로 한다 (보드 화면에 진행 표시). 이 도구는 처음 굽기, 파티션 표가
  바뀔 때, OTA 를 받지 못하는 옛 펌웨어, 복구용이다.

  릴리즈 펌웨어는 0x0 부터 합친 이미지(tools/release.py)다. 설정이 든 NVS 영역은 건너뛰고 쓴다
  (합친 이미지에는 그 자리가 0xFF 로 채워져 있어 그대로 쓰면 글자 크기 · 밝기 설정이 지워진다).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

from serial import Serial
from serial.tools import list_ports


USB_ID_APP   = (0x1209, 0xE324)   # 앱 (usb_desc.c, pid.codes)
USB_ID_ROM   = (0x303A, 0x0012)   # ROM USB-OTG 다운로드 모드
USB_ID_OLD   = (0x303A, 0x4001)   # pid.codes 로 옮기기 전 앱 (34a5f06 까지)

PROJ_DIR  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(PROJ_DIR, "build")


class FlashError(Exception):
  pass


def col(s, c=36):
  return f"\033[{c}m{s}\033[0m" if sys.stdout.isatty() else s


def find_port(usb_id, serial=None):
  for p in list_ports.comports():
    if (p.vid, p.pid) == usb_id:
      if serial is None or (p.serial_number or "").lower() == serial.lower():
        return p
  return None


def wait_port(usb_id, serial=None, timeout=8.0):
  pre = time.time()
  while time.time() - pre < timeout:
    p = find_port(usb_id, serial)
    if p is not None:
      return p
    time.sleep(0.1)
  return None


class BaramTerm:
  """baram-term 이 포트를 쥐고 있으면 놓게 하고, 끝나면 다시 열게 한다."""

  def __init__(self, port, enable):
    self.port = port
    self.ctl  = shutil.which("baram-ctl") if enable else None
    self.released = False

  def __enter__(self):
    if self.ctl and self.port:
      r = subprocess.run([self.ctl, "--port", self.port, "release"], capture_output=True)
      self.released = (r.returncode == 0)
      if self.released:
        print(f"{col('baram-term')} : 포트를 잠시 놓음")
    return self

  def __exit__(self, *exc):
    if self.released:
      subprocess.run([self.ctl, "--port", self.port, "resume"], capture_output=True)
      print(f"{col('baram-term')} : 포트 다시 엶")


def enter_download(port):
  """esptool 클래식 리셋과 같은 신호. RTS 가 1 → 0 으로 떨어질 때 DTR 이 1 이면 펌웨어가 다운로드 모드로 재시작한다."""
  with Serial(port, 115200) as ser:
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.dtr = True
    ser.rts = False
    time.sleep(0.05)


def esptool_cmd():
  """pip 로 설치한 esptool 을 먼저, 없으면 ESP-IDF 안의 것을 쓴다"""
  try:
    import esptool  # noqa: F401
    return [sys.executable, "-m", "esptool"]
  except ImportError:
    pass
  idf_path = os.environ.get("IDF_PATH")
  if idf_path:
    return [sys.executable, os.path.join(idf_path, "components", "esptool_py", "esptool", "esptool.py")]
  raise FlashError('esptool 이 없다. pip install "esptool<5"  (또는 ESP-IDF 환경에서 실행)')


# esptool 5.x 는 이 보드(USB-OTG 다운로드)에서 큰 조각을 쓰다 'No more data to read' 로 끊긴다. 4.x 를 쓴다.
# 같은 프로세스에서 esptool.main() 을 부르면 쓰는 도중 'Invalid head of packet' 으로 끊긴다. 따로 실행한다.
def esptool_run(argv):
  if subprocess.run(esptool_cmd() + argv).returncode != 0:
    raise FlashError("esptool 실패")


def build_images():
  """build/flash_args → [주소, 절대 경로, ...] (첫 줄의 --flash_mode 등은 빼고 이미지 머리를 그대로 쓴다)"""
  out = []
  for line in open(os.path.join(BUILD_DIR, "flash_args")):
    c = line.split()
    if len(c) == 2 and c[0].startswith("0x"):
      out += [c[0], os.path.join(BUILD_DIR, c[1])]
  return out


# 파티션 위치. 릴리즈 폴더에는 partitions.csv 가 없으므로 기본값을 둔다 (partitions.csv 와 같게)
PART_DEFAULT = {"nvs": (0x9000, 0x4000), "fonts": (0x810000, 0x300000)}


def partition(name):
  """partitions.csv 의 (offset, size). 없으면 기본값"""
  path = os.path.join(PROJ_DIR, "partitions.csv")
  if os.path.isfile(path):
    for line in open(path):
      c = [x.strip() for x in line.split("#")[0].split(",")]
      if len(c) >= 5 and c[0] == name:
        size = c[4]
        size = int(size[:-1]) << 20 if size.endswith("M") else int(size[:-1]) << 10 if size.endswith("K") else int(size, 0)
        return int(c[3], 0), size
  return PART_DEFAULT[name]


def fonts_offset():
  return partition("fonts")[0]


def fw_segments(path, tmp):
  """합친 이미지를 NVS 를 뺀 조각으로 나눈다 → [주소, 파일, ...]"""
  data = open(path, "rb").read()
  nvs_off, nvs_size = partition("nvs")
  start = 0
  while start < len(data) and data[start:start + 0x1000] == b"\xff" * min(0x1000, len(data) - start):
    start += 0x1000                       # 앞의 빈 칸 (0x0 ~ 부트로더 0x2000) 은 쓰지 않는다
  if data[0x2000:0x2001] != b"\xe9":
    raise FlashError(f"{path} : 0x2000 에 부트로더가 없다. tools/release.py 로 만든 합친 이미지인지 확인")

  out = []
  for a, b in ((start, nvs_off), (nvs_off + nvs_size, len(data))):
    if b <= a:
      continue
    f = os.path.join(tmp, f"fw_{a:06x}.bin")
    open(f, "wb").write(data[a:b])
    out += [hex(a), f]
  return out


def fonts_build():
  sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
  import fonts
  return fonts.build(os.path.join(BUILD_DIR, "fonts.bin"))


def flash(fw=None, fonts_bin=None, fonts=False, port=None, baud=460800, compress=False, baram=True,
          out=print):
  """굽는다. fw 가 없으면 build/ 의 이미지. 실패하면 FlashError"""
  if fw is None and not os.path.isfile(os.path.join(BUILD_DIR, "flash_args")):
    raise FlashError("build/flash_args 가 없다. 먼저 idf.py build  (릴리즈 파일이면 --fw)")
  for f in (fw, fonts_bin):
    if f and not os.path.isfile(f):
      raise FlashError(f"{f} 가 없다")

  # esptool 이 없으면 다운로드 모드에 넣기 전에 멈춘다 (넣고 나서 실패하면 보드가 꺼진 채 남는다)
  esptool_cmd()

  p = find_port(USB_ID_APP) or find_port(USB_ID_OLD) or find_port(USB_ID_ROM)
  if port:
    p = next((x for x in list_ports.comports() if x.device == port), p)
  if p is None:
    raise FlashError("보드를 찾지 못했다 (1209:E324 앱 / 303A:0012 ROM)")

  serial = p.serial_number
  out(f"{col('board')}      : {p.device}  {p.vid:04X}:{p.pid:04X}  SN={serial}")

  tmp = tempfile.mkdtemp(prefix="ping-pet-")
  try:
    with BaramTerm(p.device, baram):
      if (p.vid, p.pid) != USB_ID_ROM:
        out(f"{col('reset')}      : 다운로드 모드로")
        enter_download(p.device)
        rom = wait_port(USB_ID_ROM, serial)
        if rom is None:
          raise FlashError("ROM 다운로드 장치(303A:0012)가 나타나지 않았다")
      else:
        rom = p
      out(f"{col('download')}   : {rom.device}")

      images = fw_segments(os.path.abspath(fw), tmp) if fw else build_images()
      if fonts_bin:
        images += [hex(fonts_offset()), os.path.abspath(fonts_bin)]
      elif fonts:
        images += [hex(fonts_offset()), fonts_build()]

      # USB-OTG 다운로드에서 압축 쓰기는 이미지가 커지면 (약 3 MB, 폰트 포함) 스텁이
      #   'Failed to write compressed data to flash after seq 575 (result was C900: Too much data)'
      # 로 멈춘다. 압축하지 않으면 된다. HS USB 라 속도 차이는 크지 않다.
      esptool_run(["--chip", "esp32p4", "-p", rom.device, "-b", str(baud),
                   "--before", "no_reset", "--after", "hard_reset",
                   "write_flash"] + ([] if compress else ["--no-compress"]) + images)

      app = wait_port(USB_ID_APP, serial)
      if app is None:
        raise FlashError("앱(1209:E324)이 다시 나타나지 않았다")
      out(f"{col('app')}        : {app.device}")
      time.sleep(0.5)
  finally:
    shutil.rmtree(tmp, ignore_errors=True)


def main():
  ap = argparse.ArgumentParser(description="USB(HS OTG) 로 버튼 없이 굽기")
  ap.add_argument("--port", help="시리얼 포트. 없으면 VID/PID 로 찾는다")
  ap.add_argument("--baud", type=int, default=460800)
  ap.add_argument("--no-baram", action="store_true", help="baram-term 포트 놓기/다시 열기를 하지 않는다")
  ap.add_argument("--compress", action="store_true", help="압축해서 쓴다 (기본은 압축하지 않는다)")
  ap.add_argument("--fonts", action="store_true", help="폰트 파티션도 쓴다 (tools/fonts.py 로 만든다). 폰트가 바뀔 때만")
  ap.add_argument("--fw", metavar="BIN", help="릴리즈 펌웨어 (합친 이미지). 없으면 build/ 의 이미지")
  ap.add_argument("--fonts-bin", metavar="BIN", help="릴리즈 폰트 이미지")
  args = ap.parse_args()

  try:
    flash(fw=args.fw, fonts_bin=args.fonts_bin, fonts=args.fonts, port=args.port, baud=args.baud,
          compress=args.compress, baram=not args.no_baram)
  except FlashError as e:
    sys.exit(str(e))


if __name__ == "__main__":
  main()
