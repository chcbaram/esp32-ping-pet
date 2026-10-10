"""보드 펌웨어 업데이트 (USB OTA) — 트레이 메뉴에서 부른다. 진행은 보드 화면에 보인다.

  펌웨어 파일을 찾는 곳
    묶은 앱   : 앱 안의 firmware/ (빌드할 때 릴리즈 파일을 넣는다)
    소스 실행 : 저장소의 firmware/ping-pet/dist/ (tools/release.py 로 만든다)
  ping-pet-fw-<버전>.bin  : 0x0 부터 합친 이미지. OTA 로는 그 안의 앱(0x10000 ~)만 보낸다
  ping-pet-fonts-<버전>.bin : 폰트 파티션 이미지

  보내는 법 (link.c) : ota_begin → ota_ready, ota_data(2 KB, base64) → ota_ack 를 되풀이, ota_end → ota_done.
  보드는 다 받으면 다시 시작한다. 폰트와 앱을 함께 보낼 때는 폰트 → (다시 연결) → 앱.

  OTA 를 받지 못하는 옛 펌웨어는 tools/flash.py 로 한 번 굽는다.
"""
from __future__ import annotations

import base64
import hashlib
import logging
import re
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path

log = logging.getLogger("ping-pet")

REPO_DIST  = Path(__file__).resolve().parents[2] / "firmware" / "ping-pet" / "dist"
APP_OFFSET = 0x10000          # 합친 이미지 안의 앱 위치 (partitions.csv 의 ota_0)
CHUNK      = 2048             # link.c 의 OTA_CHUNK_MAX
ACK_SEC    = 10.0             # 조각 하나의 응답 (폰트는 64 KB 씩 지우느라 늦을 수 있다)
END_SEC    = 15.0             # ota_end : 앱 이미지 확인
RECONNECT_SEC = 30.0

_RE_FW = re.compile(r"ping-pet-fw-(.+)\.bin$")


@dataclass
class Image:
  version: str
  fw: Path
  fonts: Path | None


def _bundle_dir() -> Path | None:
  if not getattr(sys, "frozen", False):
    return None
  base = getattr(sys, "_MEIPASS", None)                 # PyInstaller
  if base:
    return Path(base) / "firmware"
  exe = Path(sys.executable).resolve()                 # py2app : …/Contents/MacOS/x → …/Contents/Resources
  return exe.parents[1] / "Resources" / "firmware"


def find_image() -> Image | None:
  folder = _bundle_dir() or REPO_DIST
  if not folder.is_dir():
    return None
  found = sorted(folder.glob("ping-pet-fw-*.bin"), key=lambda p: p.stat().st_mtime)
  if not found:
    return None
  fw = found[-1]
  version = _RE_FW.search(fw.name).group(1)
  fonts = folder / f"ping-pet-fonts-{version}.bin"
  return Image(version, fw, fonts if fonts.is_file() else None)


class OtaError(Exception):
  pass


class Updater:
  """한 번에 하나만. state : '' (쉬는 중) | 'busy' | 'ok' | 'fail', message 는 메뉴에 보인다"""

  def __init__(self, board, on_status=lambda: None):
    self.board = board
    self.on_status = on_status
    self.state = ""
    self.message = ""

  @property
  def busy(self) -> bool:
    return self.state == "busy"

  def start(self, image: Image, with_fonts: bool = False):
    if self.busy:
      return
    self._set("busy", f"{image.version} 준비 중…")
    threading.Thread(target=self._run, args=(image, with_fonts), daemon=True, name="ota").start()

  def _set(self, state: str, message: str):
    self.state, self.message = state, message
    self.on_status()

  def _run(self, image: Image, with_fonts: bool):
    jobs = []
    if with_fonts and image.fonts:
      jobs.append(("fonts", image.fonts.read_bytes()))
    jobs.append(("app", image.fw.read_bytes()[APP_OFFSET:]))

    try:
      for i, (target, data) in enumerate(jobs):
        if i > 0:
          self._wait_reconnect()
        self._send(target, image.version, data)
      self._wait_reconnect()
      if self.board.fw != image.version:
        raise OtaError(f"다시 시작한 보드가 {self.board.fw} 로 떴어요 (되돌림?)")
      self._set("ok", f"{image.version} 로 업데이트했어요")
      log.info("OTA 끝 %s", image.version)
    except Exception as e:  # noqa: BLE001 - OtaError, TimeoutError, 시리얼 오류. 트레이가 죽지 않게
      try:
        self.board.send({"type": "ota_abort"})
      except Exception:  # noqa: BLE001
        pass
      self._set("fail", f"업데이트 실패 : {e}")
      log.error("OTA 실패: %s", e)

  def _send(self, target: str, version: str, data: bytes):
    if not self.board.connected:
      raise OtaError("보드가 연결되어 있지 않아요")
    if not self.board.ota:
      raise OtaError("보드 펌웨어가 OTA 를 받지 못해요 (tools/flash.py 로 한 번 굽기)")

    name = "폰트" if target == "fonts" else "펌웨어"
    size = len(data)
    log.info("OTA %s %s %d B", target, version, size)
    r = self.board.ota_request({"type": "ota_begin", "target": target, "size": size,
                                "sha256": hashlib.sha256(data).hexdigest(), "version": version}, ACK_SEC)
    if r.get("type") != "ota_ready":
      raise OtaError(r.get("msg") or str(r))

    pre = time.time()
    shown = -1
    for off in range(0, size, CHUNK):
      chunk = data[off:off + CHUNK]
      r = self.board.ota_request({"type": "ota_data", "off": off,
                                  "data": base64.b64encode(chunk).decode()}, ACK_SEC)
      if r.get("type") != "ota_ack" or r.get("off") != off + len(chunk):
        raise OtaError(r.get("msg") or f"응답이 틀림 {r}")
      pct = (off + len(chunk)) * 100 // size
      if pct != shown and pct % 5 == 0:
        shown = pct
        self._set("busy", f"{name} 보내는 중 {pct}%")

    r = self.board.ota_request({"type": "ota_end"}, END_SEC)
    if r.get("type") != "ota_done":
      raise OtaError(r.get("msg") or str(r))
    log.info("OTA %s 보냄 %.1f 초", target, time.time() - pre)
    self._set("busy", f"{name} 보냄, 보드 다시 시작 중…")

  def _wait_reconnect(self):
    """보드가 다시 시작해 링크가 끊겼다가 hello 로 다시 붙을 때까지"""
    pre = time.time()
    while self.board.connected and time.time() - pre < 5:      # 끊기기를 기다린다
      time.sleep(0.1)
    while time.time() - pre < RECONNECT_SEC:
      if self.board.connected and self.board.fw:
        return
      time.sleep(0.2)
    raise OtaError("보드가 다시 연결되지 않았어요")
