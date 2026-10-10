#!/usr/bin/env python3
"""화면 캡처 — 보드가 지금 표시 중인 화면을 PNG 로 저장한다.

  CLI `lcd capture` 로 프레임버퍼(패널 방향 480x800 RGB565)를 받아 화면 방향(800x480)으로 돌린다.
  baram-term 이 포트를 열고 있으면 잠시 놓게 한다.

  쓰는 법:
    python3 tools/capture.py                 # capture.png
    python3 tools/capture.py -o shot.png
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import time
import zlib

from serial import Serial
from serial.tools import list_ports


USB_ID_APP = (0x1209, 0xE324)


def find_port():
  for p in list_ports.comports():
    if (p.vid, p.pid) == USB_ID_APP:
      return p.device
  return None


def write_png(path, w, h, rgb):
  raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

  def chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

  with open(path, "wb") as f:
    f.write(b"\x89PNG\r\n\x1a\n")
    f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
    f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
    f.write(chunk(b"IEND", b""))


def rgb565_to_screen(fb, pw, ph, rot):
  """패널 버퍼 → 화면 RGB888. lcd.c 의 lcdIndex() 와 같은 변환."""
  if rot in (90, 270):
    sw, sh = ph, pw
  else:
    sw, sh = pw, ph
  px = struct.unpack("<%dH" % (pw * ph), fb)
  out = bytearray(sw * sh * 3)
  for y in range(sh):
    for x in range(sw):
      if rot == 270:
        i = x * pw + (pw - 1 - y)
      elif rot == 90:
        i = (ph - 1 - x) * pw + y
      else:
        i = y * pw + x
      c = px[i]
      o = (y * sw + x) * 3
      out[o]     = ((c >> 11) & 0x1F) * 255 // 31
      out[o + 1] = ((c >> 5) & 0x3F) * 255 // 63
      out[o + 2] = (c & 0x1F) * 255 // 31
  return sw, sh, bytes(out)


def main():
  ap = argparse.ArgumentParser(description="보드 화면 캡처")
  ap.add_argument("-o", "--out", default="capture.png")
  ap.add_argument("--port")
  ap.add_argument("--no-baram", action="store_true")
  args = ap.parse_args()

  port = args.port or find_port()
  if port is None:
    sys.exit("보드를 찾지 못했다 (1209:E324)")

  ctl = None if args.no_baram else shutil.which("baram-ctl")
  released = False
  if ctl:
    released = subprocess.run([ctl, "--port", port, "release"], capture_output=True).returncode == 0

  try:
    # 115200 으로 열어야 CDC 의 주인이 CLI 가 된다 (docs/27 3절)
    with Serial(port, 115200, timeout=3) as ser:
      time.sleep(0.3)
      ser.reset_input_buffer()
      ser.write(b"\r")
      time.sleep(0.2)
      ser.reset_input_buffer()
      ser.write(b"lcd capture\r")

      while True:
        line = ser.readline()
        if not line:
          sys.exit("응답이 없다")
        if line.startswith(b"CAPTURE ") and not line.startswith(b"CAPTURE END"):
          pw, ph, rot, size = (int(v) for v in line.split()[1:5])
          break

      fb = bytearray()
      while len(fb) < size:
        data = ser.read(size - len(fb))
        if not data:
          sys.exit("받다가 끊겼다 (%d / %d)" % (len(fb), size))
        fb += data
  finally:
    if released:
      subprocess.run([ctl, "--port", port, "resume"], capture_output=True)

  sw, sh, rgb = rgb565_to_screen(bytes(fb), pw, ph, rot)
  write_png(args.out, sw, sh, rgb)
  print("%s : %dx%d" % (os.path.abspath(args.out), sw, sh))


if __name__ == "__main__":
  main()
