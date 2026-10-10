"""중계 설정 — data_dir()/settings.json. 트레이 메뉴에서 바꾸고 다음 실행에도 남는다."""
from __future__ import annotations

import json
import logging

from .platform_util import data_dir

log = logging.getLogger("ping-pet")

DEFAULTS = {
  "scan_days": 3,          # 시작할 때 훑기 (0 이면 안 함)
}


def _path():
  return data_dir() / "settings.json"


def load() -> dict:
  s = dict(DEFAULTS)
  try:
    s.update(json.loads(_path().read_text()))
  except (OSError, ValueError):
    pass
  return s


def get(key: str):
  return load().get(key, DEFAULTS.get(key))


def set(key: str, value):  # noqa: A001 - settings.set
  s = load()
  s[key] = value
  tmp = _path().with_suffix(".tmp")
  tmp.write_text(json.dumps(s, ensure_ascii=False, indent=2))
  tmp.replace(_path())
  log.info("설정 %s = %r", key, value)
