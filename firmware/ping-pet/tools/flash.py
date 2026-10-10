#!/usr/bin/env python3
"""USB(HS OTG) 하나로 굽기 — 버튼 없이.

  앱(1209:E324)이 돌고 있으면 DTR/RTS 리셋 신호로 ROM 다운로드 모드에 넣고 (cdc.c 의 cdcLineStateCallback),
  ROM 다운로드 장치(303A:0012)가 다시 열거되면 esptool 로 쓴 뒤 앱으로 리셋한다.

  esptool 혼자서는 이 사이의 USB 재열거를 기다리지 못해 'Device not configured' 로 끝난다.
  그래서 리셋과 쓰기를 나눠서 한다.

  baram-term 이 포트를 열고 있으면 baram-ctl 로 잠시 놓게 한다 (--no-baram 으로 끈다).

  쓰는 법 (ESP-IDF 환경에서):
    python3 tools/flash.py              # build/ 의 이미지를 쓴다
    python3 tools/flash.py --port /dev/cu.usbmodem114101
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

from serial import Serial
from serial.tools import list_ports


USB_ID_APP   = (0x1209, 0xE324)   # 앱 (usb_desc.c, pid.codes)
USB_ID_ROM   = (0x303A, 0x0012)   # ROM USB-OTG 다운로드 모드
USB_ID_OLD   = (0x303A, 0x4001)   # pid.codes 로 옮기기 전 앱 (34a5f06 까지)

PROJ_DIR  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_DIR = os.path.join(PROJ_DIR, "build")


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
  idf_path = os.environ.get("IDF_PATH")
  if not idf_path:
    sys.exit("IDF_PATH 가 없다. ESP-IDF 환경(export.sh)에서 실행한다.")
  return [sys.executable, os.path.join(idf_path, "components", "esptool_py", "esptool", "esptool.py")]


def main():
  ap = argparse.ArgumentParser(description="USB(HS OTG) 로 버튼 없이 굽기")
  ap.add_argument("--port", help="시리얼 포트. 없으면 VID/PID 로 찾는다")
  ap.add_argument("--baud", type=int, default=460800)
  ap.add_argument("--no-baram", action="store_true", help="baram-term 포트 놓기/다시 열기를 하지 않는다")
  args = ap.parse_args()

  if not os.path.isfile(os.path.join(BUILD_DIR, "flash_args")):
    sys.exit("build/flash_args 가 없다. 먼저 idf.py build")

  p = find_port(USB_ID_APP) or find_port(USB_ID_OLD) or find_port(USB_ID_ROM)
  if args.port:
    p = next((x for x in list_ports.comports() if x.device == args.port), p)
  if p is None:
    sys.exit("보드를 찾지 못했다 (1209:E324 앱 / 303A:0012 ROM)")

  serial = p.serial_number
  print(f"{col('board')}      : {p.device}  {p.vid:04X}:{p.pid:04X}  SN={serial}")

  with BaramTerm(p.device, not args.no_baram):
    if (p.vid, p.pid) != USB_ID_ROM:
      print(f"{col('reset')}      : 다운로드 모드로")
      enter_download(p.device)
      rom = wait_port(USB_ID_ROM, serial)
      if rom is None:
        sys.exit("ROM 다운로드 장치(303A:0012)가 나타나지 않았다")
    else:
      rom = p
    print(f"{col('download')}   : {rom.device}")

    cmd = esptool_cmd() + ["--chip", "esp32p4", "-p", rom.device, "-b", str(args.baud),
                           "--before", "no_reset", "--after", "hard_reset",
                           "write_flash", "@flash_args"]
    r = subprocess.run(cmd, cwd=BUILD_DIR)
    if r.returncode != 0:
      sys.exit(r.returncode)

    app = wait_port(USB_ID_APP, serial)
    if app is None:
      sys.exit("앱(1209:E324)이 다시 나타나지 않았다")
    print(f"{col('app')}        : {app.device}")
    time.sleep(0.5)


if __name__ == "__main__":
  main()
