#!/usr/bin/env python3
"""Ping Pet 맥 중계: Slack Socket Mode -> 미응답 요청 목록 -> USB CDC 보드.

토큰은 키체인 서비스 `ping-pet`의 계정 `xoxp`, `xapp`에서 읽는다.
보드와는 줄 단위 JSON(UTF-8, '\n')으로 통신한다. 프로토콜은 인계 문서 "USB 프로토콜" 참고.
"""
from __future__ import annotations

import argparse
import glob
import html
import json
import logging
import os
import re
import subprocess
import sys
import threading
import time
from collections import deque
from datetime import datetime
from pathlib import Path

import keyring
from slack_sdk import WebClient
from slack_sdk.errors import SlackApiError
from slack_sdk.socket_mode import SocketModeClient
from slack_sdk.socket_mode.request import SocketModeRequest
from slack_sdk.socket_mode.response import SocketModeResponse

KEYCHAIN_SERVICE = "ping-pet"
SERIAL_GLOB = "/dev/cu.usbmodem*"
SERIAL_RETRY_SEC = 2
TICK_SEC = 60
TEXT_MAX = 80
LINE_MAX = 4096
BOARD_MAX_ITEMS = 16
STATE_FILE = Path.home() / "Library/Application Support/ping-pet/state.json"

# 처리 대상 메시지 subtype. 그 외(message_changed, channel_join 등)는 무시
ACCEPT_SUBTYPES = {None, "thread_broadcast", "file_share", "me_message"}

EMOJI_MAP = {
    "smile": "☺", "slightly_smiling_face": "☺", "blush": "☺", "grinning": "☺",
    "joy": "ㅋㅋ", "laughing": "ㅋㅋ", "sweat_smile": "^^;",
    "pray": "(꾸벅)", "bow": "(꾸벅)", "+1": "👍", "thumbsup": "👍",
    "heart": "♥", "white_check_mark": "✓", "heavy_check_mark": "✓",
    "x": "✗", "warning": "⚠", "eyes": "(봄)", "fire": "(급)",
    "sob": "ㅠㅠ", "cry": "ㅠ", "rocket": "(!)", "tada": "(축)",
}

log = logging.getLogger("ping-pet")


# ---------------------------------------------------------------- Slack 조회 캐시

class SlackDirectory:
    """users.info / conversations.info 결과를 메모리에 캐시한다."""

    def __init__(self, web: WebClient):
        self.web = web
        self._users: dict[str, str] = {}
        self._channels: dict[str, dict] = {}

    def user_name(self, user_id: str) -> str:
        if user_id in self._users:
            return self._users[user_id]
        name = user_id
        try:
            u = self.web.users_info(user=user_id)["user"]
            p = u.get("profile", {})
            name = p.get("display_name") or p.get("real_name") or u.get("name") or user_id
        except SlackApiError as e:
            log.warning("users.info %s 실패: %s", user_id, e.response.get("error"))
        self._users[user_id] = name
        return name

    def channel(self, channel_id: str) -> dict:
        if channel_id in self._channels:
            return self._channels[channel_id]
        info: dict = {}
        try:
            info = self.web.conversations_info(channel=channel_id)["channel"]
        except SlackApiError as e:
            log.warning("conversations.info %s 실패: %s", channel_id, e.response.get("error"))
        self._channels[channel_id] = info
        return info

    def channel_name(self, channel_id: str) -> str:
        return self.channel(channel_id).get("name") or channel_id


# ---------------------------------------------------------------- 텍스트 가공

_RE_USER = re.compile(r"<@([UW][A-Z0-9]+)(?:\|[^>]*)?>")
_RE_CHAN = re.compile(r"<#([CG][A-Z0-9]+)(?:\|([^>]*))?>")
_RE_SPECIAL = re.compile(r"<!(here|channel|everyone)(?:\|[^>]*)?>")
_RE_SUBTEAM = re.compile(r"<!subteam\^[A-Z0-9]+(?:\|([^>]*))?>")
_RE_LINK = re.compile(r"<((?:https?|mailto):[^>|]+)(?:\|([^>]*))?>")
_RE_EMOJI = re.compile(r":([a-z0-9_+\-']+)(?:::skin-tone-\d)?:")


def clean_text(text: str, d: SlackDirectory) -> str:
    text = _RE_USER.sub(lambda m: "@" + d.user_name(m.group(1)), text)
    text = _RE_CHAN.sub(lambda m: "#" + (m.group(2) or d.channel_name(m.group(1))), text)
    text = _RE_SPECIAL.sub(lambda m: "@" + m.group(1), text)
    text = _RE_SUBTEAM.sub(lambda m: m.group(1) or "@그룹", text)
    text = _RE_LINK.sub(lambda m: m.group(2) or m.group(1), text)
    text = _RE_EMOJI.sub(lambda m: EMOJI_MAP.get(m.group(1), ""), text)
    text = html.unescape(text)
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) > TEXT_MAX:
        text = text[: TEXT_MAX - 1] + "…"
    return text


# ---------------------------------------------------------------- 미응답 목록

class Tracker:
    """미응답 항목과 오늘 통계. 모든 변경은 lock 안에서, 보드 전송은 link로."""

    def __init__(self, link: "BoardLink"):
        self.link = link
        self.lock = threading.Lock()
        self.items: dict[str, dict] = {}
        self.day = datetime.now().date().isoformat()
        self.handled = 0
        self.waits: list[int] = []
        self.recent: deque = deque(maxlen=3)
        self._load()

    # --- 영속화 (재시작해도 목록 유지)
    def _load(self):
        try:
            s = json.loads(STATE_FILE.read_text())
        except (OSError, ValueError):
            return
        self.items = s.get("items", {})
        if s.get("day") == self.day:
            self.handled = s.get("handled", 0)
            self.waits = s.get("waits", [])
            self.recent.extend(s.get("recent", []))
        log.info("상태 복원: 미응답 %d건", len(self.items))

    def _save(self):
        STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        tmp = STATE_FILE.with_suffix(".tmp")
        tmp.write_text(json.dumps({
            "items": self.items, "day": self.day, "handled": self.handled,
            "waits": self.waits, "recent": list(self.recent),
        }, ensure_ascii=False))
        tmp.replace(STATE_FILE)

    def _roll_day(self):
        today = datetime.now().date().isoformat()
        if today != self.day:
            self.day, self.handled, self.waits = today, 0, []
            self.recent.clear()

    def stats(self) -> dict:
        avg = round(sum(self.waits) / len(self.waits) / 60) if self.waits else 0
        return {"handled_today": self.handled, "avg_wait_min": avg, "recent": list(self.recent)}

    def sorted_items(self) -> list[dict]:
        return sorted(self.items.values(), key=lambda i: i["ts"])

    # --- 변경
    def add(self, item: dict):
        with self.lock:
            prev = self.items.get(item["id"])
            if prev:  # 같은 대화의 후속 메시지: 대기 시작 시각은 유지, 내용만 갱신
                item["ts"] = prev["ts"]
            self.items[item["id"]] = item
            self._save()
        log.info("add %s %s: %s", item["id"], item["from"], item["text"])
        self.link.send({"type": "add", "item": item})

    def resolve(self, item_id: str, how: str):
        """how: 'done'(답장 감지) | 'ack'(보드에서 확인)."""
        with self.lock:
            item = self.items.pop(item_id, None)
            if not item:
                return
            self._roll_day()
            waited = max(0, int(time.time()) - item["ts"])
            self.handled += 1
            if how == "done":
                self.waits.append(waited)
            self.recent.appendleft({"from": item["from"], "where": item["where"], "waited": waited})
            self._save()
            stats = self.stats()
        log.info("%s %s (대기 %d초)", how, item_id, waited)
        if how == "done":
            self.link.send({"type": "done", "id": item_id, "waited": waited})
        else:
            self.link.send({"type": "remove", "id": item_id})
        self.link.send({"type": "tick", "now": int(time.time()), "stats": stats})

    def sync_msg(self) -> dict:
        with self.lock:
            self._roll_day()
            items = self.sorted_items()[:BOARD_MAX_ITEMS]
            msg = {"type": "sync", "items": items, "now": int(time.time()), "stats": self.stats()}
        # 4 KB 제한: 넘으면 최신 항목부터 덜어낸다
        while len(json.dumps(msg, ensure_ascii=False).encode()) >= LINE_MAX and msg["items"]:
            msg["items"].pop()
        return msg

    def tick_msg(self) -> dict:
        with self.lock:
            self._roll_day()
            return {"type": "tick", "now": int(time.time()), "stats": self.stats()}


# ---------------------------------------------------------------- 보드 링크

class BoardLink:
    """USB CDC 시리얼. 끊기면 2초마다 재시도하고, 연결되면 sync를 보낸다."""

    def __init__(self, dry_run: bool, port: str | None):
        self.dry_run = dry_run
        self.port = port
        self.ser = None
        self.wlock = threading.Lock()
        self.on_connect = lambda: None
        self.on_line = lambda msg: None

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
            except Exception as e:  # noqa: BLE001 - pyserial은 OSError/SerialException 혼용
                log.warning("시리얼 쓰기 실패: %s", e)
                self._close()

    def _close(self):
        try:
            if self.ser:
                self.ser.close()
        finally:
            self.ser = None

    def run(self):
        if self.dry_run:
            self.on_connect()
            self._read_stdin()
            return
        import serial  # dry-run에서는 pyserial 없이도 동작

        while True:
            path = self.port or next(iter(sorted(glob.glob(SERIAL_GLOB))), None)
            if not path:
                time.sleep(SERIAL_RETRY_SEC)
                continue
            try:
                ser = serial.Serial(path, 115200, timeout=1)
            except Exception as e:  # noqa: BLE001
                log.debug("열기 실패 %s: %s", path, e)
                time.sleep(SERIAL_RETRY_SEC)
                continue
            with self.wlock:
                self.ser = ser
            log.info("보드 연결: %s", path)
            self.on_connect()
            buf = b""
            try:
                while True:
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
            time.sleep(SERIAL_RETRY_SEC)

    def _read_stdin(self):
        """dry-run: 표준입력의 JSON 줄을 보드가 보낸 것으로 취급한다."""
        for raw in sys.stdin.buffer:
            self._dispatch(raw)
        threading.Event().wait()  # stdin이 닫혀도 계속 실행

    def _dispatch(self, raw: bytes):
        raw = raw.strip()
        if not raw:
            return
        try:
            msg = json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            log.debug("보드 로그: %r", raw[:200])  # JSON 아닌 줄은 펌웨어 로그로 간주
            return
        if isinstance(msg, dict):
            self.on_line(msg)


# ---------------------------------------------------------------- 중계 본체

class Relay:
    def __init__(self, web: WebClient, link: BoardLink, include_bots: bool):
        self.web = web
        self.link = link
        self.include_bots = include_bots
        auth = web.auth_test()
        self.me = auth["user_id"]
        self.team = auth["team_id"]
        log.info("로그인: %s (%s / %s)", auth.get("user"), self.me, self.team)
        self.dir = SlackDirectory(web)
        self.tracker = Tracker(link)
        link.on_connect = lambda: link.send(self.tracker.sync_msg())
        link.on_line = self.on_board

    # --- 보드 -> 맥
    def on_board(self, msg: dict):
        t = msg.get("type")
        if t == "hello":
            log.info("보드 hello fw=%s", msg.get("fw"))
            self.link.send(self.tracker.sync_msg())
        elif t == "open":
            self.open_in_slack(str(msg.get("id", "")))
        elif t == "ack":
            self.tracker.resolve(str(msg.get("id", "")), "ack")
        else:
            log.debug("알 수 없는 보드 메시지: %s", msg)

    def open_in_slack(self, item_id: str):
        channel = item_id.split(":", 1)[0]
        if not channel:
            return
        url = f"slack://channel?team={self.team}&id={channel}"
        log.info("open %s", url)
        if not self.link.dry_run or sys.platform == "darwin":
            subprocess.Popen(["open", url])

    # --- Slack -> 맥
    def on_socket(self, client: SocketModeClient, req: SocketModeRequest):
        client.send_socket_mode_response(SocketModeResponse(envelope_id=req.envelope_id))
        if req.type != "events_api":
            return
        ev = req.payload.get("event", {})
        if ev.get("type") != "message":
            return
        try:
            self.on_message(ev)
        except Exception:  # noqa: BLE001 - 한 이벤트 실패로 수신 루프가 죽지 않게
            log.exception("이벤트 처리 실패: %s", ev.get("ts"))

    def on_message(self, ev: dict):
        if ev.get("subtype") not in ACCEPT_SUBTYPES:
            return
        user = ev.get("user")
        if not user or (ev.get("bot_id") and not self.include_bots):
            return
        ctype = ev.get("channel_type")
        channel = ev["channel"]
        ts = ev["ts"]
        thread_ts = ev.get("thread_ts")
        mine = user == self.me

        if ctype in ("im", "mpim"):
            if mine:
                self.tracker.resolve(channel, "done")
            else:
                self.tracker.add(self._item(channel, "dm" if ctype == "im" else "mpim", ev))
            return

        if ctype in ("channel", "group"):
            key = f"{channel}:{thread_ts or ts}"
            if mine:
                if thread_ts:
                    self.tracker.resolve(key, "done")
            elif f"<@{self.me}>" in ev.get("text", ""):
                self.tracker.add(self._item(key, "mention", ev))

    def _item(self, item_id: str, kind: str, ev: dict) -> dict:
        channel = ev["channel"]
        if kind == "dm":
            where = "DM"
        elif kind == "mpim":
            where = "그룹 DM"
        else:
            where = "#" + self.dir.channel_name(channel)
            if ev.get("thread_ts") and ev.get("thread_ts") != ev.get("ts"):
                where += " 스레드"
        text = clean_text(ev.get("text", ""), self.dir)
        if not text and ev.get("files"):
            text = "(파일)"
        return {
            "id": item_id,
            "kind": kind,
            "from": self.dir.user_name(ev["user"]),
            "where": where,
            "text": text,
            "ts": int(float(ev["ts"])),
        }

    def tick_loop(self):
        while True:
            time.sleep(TICK_SEC)
            self.link.send(self.tracker.tick_msg())


# ---------------------------------------------------------------- main

def read_token(account: str) -> str:
    tok = keyring.get_password(KEYCHAIN_SERVICE, account)
    if not tok:
        sys.exit(
            f"키체인에 {KEYCHAIN_SERVICE}/{account} 토큰이 없습니다.\n"
            f"  security add-generic-password -s {KEYCHAIN_SERVICE} -a {account} -w"
        )
    return tok


def main():
    ap = argparse.ArgumentParser(description="Slack 미응답 요청을 USB 보드로 중계")
    ap.add_argument("--dry-run", action="store_true",
                    help="보드 대신 stdout에 JSON 줄 출력, stdin 줄을 보드 입력으로 처리")
    ap.add_argument("--port", help="시리얼 포트 직접 지정 (기본: /dev/cu.usbmodem* 자동 탐색)")
    ap.add_argument("--include-bots", action="store_true", help="봇/앱이 보낸 DM도 미응답으로 집계")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
        stream=sys.stderr,  # dry-run의 stdout은 프로토콜 줄 전용
    )
    logging.getLogger("slack_sdk").setLevel(logging.WARNING)

    web = WebClient(token=read_token("xoxp"))
    link = BoardLink(dry_run=args.dry_run, port=args.port)
    relay = Relay(web, link, include_bots=args.include_bots)

    sm = SocketModeClient(app_token=read_token("xapp"), web_client=web)
    sm.socket_mode_request_listeners.append(relay.on_socket)
    sm.connect()
    log.info("Socket Mode 연결됨")

    threading.Thread(target=relay.tick_loop, daemon=True, name="tick").start()
    try:
        link.run()
    except KeyboardInterrupt:
        pass
    finally:
        sm.close()


if __name__ == "__main__":
    os.environ.setdefault("PYTHONUNBUFFERED", "1")
    main()
