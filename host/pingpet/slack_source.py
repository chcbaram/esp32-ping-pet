"""Slack 이벤트 → 미응답 판정 (Socket Mode, 사용자 토큰).

  미응답 판정 (인계 문서 "맥 중계 스크립트 사양")
    DM, 그룹 DM 에서 남이 보낸 메시지       → 추가. 키는 채널 ID
    채널 메시지 중 <@내 사용자 ID> 가 들어간 것 → 추가. 키는 채널 ID:스레드 ts (없으면 메시지 ts)
    내가 같은 DM 또는 같은 스레드에 쓰면      → 제거 (done)

  시작할 때 훑기 (--scan-days, 기본 3 일, 0 이면 안 함) : 중계를 켜기 전에 와 있던 미응답을 찾는다.
    DM · 그룹 DM : 최근 N 일 대화에서 내 마지막 메시지 뒤로 남의 메시지가 있으면 미응답 (im:read, mpim:read)
    멘션        : 최근 N 일 <@나> 검색 → 그 스레드에 내가 그 뒤로 쓰지 않았으면 미응답 (search:read)
    이미 목록에 있던 것 중 그사이 답한 것은 조용히 뺀다. 권한이 없으면 그 부분만 건너뛴다.

  --self-test : 혼자 시험할 때. 내가 쓴 DM 이 "핑" 으로 시작하면 남이 보낸 DM 처럼 카드를 만들고
                (메시지마다 하나), 그 밖의 글을 DM 에 쓰면 가장 오래된 시험 카드부터 하나씩 답장으로 처리한다.
                나에게 보내는 DM 창에서 쓴다. Slackbot 리마인더는 이벤트로 오지 않아 시험에 쓸 수 없다.

  토큰은 키체인(맥) / 자격 증명 관리자(윈도우) / Secret Service(리눅스)의 서비스 ping-pet,
  계정 xoxp (User OAuth Token), xapp (App-Level Token) 에서 읽는다.
"""
from __future__ import annotations

import html
import logging
import re
import threading
import time
from collections import deque
from datetime import datetime, timedelta
from urllib.parse import parse_qs, urlparse

from . import APP_ID

log = logging.getLogger("ping-pet")

TEXT_MAX = 80

# 내가 단 반응 : 👀 → 처리 중 (빼면 해제), ✅ 등 → 완료 (답장한 것으로)
WIP_REACTIONS  = {"eyes"}
DONE_REACTIONS = {"white_check_mark", "heavy_check_mark", "ballot_box_with_check", "done"}

# 처리하는 메시지 subtype. 그 외(message_changed, channel_join 등)는 무시
ACCEPT_SUBTYPES = {None, "thread_broadcast", "file_share", "me_message"}

# 보드 폰트(Jua, Silkscreen)에는 그림 문자가 없다. 글자로 바꾸고 나머지는 지운다
EMOJI_MAP = {
  "smile": "^^", "slightly_smiling_face": "^^", "blush": "^^", "grinning": "^^",
  "joy": "ㅋㅋ", "laughing": "ㅋㅋ", "sweat_smile": "^^;",
  "pray": "(꾸벅)", "bow": "(꾸벅)", "+1": "(좋아요)", "thumbsup": "(좋아요)",
  "heart": "(하트)", "white_check_mark": "(확인)", "heavy_check_mark": "(확인)",
  "x": "(X)", "warning": "(주의)", "eyes": "(봄)", "fire": "(급)",
  "sob": "ㅠㅠ", "cry": "ㅠ", "rocket": "(!)", "tada": "(축하)",
}

_RE_USER    = re.compile(r"<@([UW][A-Z0-9]+)(?:\|[^>]*)?>")
_RE_CHAN    = re.compile(r"<#([CG][A-Z0-9]+)(?:\|([^>]*))?>")
_RE_SPECIAL = re.compile(r"<!(here|channel|everyone)(?:\|[^>]*)?>")
_RE_SUBTEAM = re.compile(r"<!subteam\^[A-Z0-9]+(?:\|([^>]*))?>")
_RE_LINK    = re.compile(r"<((?:https?|mailto):[^>|]+)(?:\|([^>]*))?>")
_RE_EMOJI   = re.compile(r":([a-z0-9_+\-']+)(?:::skin-tone-\d)?:")
# 보드 폰트에 없는 그림 문자 (유니코드 이모지) 를 지운다
_RE_PICTO   = re.compile("[\U0001F000-\U0001FAFF☀-➿️‍]")


def read_tokens() -> tuple[str | None, str | None]:
  import keyring

  return keyring.get_password(APP_ID, "xoxp"), keyring.get_password(APP_ID, "xapp")


def save_tokens(xoxp: str, xapp: str):
  import keyring

  keyring.set_password(APP_ID, "xoxp", xoxp)
  keyring.set_password(APP_ID, "xapp", xapp)


class SlackDirectory:
  """users.info / conversations.info 결과를 메모리에 캐시한다."""

  def __init__(self, web):
    self.web = web
    self._users: dict[str, str] = {}
    self._bots: dict[str, bool] = {}
    self._channels: dict[str, dict] = {}

  def user_name(self, user_id: str) -> str:
    from slack_sdk.errors import SlackApiError

    if user_id in self._users:
      return self._users[user_id]
    name = user_id
    try:
      u = self.web.users_info(user=user_id)["user"]
      p = u.get("profile", {})
      name = p.get("display_name") or p.get("real_name") or u.get("name") or user_id
      self._bots[user_id] = bool(u.get("is_bot")) or user_id == "USLACKBOT"
    except SlackApiError as e:
      log.warning("users.info %s 실패: %s", user_id, e.response.get("error"))
    self._users[user_id] = name
    return name

  def is_bot(self, user_id: str) -> bool:
    if user_id not in self._bots:
      self.user_name(user_id)
    return self._bots.get(user_id, False)

  def channel_name(self, channel_id: str) -> str:
    from slack_sdk.errors import SlackApiError

    if channel_id not in self._channels:
      info: dict = {}
      try:
        info = self.web.conversations_info(channel=channel_id)["channel"]
      except SlackApiError as e:
        # channels:read / groups:read 스코프가 없으면 여기로 온다. 카드에 채널 ID 가 보인다
        log.warning("conversations.info %s 실패: %s", channel_id, e.response.get("error"))
      self._channels[channel_id] = info
    return self._channels[channel_id].get("name") or channel_id


def clean_text(text: str, d: SlackDirectory) -> str:
  text = _RE_USER.sub(lambda m: "@" + d.user_name(m.group(1)), text)
  text = _RE_CHAN.sub(lambda m: "#" + (m.group(2) or d.channel_name(m.group(1))), text)
  text = _RE_SPECIAL.sub(lambda m: "@" + m.group(1), text)
  text = _RE_SUBTEAM.sub(lambda m: m.group(1) or "@그룹", text)
  text = _RE_LINK.sub(lambda m: m.group(2) or m.group(1), text)
  text = _RE_EMOJI.sub(lambda m: EMOJI_MAP.get(m.group(1), ""), text)
  text = html.unescape(text)
  text = _RE_PICTO.sub("", text)
  text = re.sub(r"\s+", " ", text).strip()
  if len(text) > TEXT_MAX:
    text = text[: TEXT_MAX - 1] + "…"
  return text


SELF_TEST_PREFIX = "핑"


class SlackSource:
  def __init__(self, tracker, include_bots: bool = False, self_test: bool = False, scan_days: int = 3):
    self.tracker = tracker
    self.include_bots = include_bots
    self.self_test = self_test
    self.scan_days = scan_days
    self.scan_status = ""                   # 트레이에 보인다
    self.self_ids: deque[str] = deque()     # --self-test 로 만든 카드 (오래된 것이 앞)
    self.web = None
    self.sm = None
    self.me = None
    self.team = None
    self.user = None
    self.dir = None
    self.error = None

  @property
  def connected(self) -> bool:
    return self.sm is not None and self.error is None

  def start(self) -> bool:
    from slack_sdk import WebClient
    from slack_sdk.socket_mode import SocketModeClient

    xoxp, xapp = read_tokens()
    if not xoxp or not xapp:
      self.error = "토큰 없음"
      log.error("키체인에 %s/xoxp, %s/xapp 토큰이 없습니다", APP_ID, APP_ID)
      return False
    try:
      from slack_sdk.http_retry.builtin_handlers import RateLimitErrorRetryHandler

      self.web = WebClient(token=xoxp)
      self.web.retry_handlers.append(RateLimitErrorRetryHandler(max_retry_count=5))   # 훑기는 호출이 많다
      auth = self.web.auth_test()
      self.me, self.team, self.user = auth["user_id"], auth["team_id"], auth.get("user")
      self.dir = SlackDirectory(self.web)
      self.sm = SocketModeClient(app_token=xapp, web_client=self.web)
      self.sm.socket_mode_request_listeners.append(self._on_socket)
      self.sm.connect()
    except Exception as e:   # noqa: BLE001
      self.error = str(e)
      log.error("Slack 연결 실패: %s", e)
      return False
    self.error = None
    log.info("Slack 연결: %s (%s / %s)", self.user, self.me, self.team)
    if self.self_test:
      log.info("혼자 시험 : 나에게 보내는 DM 에 '%s …' 으로 카드 추가, 다른 글로 답장", SELF_TEST_PREFIX)
    if self.scan_days > 0:
      self.rescan()
    return True

  def rescan(self, days: int | None = None):
    """훑기를 (다시) 시작한다. 트레이의 '지금 다시 훑기', 기간 바꾸기"""
    if days is not None:
      self.scan_days = days
    if self.web is None or self.scan_days <= 0 or self.scan_status == "훑는 중…":
      return
    threading.Thread(target=self._scan, daemon=True, name="scan").start()

  def stop(self):
    if self.sm:
      self.sm.close()
      self.sm = None

  def channel_url(self, item_id: str) -> str | None:
    """카드를 눌렀을 때 열 곳. 멘션(채널:스레드 ts)은 그 메시지로 간다.
    message 파라미터는 Slack 문서에 없다 (공식 문서는 채널 열기만). 시험으로 확인한다"""
    channel, _, ts = item_id.partition(":")
    if not channel or not self.team:
      return None
    url = f"slack://channel?team={self.team}&id={channel}"
    if ts:
      url += f"&message={ts}"
    return url

  # --- Socket Mode
  def _on_socket(self, client, req):
    from slack_sdk.socket_mode.response import SocketModeResponse

    client.send_socket_mode_response(SocketModeResponse(envelope_id=req.envelope_id))
    ev = req.payload.get("event", {}) if req.type == "events_api" else {}
    # 본문은 남기지 않는다 (-v 일 때만)
    log.debug("socket %s event=%s subtype=%s channel_type=%s user=%s bot=%s", req.type, ev.get("type"),
              ev.get("subtype"), ev.get("channel_type"), ev.get("user"), ev.get("bot_id"))
    if req.type != "events_api":
      return
    try:
      if ev.get("type") in ("reaction_added", "reaction_removed"):
        self._on_reaction(ev)
      elif ev.get("type") == "message":
        self._on_message(ev)
    except Exception:   # noqa: BLE001 - 한 이벤트 실패로 수신 루프가 죽지 않게
      log.exception("이벤트 처리 실패: %s", ev.get("ts"))

  def _on_reaction(self, ev: dict):
    """내가 단 👀 / ✅ 만 본다"""
    item = ev.get("item", {})
    log.debug("reaction %s %s", ev.get("type"), ev.get("reaction"))
    if ev.get("user") != self.me or item.get("type") != "message":
      return
    name = ev.get("reaction", "").split("::")[0]       # 피부색 붙은 것
    item_id = self.tracker.find(item.get("channel", ""), item.get("ts"))
    if not item_id:
      return
    added = ev["type"] == "reaction_added"
    if name in WIP_REACTIONS:
      self.tracker.set_wip(item_id, added)
    elif name in DONE_REACTIONS and added:
      self.tracker.resolve(item_id, "done")

  def _on_message(self, ev: dict):
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
      if mine and self.self_test and self._self_test(ev):
        return
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
        item = self._item(key, "mention", ev)
        item["msg_ts"] = ev["ts"]
        self.tracker.add(item)

  # --- 시작할 때 훑기
  def _accept(self, m: dict) -> bool:
    if m.get("subtype") not in ACCEPT_SUBTYPES or not m.get("user"):
      return False
    if self.include_bots or m["user"] == self.me:
      return True
    return not (m.get("bot_id") or self.dir.is_bot(m["user"]))

  def _call(self, what: str, fn, **kw):
    """SlackApiError 를 None 으로. 권한이 없으면 한 번만 알린다"""
    from slack_sdk.errors import SlackApiError

    try:
      return fn(**kw)
    except SlackApiError as e:
      err = e.response.get("error")
      if err == "missing_scope":
        log.warning("훑기 : %s 권한 없음 (%s) → 매니페스트에 넣고 앱을 다시 설치", what, e.response.get("needed"))
      else:
        log.warning("훑기 : %s 실패 %s", what, err)
      return None

  def _my_reactions(self, m: dict) -> set[str]:
    return {r["name"].split("::")[0] for r in m.get("reactions", []) if self.me in r.get("users", [])}

  def _replied_after(self, channel: str, root: str, after_ts: str) -> bool | None:
    """스레드 root 에 내가 after_ts 뒤로 썼나. 읽지 못하면 None"""
    r = self._call("conversations.replies", self.web.conversations_replies, channel=channel, ts=root,
                   oldest=after_ts, limit=200)
    if r is None:
      return None
    return any(m.get("user") == self.me and float(m["ts"]) > float(after_ts) for m in r.get("messages", []))

  def _scan(self):
    pre = time.time()
    oldest = pre - self.scan_days * 86400
    found: dict[str, dict] = {}
    checked: set[str] = set()          # 결과를 믿을 수 있는 id (훑은 대화 · 확인한 스레드)
    self.scan_status = "훑는 중…"
    log.info("훑기 시작 : 최근 %d 일", self.scan_days)

    # DM · 그룹 DM
    cursor, n_conv = None, 0
    while True:
      r = self._call("DM 목록 (users.conversations)", self.web.users_conversations,
                     types="im,mpim", exclude_archived=True, limit=200, cursor=cursor)
      if r is None:
        break
      for ch in r.get("channels", []):
        n_conv += 1
        cid = ch["id"]
        h = self._call("conversations.history", self.web.conversations_history,
                       channel=cid, oldest=str(oldest), limit=50)
        if h is None:
          continue
        checked.add(cid)
        pending = []                   # 최신부터 내 메시지를 만날 때까지
        for m in h.get("messages", []):
          if not self._accept(m):
            continue
          if m["user"] == self.me:
            break
          pending.append(m)
        marks = set().union(*(self._my_reactions(m) for m in pending)) if pending else set()
        if marks & DONE_REACTIONS:     # ✅ 를 달았다 → 답한 것으로
          pending = []
        if pending:
          ev = dict(pending[0], channel=cid)
          item = self._item(cid, "mpim" if ch.get("is_mpim") else "dm", ev)
          item["ts"] = int(float(pending[-1]["ts"]))     # 기다리기 시작한 때 = 답하지 않은 첫 메시지
          item["last"] = int(float(pending[0]["ts"]))    # 가장 최근 메시지 (치운 뒤 새 글이 왔나)
          item["wip"] = 1 if marks & WIP_REACTIONS else 0
          found[cid] = item
      cursor = r.get("response_metadata", {}).get("next_cursor")
      if not cursor:
        break

    # 멘션
    after = (datetime.now() - timedelta(days=self.scan_days + 1)).strftime("%Y-%m-%d")
    r = self._call("멘션 검색 (search.messages)", self.web.search_messages,
                   query=f"<@{self.me}> after:{after}", sort="timestamp", count=100)
    for m in (r or {}).get("messages", {}).get("matches", []):
      ch = m.get("channel", {})
      if ch.get("is_im") or ch.get("is_mpim") or m.get("user") in (None, self.me) or float(m["ts"]) < oldest:
        continue
      cid = ch["id"]
      q = parse_qs(urlparse(m.get("permalink", "")).query)
      root = q.get("thread_ts", [m["ts"]])[0]
      key = f"{cid}:{root}"
      # 검색 결과에는 bot_id 가 없을 때가 있다 (GitHub 앱 등). 보낸 사람이 봇이면 뺀다 (예전에 넣은 것도)
      if not self.include_bots and (m.get("bot_id") or self.dir.is_bot(m["user"])):
        checked.add(key)
        continue
      if key in found:
        continue
      r2 = self._call("conversations.replies", self.web.conversations_replies, channel=cid, ts=root, limit=200)
      if r2 is None:
        continue
      checked.add(key)
      msgs = r2.get("messages", [])
      replied = any(x.get("user") == self.me and float(x["ts"]) > float(m["ts"]) for x in msgs)
      marks = next((self._my_reactions(x) for x in msgs if x["ts"] == m["ts"]), set())
      if not replied and not marks & DONE_REACTIONS:
        item = self._item(key, "mention", dict(m, channel=cid))
        item["last"] = item["ts"]
        item["msg_ts"] = m["ts"]
        item["wip"] = 1 if marks & WIP_REACTIONS else 0
        found.setdefault(key, item)

    # 이미 목록에 있던 멘션 중 검색에 안 걸린 것 (N 일보다 오래됨) : 그 스레드를 직접 본다
    for it in self.tracker.snapshot():
      key = it["id"]
      if key in checked or ":" not in key or key in self.self_ids:
        continue
      cid, root = key.split(":", 1)
      replied = self._replied_after(cid, root, root)
      if replied is not None:
        checked.add(key)
        if not replied:
          found.setdefault(key, it)

    # DM 은 훑은 범위(N 일)보다 오래된 것은 판단하지 않는다 → 목록에 그대로 둔다
    gone = set()
    for it in self.tracker.snapshot():
      key = it["id"]
      if key in checked and key not in found:
        gone.add(key)
    self.tracker.merge(found, gone)
    self.scan_status = f"훑기 끝 : 대화 {n_conv}개, 미응답 {len(found)}건 ({time.time() - pre:.0f}초)"
    log.info("%s", self.scan_status)

  def _self_test(self, ev: dict) -> bool:
    """--self-test : 처리했으면 True"""
    text = ev.get("text", "").strip()
    if text.startswith(SELF_TEST_PREFIX):
      item_id = f"{ev['channel']}:{ev['ts']}"
      item = self._item(item_id, "dm", ev)
      item["text"] = item["text"][len(SELF_TEST_PREFIX):].strip() or "(시험)"
      item["where"] = "DM (시험)"
      self.self_ids.append(item_id)
      self.tracker.add(item)
      return True
    if self.self_ids:
      self.tracker.resolve(self.self_ids.popleft(), "done")
      return True
    return False

  def _item(self, item_id: str, kind: str, ev: dict) -> dict:
    channel = ev["channel"]
    if kind == "dm":
      where = "DM"
    elif kind == "mpim":
      where = "그룹 DM"
    else:
      where = "#" + self.dir.channel_name(channel)
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
