"""OS 마다 다른 것 — 설정 · 로그 폴더, 슬랙 대화 열기, 로그인할 때 자동 실행."""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

from . import APP_ID, APP_NAME


def data_dir() -> Path:
  if sys.platform == "darwin":
    p = Path.home() / "Library" / "Application Support" / APP_ID
  elif sys.platform == "win32":
    p = Path(os.environ.get("APPDATA", Path.home())) / APP_ID
  else:
    p = Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local" / "state")) / APP_ID
  p.mkdir(parents=True, exist_ok=True)
  return p


def log_file() -> Path:
  if sys.platform == "darwin":
    p = Path.home() / "Library" / "Logs" / f"{APP_ID}.log"
    p.parent.mkdir(parents=True, exist_ok=True)
    return p
  return data_dir() / f"{APP_ID}.log"


def open_url(url: str):
  """slack://… 를 Slack 데스크톱 앱으로 연다"""
  if sys.platform == "darwin":
    subprocess.Popen(["open", url])
  elif sys.platform == "win32":
    os.startfile(url)        # noqa: S606 - 윈도우의 URL 핸들러
  else:
    subprocess.Popen(["xdg-open", url])


def open_file(path: Path):
  if sys.platform == "darwin":
    subprocess.Popen(["open", str(path)])
  elif sys.platform == "win32":
    os.startfile(str(path))  # noqa: S606
  else:
    subprocess.Popen(["xdg-open", str(path)])


# --- 로그인할 때 자동 실행
#   macOS   : ~/Library/LaunchAgents/<id>.plist (RunAtLoad)
#   Windows : 시작 프로그램 폴더에 .cmd
#   Linux   : ~/.config/autostart/<id>.desktop
def _launch_cmd() -> list[str]:
  if getattr(sys, "frozen", False):          # .app / .exe 로 묶였을 때
    if sys.platform == "darwin":
      app = Path(sys.executable).resolve().parents[2]   # …/Ping Pet.app/Contents/MacOS/x
      return ["/usr/bin/open", "-a", str(app)]
    return [sys.executable]
  return [sys.executable, str(Path(__file__).resolve().parents[1] / "ping_pet.py")]


def _autostart_path() -> Path:
  if sys.platform == "darwin":
    return Path.home() / "Library" / "LaunchAgents" / f"com.github.{APP_ID}.plist"
  if sys.platform == "win32":
    return Path(os.environ["APPDATA"]) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / "Startup" / f"{APP_ID}.cmd"
  return Path.home() / ".config" / "autostart" / f"{APP_ID}.desktop"


def autostart_enabled() -> bool:
  return _autostart_path().exists()


def autostart_set(enable: bool):
  path = _autostart_path()
  if not enable:
    path.unlink(missing_ok=True)
    return
  path.parent.mkdir(parents=True, exist_ok=True)
  cmd = _launch_cmd()
  if sys.platform == "darwin":
    args = "".join(f"    <string>{a}</string>\n" for a in cmd)
    path.write_text(f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>com.github.{APP_ID}</string>
  <key>ProgramArguments</key>
  <array>
{args}  </array>
  <key>RunAtLoad</key><true/>
</dict>
</plist>
""")
  elif sys.platform == "win32":
    path.write_text("@echo off\r\nstart \"\" " + " ".join(f'"{a}"' for a in cmd) + "\r\n")
  else:
    path.write_text(f"""[Desktop Entry]
Type=Application
Name={APP_NAME}
Exec={" ".join(cmd)}
X-GNOME-Autostart-enabled=true
""")
