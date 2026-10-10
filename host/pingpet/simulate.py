"""시뮬레이션 — Slack 없이 가상의 메시지를 만들어 진짜 링크(USB JSON)로 보드에 보낸다.

  보드 데모 모드(inbox_demo.c)와 같은 흐름. 토큰 없이 맥 ↔ 보드 연결을 끝에서 끝까지 시험한다.
    여유 → DM · 멘션 (배고픔) → 2 건 더 (4 건) → 차례로 답장 (냠냠) → 여유
"""
from __future__ import annotations

import threading
import time

STEPS = [
  # (기다릴 초, 동작, 인자)
  (6,  "add",  ("D01",   "dm",      "김민수", "DM",         "샘플 보드 펌웨어 오늘 중으로 올려줄 수 있어요?", 12 * 60)),
  (1,  "add",  ("C01:1", "mention", "이지은", "#hw-team",   "@나 커넥터 핀맵 한 번만 확인 부탁드려요",       4 * 60)),
  (6,  "add",  ("D02",   "dm",      "박준호", "DM",         "라이다 로그 공유드렸어요, 시간 될 때 봐주세요",   38 * 60)),
  (1,  "add",  ("C02:1", "mention", "최유나", "#robot-ops", "@나 내일 회의 시간 괜찮으세요?",                3 * 60)),
  (2,  "add",  ("D03",   "dm",      "정다은", "DM",         "견적서 검토 부탁드려요",                         9 * 60)),
  (1,  "add",  ("G01",   "mpim",    "한도윤", "그룹 DM",    "점심 메뉴 투표해 주세요",                        7 * 60)),
  (1,  "add",  ("C03:1", "mention", "오세진", "#fw-dev",    "@나 OTA 브랜치 머지해도 될까요?",                5 * 60)),
  (1,  "add",  ("D04",   "dm",      "윤하린", "DM",         "배터리 시험 결과 공유드립니다",                  1 * 60)),
  (12, "done", ("D02",)),
  (6,  "done", ("D01",)),
  (5,  "done", ("C01:1",)),
  (2,  "done", ("C02:1",)),
  (2,  "done", ("D03",)),
  (2,  "done", ("G01",)),
  (2,  "done", ("C03:1",)),
  (2,  "done", ("D04",)),
]


class Simulator:
  def __init__(self, tracker):
    self.tracker = tracker
    self.running = False

  def start(self):
    self.running = True
    threading.Thread(target=self._run, daemon=True, name="simulate").start()

  def stop(self):
    self.running = False

  def _run(self):
    while self.running:
      for wait, op, arg in STEPS:
        for _ in range(wait * 10):
          if not self.running:
            return
          time.sleep(0.1)
        if op == "add":
          item_id, kind, frm, where, text, ago = arg
          self.tracker.add({"id": item_id, "kind": kind, "from": frm, "where": where,
                            "text": text, "ts": int(time.time()) - ago})
        else:
          self.tracker.resolve(arg[0], "done")
      time.sleep(8)
