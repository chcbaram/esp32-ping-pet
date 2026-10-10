"""중계 본체 — 보드, 미응답 목록, 이벤트 소스(Slack 또는 시뮬레이션)를 묶는다. 트레이는 status() 만 읽는다."""
from __future__ import annotations

import logging
import threading
import time
from typing import Callable

from .board import BoardLink
from .model import Tracker
from .platform_util import data_dir, open_url

log = logging.getLogger("ping-pet")

TICK_SEC = 60
CRY_SEC  = 30 * 60


class PingPetApp:
  def __init__(self, simulate: bool = False, dry_run: bool = False, port: str | None = None,
               include_bots: bool = False, self_test: bool = False, scan_days: int = 3):
    self.simulate = simulate
    self.board = BoardLink(dry_run=dry_run, port=port)
    self.tracker = Tracker(self.board.send, None if simulate else data_dir() / "state.json")
    self.on_change: Callable[[], None] = lambda: None

    if simulate:
      from .simulate import Simulator
      self.source = Simulator(self.tracker)
    else:
      from .slack_source import SlackSource
      self.source = SlackSource(self.tracker, include_bots=include_bots, self_test=self_test,
                                scan_days=scan_days)

    self.board.on_connect = self._on_connect
    self.board.on_message = self._on_board
    self.board.on_status = self._changed
    self.tracker.listeners.append(self._changed)

  def _on_connect(self):
    # 보드에 hello 를 청한다 (펌웨어 버전, OTA 여부). 보드가 먼저 보내는 hello 는 포트를 여는 사이에 사라질 수 있다
    self.board.send({"type": "hello"})
    self.board.send(self.tracker.sync_msg())

  def _changed(self):
    self.on_change()

  def start(self):
    if self.simulate:
      self.tracker.clear()
    self.board.start()
    if self.simulate:
      self.source.start()
    else:
      threading.Thread(target=self._slack_loop, daemon=True, name="slack").start()
    threading.Thread(target=self._tick_loop, daemon=True, name="tick").start()

  def stop(self):
    self.source.stop()

  def _slack_loop(self):
    """연결이 끊기거나 토큰을 나중에 넣어도 다시 시도한다"""
    while True:
      if not self.source.connected:
        self.source.start()
        self._changed()
      time.sleep(30)

  def _tick_loop(self):
    while True:
      time.sleep(TICK_SEC)
      self.board.send(self.tracker.tick_msg())

  # --- 보드 → 맥
  def _on_board(self, msg: dict):
    t = msg.get("type")
    if t == "hello":
      log.info("보드 hello fw=%s", msg.get("fw"))
      self.board.send(self.tracker.sync_msg())
    elif t == "open":
      self.open_item(str(msg.get("id", "")))
    elif t == "ack":
      self.tracker.resolve(str(msg.get("id", "")), "ack")
    elif t == "sync":            # 보드가 목록을 다시 청한다 (데모를 끈 뒤)
      self.board.send(self.tracker.sync_msg())
    elif t in ("wip", "unwip"):
      self.tracker.set_wip(str(msg.get("id", "")), t == "wip")

  def open_item(self, item_id: str):
    url = None if self.simulate else self.source.channel_url(item_id)
    log.info("open %s %s", item_id, url or "(시뮬레이션)")
    if url:
      open_url(url)

  # --- 트레이가 읽는다
  def status(self) -> dict:
    items = self.tracker.snapshot()
    now = int(time.time())
    active = [i for i in items if not i.get("wip")]
    oldest = now - active[0]["ts"] if active else 0
    if not items:
      state = "idle"
    elif not active:
      state = "work"
    elif oldest > CRY_SEC:
      state = "cry"
    else:
      state = "hungry"
    if self.simulate:
      slack = "시뮬레이션"
    elif self.source.connected:
      slack = f"연결됨 ({self.source.user})"
    else:
      slack = f"끊김 ({self.source.error or '연결 중'})"
    if self.board.paused:
      board = "일시 정지"
    elif self.board.connected:
      board = f"연결됨 ({self.board.port}{', fw ' + self.board.fw if self.board.fw else ''})"
    else:
      board = "찾는 중"
    return {"items": items, "count": len(items), "oldest_sec": oldest, "state": state,
            "slack": slack, "board": board, "paused": self.board.paused}
