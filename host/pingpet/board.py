"""보드 연결 — USB CDC 위의 줄 단위 JSON.

  보드를 VID:PID (1209:E324) 로 찾는다. 포트 이름은 OS 마다 다르다 (/dev/cu.usbmodem…, COMx, /dev/ttyACM…).
  115200 이 아닌 보율(921600)로 연다. 보드는 보율로 CDC 의 주인을 가른다 — 115200 은 CLI, 그 외는 이 링크.
  끊기면 2 초마다 다시 찾는다. pause() 로 포트를 잠시 놓을 수 있다 (굽기, baram-term).
"""
from __future__ import annotations

import json
import logging
import threading
import time
from typing import Callable

log = logging.getLogger("ping-pet")

USB_ID          = (0x1209, 0xE324)
LINK_BAUD       = 921600
RETRY_SEC       = 2
LINE_MAX        = 4096


class BoardLink:
  def __init__(self, dry_run: bool = False, port: str | None = None):
    self.dry_run = dry_run
    self.port_fixed = port
    self.ser = None
    self.port = None
    self.wlock = threading.Lock()
    self.paused = False
    self.fw = None
    self.on_connect: Callable[[], None] = lambda: None
    self.on_message: Callable[[dict], None] = lambda msg: None
    self.on_status: Callable[[], None] = lambda: None
    self._thread = None

  @property
  def connected(self) -> bool:
    return self.dry_run or self.ser is not None

  def find_port(self) -> str | None:
    from serial.tools import list_ports

    if self.port_fixed:
      return self.port_fixed
    for p in list_ports.comports():
      if (p.vid, p.pid) == USB_ID:
        return p.device
    return None

  def send(self, msg: dict):
    line = json.dumps(msg, ensure_ascii=False, separators=(",", ":"))
    data = (line + "\n").encode()
    if len(data) > LINE_MAX:
      log.error("줄이 4 KB 초과(%d B), 버림: %s", len(data), msg.get("type"))
      return
    if self.dry_run:
      print(line, flush=True)
      return
    with self.wlock:
      if not self.ser:
        return
      try:
        self.ser.write(data)
      except Exception as e:  # noqa: BLE001 - pyserial 은 OSError / SerialException 을 섞어 던진다
        log.warning("보드 쓰기 실패: %s", e)
        self._close()

  def pause(self, on: bool):
    """포트를 놓는다 (on) / 다시 연다 (off)"""
    self.paused = on
    if on:
      with self.wlock:
        self._close()
    self.on_status()

  def _close(self):
    try:
      if self.ser:
        self.ser.close()
    except Exception:  # noqa: BLE001
      pass
    if self.ser is not None:
      self.ser = None
      self.fw = None
      self.on_status()

  def start(self):
    self._thread = threading.Thread(target=self._run, daemon=True, name="board")
    self._thread.start()

  def _run(self):
    if self.dry_run:
      self.on_connect()
      return
    import serial

    while True:
      if self.paused:
        time.sleep(0.5)
        continue
      path = self.find_port()
      if not path:
        time.sleep(RETRY_SEC)
        continue
      try:
        ser = serial.Serial(path, LINK_BAUD, timeout=0.5)
      except Exception as e:  # noqa: BLE001
        log.debug("열기 실패 %s: %s", path, e)
        time.sleep(RETRY_SEC)
        continue
      with self.wlock:
        self.ser = ser
        self.port = path
      log.info("보드 연결: %s", path)
      self.on_status()
      self.on_connect()
      buf = b""
      try:
        while self.ser is not None and not self.paused:
          chunk = ser.read(256)
          if not chunk:
            continue
          buf += chunk
          while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            self._dispatch(raw)
          if len(buf) > LINE_MAX:
            buf = b""
      except Exception as e:  # noqa: BLE001
        log.warning("보드 연결 끊김: %s", e)
      with self.wlock:
        self._close()
      time.sleep(RETRY_SEC)

  def _dispatch(self, raw: bytes):
    raw = raw.strip()
    if not raw:
      return
    try:
      msg = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
      log.debug("보드: %r", raw[:200])
      return
    if isinstance(msg, dict):
      if msg.get("type") == "hello":
        self.fw = msg.get("fw")
        self.on_status()
      self.on_message(msg)
