#!/usr/bin/env python3
"""폰트 파티션 이미지 만들기 — main/ap/modules/ui/fonts/*.ttf 를 하나로 묶는다.

  펌웨어(ui_theme.c)가 부팅할 때 fonts 파티션을 메모리에 매핑해 이름으로 찾아 쓴다.

  형식 (리틀 엔디언)
    0   'PPFT'                  매직
    4   u32 version (1)
    8   u32 count
    12  u32 reserved
    16  항목 count 개, 각 48 B : name[32] (NUL 채움), u32 offset, u32 size, u32 crc32, u32 reserved
    ..  데이터 (각 16 B 정렬). offset 은 이미지 처음부터

  쓰는 법 :  python3 tools/fonts.py [-o build/fonts.bin]
"""
import argparse
import glob
import os
import struct
import zlib

PROJ_DIR  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR  = os.path.join(PROJ_DIR, "main", "ap", "modules", "ui", "fonts")
OUT_FILE  = os.path.join(PROJ_DIR, "build", "fonts.bin")

MAGIC     = b"PPFT"
VERSION   = 1
ENTRY_LEN = 48


def build(out):
  files = sorted(glob.glob(os.path.join(FONT_DIR, "*.ttf")))
  head  = 16 + ENTRY_LEN * len(files)
  pos   = (head + 15) & ~15
  table = b""
  data  = b""

  for f in files:
    blob = open(f, "rb").read()
    name = os.path.basename(f).encode()[:31]
    pad  = ((pos + 15) & ~15) - pos
    data += b"\0" * pad
    pos  += pad
    table += struct.pack("<32sIIII", name, pos, len(blob), zlib.crc32(blob) & 0xFFFFFFFF, 0)
    data += blob
    pos  += len(blob)

  img = MAGIC + struct.pack("<III", VERSION, len(files), 0) + table
  img += b"\0" * (((len(img) + 15) & ~15) - len(img)) + data

  os.makedirs(os.path.dirname(out), exist_ok=True)
  open(out, "wb").write(img)
  for f in files:
    print("  %-28s %8d B" % (os.path.basename(f), os.path.getsize(f)))
  print("%s : %d B" % (out, len(img)))
  return out


def main():
  ap = argparse.ArgumentParser(description="폰트 파티션 이미지 만들기")
  ap.add_argument("-o", "--out", default=OUT_FILE)
  args = ap.parse_args()
  build(args.out)


if __name__ == "__main__":
  main()
