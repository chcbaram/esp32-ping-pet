# PyInstaller 설정 — 맥 .app, 윈도우 · 리눅스 폴더 (그 뒤 dmg / Setup.exe / AppImage 로 묶는다)
#
#   host 폴더에서 :  pyinstaller packaging/ping_pet.spec --noconfirm
#   먼저 :           python packaging/make_icons.py
#   펌웨어 :         PINGPET_FIRMWARE 폴더(기본 ../firmware/ping-pet/dist)의 ping-pet-fw-*.bin,
#                    ping-pet-fonts-*.bin 을 앱 안의 firmware/ 에 넣는다 (트레이의 "보드 펌웨어 업데이트")
import os
import sys
from pathlib import Path

HOST  = Path(SPECPATH).parent
BUILD = HOST / "packaging" / "build"
FW    = Path(os.environ.get("PINGPET_FIRMWARE", HOST.parent / "firmware" / "ping-pet" / "dist"))

sys.path.insert(0, str(HOST))
from pingpet import APP_NAME, __version__  # noqa: E402

# 가장 새 펌웨어와 같은 버전의 폰트만
fws = sorted(FW.glob("ping-pet-fw-*.bin"), key=lambda p: p.stat().st_mtime)
datas = []
if fws:
  ver = fws[-1].name[len("ping-pet-fw-"):-len(".bin")]
  datas = [(str(p), "firmware") for p in (fws[-1], FW / f"ping-pet-fonts-{ver}.bin") if p.is_file()]
  print(f"펌웨어 {ver} 를 넣는다")
if not datas:
  print(f"경고 : {FW} 에 펌웨어 파일이 없다. 앱에서 보드 업데이트를 할 수 없다")

# pystray 는 OS 마다 백엔드를 이름으로 불러온다
hidden = ["pystray._darwin", "pystray._win32", "pystray._xorg", "pystray._appindicator", "pystray._gtk",
          "keyring.backends.macOS", "keyring.backends.Windows", "keyring.backends.SecretService",
          "keyring.backends.libsecret", "keyring.backends.kwallet"]

a = Analysis(
  [str(HOST / "ping_pet.py")],
  pathex=[str(HOST)],
  datas=datas,
  hiddenimports=hidden,
  excludes=["tkinter"],
)
pyz = PYZ(a.pure)

icon = str(BUILD / ("icon.icns" if sys.platform == "darwin" else "icon.ico"))

exe = EXE(
  pyz, a.scripts, [],
  exclude_binaries=True,
  name="ping-pet" if sys.platform.startswith("linux") else APP_NAME,
  console=False,
  icon=icon,
)
coll = COLLECT(exe, a.binaries, a.datas, name=APP_NAME)

if sys.platform == "darwin":
  app = BUNDLE(
    coll,
    name=f"{APP_NAME}.app",
    icon=icon,
    bundle_identifier="io.github.chcbaram.ping-pet",
    version=__version__,
    info_plist={
      "LSUIElement": True,                 # 메뉴 막대 앱 : Dock 아이콘 없음
      "CFBundleShortVersionString": __version__,
      "NSHumanReadableCopyright": "Ping Pet",
    },
  )
