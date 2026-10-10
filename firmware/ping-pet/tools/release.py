#!/usr/bin/env python3
"""릴리즈 파일 만들기 — dist/ 에 펌웨어 · 폰트 이미지와 굽기 도구를 모은다.

  ping-pet-fw-<버전>.bin      부트로더 + 파티션 테이블 + 앱을 0x0 부터 합친 이미지 (esptool merge_bin)
  ping-pet-fonts-<버전>.bin   폰트 파티션 (0x810000, tools/fonts.py)
  flash.py                    굽기 도구 (ESP-IDF 없이 : pip install esptool pyserial)
  SHA256SUMS

  버전은 main/hw/hw_def.h 의 _DEF_FIRMWATRE_VERSION. 먼저 idf.py build 를 한다.

  쓰는 법 (ESP-IDF 환경에서) :  python3 tools/release.py [-o dist]
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys

TOOL_DIR  = os.path.dirname(os.path.abspath(__file__))
PROJ_DIR  = os.path.dirname(TOOL_DIR)
BUILD_DIR = os.path.join(PROJ_DIR, "build")

sys.path.insert(0, TOOL_DIR)
import flash   # noqa: E402
import fonts   # noqa: E402


def fw_version():
  text = open(os.path.join(PROJ_DIR, "main", "hw", "hw_def.h")).read()
  m = re.search(r'_DEF_FIRMWATRE_VERSION\s+"([^"]+)"', text)
  if not m:
    sys.exit("hw_def.h 에 _DEF_FIRMWATRE_VERSION 이 없다")
  return m.group(1)


def main():
  ap = argparse.ArgumentParser(description="릴리즈 파일 만들기")
  ap.add_argument("-o", "--out", default=os.path.join(PROJ_DIR, "dist"))
  args = ap.parse_args()

  if not os.path.isfile(os.path.join(BUILD_DIR, "flash_args")):
    sys.exit("build/flash_args 가 없다. 먼저 idf.py build")

  ver = fw_version()
  os.makedirs(args.out, exist_ok=True)
  fw_bin    = os.path.join(args.out, f"ping-pet-fw-{ver}.bin")
  fonts_bin = os.path.join(args.out, f"ping-pet-fonts-{ver}.bin")

  # flash_args 의 첫 줄 (--flash_mode dio ...) 이 머리에 들어간다
  r = subprocess.run(flash.esptool_cmd() + ["--chip", "esp32p4", "merge_bin", "-o", fw_bin, "@flash_args"],
                     cwd=BUILD_DIR)
  if r.returncode != 0:
    sys.exit(r.returncode)
  fonts.build(fonts_bin)
  shutil.copy(os.path.join(TOOL_DIR, "flash.py"), os.path.join(args.out, "flash.py"))

  files = [fw_bin, fonts_bin, os.path.join(args.out, "flash.py")]
  with open(os.path.join(args.out, "SHA256SUMS"), "w") as f:
    for p in files:
      f.write(f"{hashlib.sha256(open(p, 'rb').read()).hexdigest()}  {os.path.basename(p)}\n")

  for p in files:
    print(f"{os.path.getsize(p):>9,}  {os.path.relpath(p)}")


if __name__ == "__main__":
  main()
