#!/usr/bin/env python3
"""Ping Pet 중계 — Slack 미응답 요청을 USB 로 보드(답답이)에 보낸다.

  python3 ping_pet.py                 # 트레이 아이콘 + Slack (토큰은 키체인)
  python3 ping_pet.py --simulate      # Slack 없이 가상의 메시지로 보드 연결 시험
  python3 ping_pet.py --no-tray       # 트레이 없이 (리눅스 서비스 등)
  python3 ping_pet.py --dry-run --no-tray   # 보드 없이 보낼 JSON 줄을 화면에 찍는다
"""
from __future__ import annotations

import argparse
import logging
import sys
import threading

from pingpet import APP_NAME, __version__
from pingpet.app import PingPetApp
from pingpet import settings
from pingpet.platform_util import log_file


def main():
  ap = argparse.ArgumentParser(description=f"{APP_NAME} 중계 {__version__}")
  ap.add_argument("--simulate", action="store_true", help="Slack 없이 가상의 메시지로 시험")
  ap.add_argument("--dry-run", action="store_true", help="보드 대신 보낼 JSON 줄을 표준 출력에")
  ap.add_argument("--port", help="보드 포트 직접 지정 (기본: VID:PID 1209:E324 로 찾는다)")
  ap.add_argument("--no-tray", action="store_true", help="트레이 아이콘 없이 실행")
  ap.add_argument("--include-bots", action="store_true", help="봇 · 앱이 보낸 DM 도 미응답으로")
  ap.add_argument("--self-test", action="store_true",
                  help="혼자 시험 : 나에게 보내는 DM 에 '핑 …' 으로 카드 추가, 다른 글로 답장")
  ap.add_argument("--scan-days", type=int, default=None, metavar="N",
                  help="시작할 때 최근 N 일의 미응답을 훑는다 (0 이면 안 함). 없으면 트레이에서 고른 값 (기본 3)")
  ap.add_argument("-v", "--verbose", action="store_true")
  args = ap.parse_args()

  handlers = [logging.FileHandler(log_file(), encoding="utf-8"), logging.StreamHandler(sys.stderr)]
  logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                      format="%(asctime)s %(levelname)s %(message)s", handlers=handlers)
  logging.getLogger("slack_sdk").setLevel(logging.WARNING)

  app = PingPetApp(simulate=args.simulate, dry_run=args.dry_run, port=args.port,
                   include_bots=args.include_bots, self_test=args.self_test,
                   scan_days=settings.get("scan_days") if args.scan_days is None else args.scan_days)
  app.start()

  if args.no_tray:
    try:
      threading.Event().wait()
    except KeyboardInterrupt:
      pass
  else:
    from pingpet.tray import run_tray
    run_tray(app)          # 메인 스레드에서 돈다 (macOS 메뉴 막대는 메인 스레드여야 한다)
  app.stop()


if __name__ == "__main__":
  main()
