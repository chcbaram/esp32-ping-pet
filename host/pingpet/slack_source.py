"""Slack 이벤트 → 미응답 판정 (Socket Mode, 사용자 토큰).

  미응답 판정 (인계 문서 "맥 중계 스크립트 사양")
    DM, 그룹 DM 에서 남이 보낸 메시지       → 추가. 키는 채널 ID
    채널 메시지 중 <@내 사용자 ID> 가 들어간 것 → 추가. 키는 채널 ID:스레드 ts (없으면 메시지 ts)
    내가 같은 DM 또는 같은 스레드에 쓰면      → 제거 (done)

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
from collections import deque

from . import APP_ID

log = logging.getLogger("ping-pet")

TEXT_MAX = 80

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
    except SlackApiError as e:
      log.warning("users.info %s 실패: %s", user_id, e.response.get("error"))
    self._users[user_id] = name
    return name

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
  def __init__(self, tracker, include_bots: bool = False, self_test: bool = False):
    self.tracker = tracker
    self.include_bots = include_bots
    self.self_test = self_test
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
      self.web = WebClient(token=xoxp)
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
    # TODO: 시작할 때 훑기. 지금은 실행한 뒤에 오는 이벤트만 받아 그 전에 와 있던 미응답이 빠진다
    #   DM · 그룹 DM : conversations.list(types=im,mpim) → 최근 대화의 마지막 메시지가 남의 것이면 추가
    #   멘션        : 최근 며칠의 <@나> 메시지 중 내가 그 스레드에 쓰지 않은 것 (search.messages 는 search:read 필요)
    #   이미 목록(state.json)에 있는 것은 두고, 그사이 내가 답한 것은 뺀다
    return True

  def stop(self):
    if self.sm:
      self.sm.close()
      self.sm = None

  def channel_url(self, item_id: str) -> str | None:
    channel = item_id.split(":", 1)[0]
    if not channel or not self.team:
      return None
    return f"slack://channel?team={self.team}&id={channel}"

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
    if ev.get("type") != "message":
      return
    try:
      self._on_message(ev)
    except Exception:   # noqa: BLE001 - 한 이벤트 실패로 수신 루프가 죽지 않게
      log.exception("이벤트 처리 실패: %s", ev.get("ts"))

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
        self.tracker.add(self._item(key, "mention", ev))

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
