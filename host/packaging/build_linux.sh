#!/bin/bash
# 리눅스 : PyInstaller 폴더 → AppImage   (host 폴더에서 bash packaging/build_linux.sh)
set -euo pipefail
cd "$(dirname "$0")/.."
PY=${PYTHON:-python3}
VER=$($PY -c "import pingpet; print(pingpet.__version__)")
ARCH=$(uname -m)

$PY packaging/make_icons.py
$PY -m PyInstaller packaging/ping_pet.spec --noconfirm --distpath packaging/dist --workpath packaging/build/work

APPDIR=packaging/build/AppDir
rm -rf "$APPDIR" && mkdir -p "$APPDIR/usr/lib"
cp -R "packaging/dist/Ping Pet" "$APPDIR/usr/lib/ping-pet"
cp packaging/build/icon.png "$APPDIR/ping-pet.png"
cat > "$APPDIR/ping-pet.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Ping Pet
Comment=Slack 미응답 요청을 보드(답답이)에 보여 준다
Exec=ping-pet
Icon=ping-pet
Categories=Utility;
DESK
cat > "$APPDIR/AppRun" <<'RUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/lib/ping-pet/ping-pet" "$@"
RUN
chmod +x "$APPDIR/AppRun"

TOOL=packaging/build/appimagetool
if [ ! -x "$TOOL" ]; then
  curl -sSL -o "$TOOL" "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$ARCH.AppImage"
  chmod +x "$TOOL"
fi
OUT="packaging/dist/PingPet-$VER-linux-$ARCH.AppImage"
ARCH=$ARCH "$TOOL" --appimage-extract-and-run "$APPDIR" "$OUT"
echo "$OUT"
