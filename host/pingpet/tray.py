"""트레이 · 메뉴 막대 아이콘 (pystray) — macOS 메뉴 막대, Windows 알림 영역, Linux 트레이.

  아이콘 : 답답이 기본 로고. 미응답이 있으면 오른쪽 위에 빨간 점 (상태와 건수는 메뉴 · 툴팁에).
  메뉴   : 상태 · 보드 · Slack, 미응답 목록 (누르면 그 대화 열기), 보드 포트 잠시 놓기,
           보드 펌웨어 업데이트, 로그, 로그인할 때 실행, 종료.

  macOS 메뉴 막대는 메인 스레드에서 돌아야 한다 → run_tray() 는 메인 스레드에서 부른다.
"""
from __future__ import annotations

import logging
import sys
import threading
import time

from PIL import Image, ImageDraw

from . import APP_ID, APP_NAME, __version__
from .firmware import Updater, find_image
from .platform_util import autostart_enabled, autostart_set, log_file, open_file

log = logging.getLogger("ping-pet")

ITEM_MENU_MAX = 12          # 메뉴에 보일 미응답 수. 나머지는 "외 N건"
REFRESH_SEC   = 1.0

# 답답이 로고 (pet.c 의 로고 좌표, 바탕 상자 0 ~ 288.72)
C_BODY = (0x00, 0xD4, 0x73)
C_FACE = (0xDB, 0xFF, 0xD3)
C_DOT  = (0xE5, 0x3B, 0x2F)
LOGO_X, LOGO_Y, LOGO_W = 0.0, 4.67, 288.72
# 둘레 투명 여백 (한 변 비율). macOS 메뉴 막대는 그림을 막대 높이(22 pt)에 꽉 채우므로 여백을 더 둬
# 다른 아이콘(약 16 pt)과 크기를 맞춘다. Windows · Linux 트레이는 작게 그려지므로 조금만
LOGO_PAD = 0.14 if sys.platform == "darwin" else 0.04

STATE_TEXT = {"idle": "배불러요", "hungry": "배고파요", "cry": "엉엉… 너무 배고파요"}


def draw_logo(size: int = 64, dot: bool = False) -> Image.Image:
  """기본 로고 (몸통, 반원 눈, 입 막대, 발). dot : 미응답이 있으면 오른쪽 위에 빨간 점.
  4 배로 그려 줄여서 가장자리를 부드럽게"""
  k = 4
  n = size * k
  pad = n * LOGO_PAD
  s = (n - pad * 2) / LOGO_W
  img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
  d = ImageDraw.Draw(img)

  def x(v):
    return pad + (v - LOGO_X) * s

  def y(v):
    return pad + (v - LOGO_Y) * s

  def half_disk(cx, base, r):     # 위로 볼록한 반원 (밑변 y = base)
    d.pieslice((x(cx - r), y(base - r), x(cx + r), y(base + r)), 180, 360, fill=C_FACE)

  d.rounded_rectangle((x(0), y(4.67), x(288.72), y(4.67 + 288.72)), radius=37.73 * s, fill=C_BODY)
  half_disk(86.94, 93.3, 31.97)                                     # 눈
  half_disk(202.11, 93.3, 31.97)
  d.rectangle((x(39.81), y(128.15), x(39.81 + 209.12), y(128.15 + 55.72)), fill=C_FACE)   # 입
  half_disk(86.94, 247.57, 34.76)                                   # 발
  half_disk(199.32, 247.57, 34.76)

  if dot:                          # 몸통 오른쪽 위 모서리에 걸친다. 흰 테로 로고와 떼어 보이게
    r = n * 0.17
    cx, cy = n - r - n * 0.01, r + n * 0.01
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=C_DOT, outline=(255, 255, 255), width=int(n * 0.04))
  return img.resize((size, size), Image.LANCZOS)


def _on_main(fn):
  """macOS 는 메뉴 막대를 메인 스레드에서만 바꿀 수 있다 (다른 스레드에서 setMenu 하면 앱이 죽는다)"""
  if sys.platform == "darwin":
    from PyObjCTools import AppHelper
    AppHelper.callAfter(fn)
  else:
    fn()


def _age(sec: int) -> str:
  m = sec // 60
  return f"{m}분" if m < 60 else f"{m // 60}시간 {m % 60}분"


# --- 트레이
class Tray:
  def __init__(self, app):
    import pystray

    self.pystray = pystray
    self.app = app
    self.updater = Updater(app.board, on_status=self._kick)
    self.image = find_image()
    self._key = None
    self._last_note = None
    self._dot = False
    self._dirty = threading.Event()
    self.icon = pystray.Icon(APP_ID, draw_logo(), APP_NAME, menu=pystray.Menu(self._menu))
    app.on_change = self._kick

  def _kick(self):
    self._dirty.set()

  # 메뉴는 열 때마다 새로 만든다
  def _menu(self):
    Item, Menu = self.pystray.MenuItem, self.pystray.Menu
    st = self.app.status()
    now = int(time.time())

    yield Item(f"{APP_NAME} — {STATE_TEXT[st['state']]} · 미응답 {st['count']}건", None, enabled=False)
    yield Item(f"보드 : {st['board']}", None, enabled=False)
    yield Item(f"Slack : {st['slack']}", None, enabled=False)
    yield Menu.SEPARATOR

    items = st["items"]
    for it in items[:ITEM_MENU_MAX]:
      label = f"{it['from']} · {it['where']}  ({_age(now - it['ts'])})"
      yield Item(label, self._opener(it["id"]))
    if len(items) > ITEM_MENU_MAX:
      yield Item(f"외 {len(items) - ITEM_MENU_MAX}건", None, enabled=False)
    if not items:
      yield Item("미응답 없음", None, enabled=False)
    yield Menu.SEPARATOR

    yield Item("보드 포트 잠시 놓기 (터미널 · flash.py 용)", self._toggle_pause,
               checked=lambda _: self.app.board.paused, enabled=not self.updater.busy)
    yield Item("보드 펌웨어 업데이트", Menu(*self._fw_menu()))
    yield Menu.SEPARATOR
    yield Item("로그 보기", lambda: open_file(log_file()))
    yield Item("로그인할 때 실행", self._toggle_autostart, checked=lambda _: autostart_enabled())
    yield Item(f"종료  (v{__version__})", self._quit)

  def _fw_menu(self):
    Item = self.pystray.MenuItem
    board = self.app.board
    self.image = find_image()
    yield Item(f"보드 : {board.fw or '연결 안 됨'}", None, enabled=False)
    if self.updater.message:
      yield Item(self.updater.message, None, enabled=False)
    if self.image is None:
      yield Item("펌웨어 파일이 없어요", None, enabled=False)
      return
    if board.connected and not board.ota:
      yield Item("이 보드 펌웨어는 앱에서 업데이트할 수 없어요 (flash.py 로 한 번)", None, enabled=False)
    can = board.connected and board.ota and not self.updater.busy and not self.app.simulate
    same = board.fw == self.image.version
    yield Item(f"{self.image.version} 로 업데이트" + ("  (같은 버전)" if same else ""),
               lambda: self.updater.start(self.image, with_fonts=False), enabled=can)
    if self.image.fonts:
      yield Item(f"{self.image.version} + 폰트 업데이트",
                 lambda: self.updater.start(self.image, with_fonts=True), enabled=can)

  def _opener(self, item_id: str):
    return lambda: self.app.open_item(item_id)

  def _toggle_pause(self):
    self.app.board.pause(not self.app.board.paused)

  def _toggle_autostart(self):
    autostart_set(not autostart_enabled())

  def _quit(self):
    self.icon.stop()

  # 상태가 바뀌면 아이콘과 메뉴를 갈아 끼운다 (1 초에 한 번까지, 경과 시간은 30 초마다)
  def _refresh_loop(self):
    while True:
      self._dirty.wait(timeout=30)
      self._dirty.clear()
      _on_main(self._apply)
      if self.updater.state in ("ok", "fail") and self.updater.message:
        self._notify(self.updater.message)
      time.sleep(REFRESH_SEC)

  def _apply(self):
    st = self.app.status()
    title = f"{APP_NAME} — {STATE_TEXT[st['state']]} · 미응답 {st['count']}건"
    if title != self._key:
      self._key = title
      self.icon.title = title
    dot = st["count"] > 0
    if dot != self._dot:
      self._dot = dot
      self.icon.icon = draw_logo(dot=dot)
    try:
      self.icon.update_menu()
    except Exception:  # noqa: BLE001 - 백엔드에 따라 없다
      pass

  def _notify(self, text: str):
    if text == self._last_note:          # 같은 결과를 되풀이해 알리지 않는다
      return
    self._last_note = text
    try:
      _on_main(lambda: self.icon.notify(text, APP_NAME))
    except Exception:  # noqa: BLE001 - 알림을 못 띄우는 환경
      pass

  def run(self):
    def setup(icon):
      icon.visible = True
      threading.Thread(target=self._refresh_loop, daemon=True, name="tray").start()
      self._kick()

    self.icon.run(setup=setup)


def run_tray(app):
  Tray(app).run()
