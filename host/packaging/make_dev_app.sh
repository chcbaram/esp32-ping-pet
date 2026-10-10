#!/bin/bash
# 개발용 중계 실행 앱 — 저장소의 코드(.venv)와 firmware/ping-pet/dist 의 최신 펌웨어로 돈다.
#   host 폴더에서 : bash packaging/make_dev_app.sh [옵션 ...]   (기본 옵션 --self-test -v)
#   ~/Applications/Ping Pet Dev.app 이 생긴다. Spotlight · Launchpad 에서 "Ping Pet Dev" 로 실행
#   이미 돌고 있으면 새로 띄우지 않는다. 로그는 ~/Library/Logs/ping-pet.log
set -euo pipefail
cd "$(dirname "$0")/.."
HOST=$(pwd)
ARGS=${*:-"--self-test -v"}
APP="$HOME/Applications/Ping Pet Dev.app"

[ -x "$HOST/.venv/bin/python" ] || { echo ".venv 가 없다 : python3 -m venv .venv && .venv/bin/pip install -r requirements.txt"; exit 1; }
.venv/bin/python packaging/make_icons.py >/dev/null

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp packaging/build/icon.icns "$APP/Contents/Resources/icon.icns"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Ping Pet Dev</string>
  <key>CFBundleIdentifier</key><string>io.github.chcbaram.ping-pet.dev</string>
  <key>CFBundleExecutable</key><string>run</string>
  <key>CFBundleIconFile</key><string>icon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>LSUIElement</key><true/>
</dict>
</plist>
PLIST

cat > "$APP/Contents/MacOS/run" <<RUN
#!/bin/bash
# 이미 돌고 있으면 두 번 띄우지 않는다 (보드 포트를 서로 잡는다)
if pgrep -f "$HOST/ping_pet.py" >/dev/null; then
  osascript -e 'display notification "이미 실행 중이에요 (메뉴 막대 아이콘)" with title "Ping Pet Dev"'
  exit 0
fi
cd "$HOST"
exec "$HOST/.venv/bin/python" "$HOST/ping_pet.py" $ARGS
RUN
chmod +x "$APP/Contents/MacOS/run"
touch "$APP"
echo "$APP"
