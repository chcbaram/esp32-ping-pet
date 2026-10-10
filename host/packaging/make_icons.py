#!/usr/bin/env python3
"""앱 아이콘 만들기 — 트레이와 같은 답답이 로고 (pingpet/tray.py draw_logo).

  packaging/build/icon.png (512), icon.icns (맥), icon.ico (윈도우)
  쓰는 법 : python packaging/make_icons.py   (host 폴더에서)
"""
import sys
from pathlib import Path

HOST = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HOST))

from pingpet.tray import draw_logo  # noqa: E402

OUT = HOST / "packaging" / "build"


def main():
  OUT.mkdir(parents=True, exist_ok=True)
  big = draw_logo(1024, pad=0.08)
  big.resize((512, 512)).save(OUT / "icon.png")
  big.save(OUT / "icon.icns")                                    # Pillow 가 여러 크기를 넣는다
  big.save(OUT / "icon.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
  print("\n".join(str(p) for p in sorted(OUT.glob("icon.*"))))


if __name__ == "__main__":
  main()
