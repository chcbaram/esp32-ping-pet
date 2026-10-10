"""미응답 요청 목록과 오늘 통계. 바뀔 때마다 보드로 보낼 메시지를 send() 로 넘긴다.

  보드 프로토콜 (firmware/ping-pet/main/ap/modules/link/link.c)
    sync {items[], now, tz, stats} / add {item} / done {id, waited} / remove {id} / tick {now, tz, stats}
    item  : {id, kind: dm|mpim|mention, from, where, text, ts, wip}   wip : 처리 중 (1)
    stats : {handled_today, avg_wait_min, recent: [{kind, from, where, waited, at}]}
"""
from __future__ import annotations

import json
import logging
import threading
import time
from collections import deque
from datetime import datetime
from pathlib import Path
from typing import Callable

log = logging.getLogger("ping-pet")

BOARD_MAX_ITEMS = 16
DISMISS_KEEP_SEC = 30 * 86400      # 치운 기록을 이만큼 둔다
LINE_MAX        = 4096


def local_tz() -> int:
  """지금 시간대 (초). 서머타임 반영"""
  return int(datetime.now().astimezone().utcoffset().total_seconds())


class Tracker:
  """모든 변경은 lock 안에서, 보드 전송은 send 콜백으로 (보드가 없으면 버린다)."""

  def __init__(self, send: Callable[[dict], None], state_file: Path | None):
    self.send = send
    self.state_file = state_file
    self.lock = threading.Lock()
    self.items: dict[str, dict] = {}
    self.day = datetime.now().date().isoformat()
    self.handled = 0
    self.waits: list[int] = []
    self.recent: deque = deque(maxlen=3)
    self.dismissed: dict[str, int] = {}                # 보드에서 치운 것 : id → 치운 시각 (훑기가 되살리지 않게)
    self.listeners: list[Callable[[], None]] = []      # 트레이가 상태를 다시 그린다
    self._load()

  # --- 영속화 (재시작해도 목록 유지)
  def _load(self):
    if not self.state_file:
      return
    try:
      s = json.loads(self.state_file.read_text())
    except (OSError, ValueError):
      return
    self.items = s.get("items", {})
    cut = time.time() - DISMISS_KEEP_SEC
    self.dismissed = {k: v for k, v in s.get("dismissed", {}).items() if v >= cut}
    if s.get("day") == self.day:
      self.handled = s.get("handled", 0)
      self.waits = s.get("waits", [])
      self.recent.extend(s.get("recent", []))
    log.info("상태 복원: 미응답 %d건", len(self.items))

  def _save(self):
    if not self.state_file:
      return
    self.state_file.parent.mkdir(parents=True, exist_ok=True)
    tmp = self.state_file.with_suffix(".tmp")
    tmp.write_text(json.dumps({
      "items": self.items, "day": self.day, "handled": self.handled,
      "waits": self.waits, "recent": list(self.recent), "dismissed": self.dismissed,
    }, ensure_ascii=False))
    tmp.replace(self.state_file)

  def _roll_day(self):
    today = datetime.now().date().isoformat()
    if today != self.day:
      self.day, self.handled, self.waits = today, 0, []
      self.recent.clear()

  def _notify(self):
    for f in self.listeners:
      try:
        f()
      except Exception:      # noqa: BLE001
        log.exception("listener")

  def stats(self) -> dict:
    avg = round(sum(self.waits) / len(self.waits) / 60) if self.waits else 0
    return {"handled_today": self.handled, "avg_wait_min": avg, "recent": list(self.recent)}

  def snapshot(self) -> list[dict]:
    with self.lock:
      return sorted(self.items.values(), key=lambda i: i["ts"])

  # --- 변경
  def add(self, item: dict):
    with self.lock:
      prev = self.items.get(item["id"])
      if prev:    # 같은 대화의 후속 메시지 : 기다리기 시작한 시각은 그대로, 내용만 바꾼다
        item["ts"] = prev["ts"]
        item.setdefault("wip", prev.get("wip", 0))
      self.items[item["id"]] = item
      self.dismissed.pop(item["id"], None)      # 치운 뒤 새 글이 왔다
      self._save()
    log.info("add %s %s: %s", item["id"], item["from"], item["text"])
    self.send({"type": "add", "item": item})
    self._notify()

  def set_wip(self, item_id: str, on: bool):
    """처리 중 표시 (보드에서 고르거나 Slack 에서 👀). 울음 · 배부름 계산에서 빠진다"""
    with self.lock:
      item = self.items.get(item_id)
      if not item or bool(item.get("wip")) == on:
        return
      item["wip"] = 1 if on else 0
      self._save()
    log.info("%s %s", "처리 중" if on else "처리 중 해제", item_id)
    self.send({"type": "add", "item": item})
    self._notify()

  def find(self, channel: str, ts: str | None = None) -> str | None:
    """Slack 메시지 (채널, ts) → 목록의 id. DM 은 채널, 멘션은 채널:스레드 또는 그 멘션 메시지"""
    with self.lock:
      if channel in self.items:
        return channel
      for item_id, item in self.items.items():
        if ts and (item_id == f"{channel}:{ts}" or (item_id.startswith(channel + ":") and item.get("msg_ts") == ts)):
          return item_id
    return None

  def resolve(self, item_id: str, how: str):
    """how : 'done' (답장 감지) | 'ack' (보드에서 확인)"""
    with self.lock:
      item = self.items.pop(item_id, None)
      if not item:
        return
      self._roll_day()
      now = int(time.time())
      waited = max(0, now - item["ts"])
      self.handled += 1
      if how == "done":
        self.waits.append(waited)
      else:
        self.dismissed[item_id] = now
      self.recent.appendleft({"kind": item["kind"], "from": item["from"], "where": item["where"],
                              "waited": waited, "at": now})
      self._save()
      stats = self.stats()
    log.info("%s %s (대기 %d초)", how, item_id, waited)
    if how == "done":
      self.send({"type": "done", "id": item_id, "waited": waited})
    else:
      self.send({"type": "remove", "id": item_id})
    self.send({"type": "tick", "now": int(time.time()), "tz": local_tz(), "stats": stats})
    self._notify()

  def merge(self, found: dict[str, dict], gone: set[str]):
    """시작할 때 훑은 결과 : found 는 넣거나 갱신 (기다리기 시작한 시각은 이른 쪽), gone 은 조용히 뺀다.
    그사이 답한 것은 냠냠 없이 빠진다 (오늘 처리 수에도 넣지 않는다). 보드에는 sync 한 번"""
    with self.lock:
      for item_id in gone:
        self.items.pop(item_id, None)
      for item_id, item in list(found.items()):
        # 보드에서 치운 뒤로 새 글이 없으면 되살리지 않는다
        last = item.pop("last", item["ts"])
        if item_id in self.dismissed and last <= self.dismissed[item_id]:
          del found[item_id]
          continue
        prev = self.items.get(item_id)
        if prev:
          item["ts"] = min(prev["ts"], item["ts"])
          item["wip"] = max(prev.get("wip", 0), item.get("wip", 0))    # 보드에서 고른 처리 중은 Slack 에 없다
        self.items[item_id] = item
      self._save()
    log.info("훑기 반영 : 추가 · 갱신 %d건, 뺌 %d건", len(found), len(gone))
    self.send(self.sync_msg())
    self._notify()

  def clear(self):
    with self.lock:
      self.items.clear()
      self._save()
    self._notify()

  def sync_msg(self) -> dict:
    with self.lock:
      self._roll_day()
      items = sorted(self.items.values(), key=lambda i: i["ts"])[:BOARD_MAX_ITEMS]
      msg = {"type": "sync", "items": items, "now": int(time.time()), "tz": local_tz(), "stats": self.stats()}
    # 한 줄 4 KB 제한 : 넘으면 최신 항목부터 덜어낸다
    while len(json.dumps(msg, ensure_ascii=False).encode()) >= LINE_MAX and msg["items"]:
      msg["items"].pop()
    return msg

  def tick_msg(self) -> dict:
    with self.lock:
      self._roll_day()
      return {"type": "tick", "now": int(time.time()), "tz": local_tz(), "stats": self.stats()}
