#!/bin/bash
# 맥 : .app → .dmg   (host 폴더에서 bash packaging/build_mac.sh)
set -euo pipefail
cd "$(dirname "$0")/.."
PY=${PYTHON:-python3}
VER=$($PY -c "import pingpet; print(pingpet.__version__)")

$PY packaging/make_icons.py
$PY -m PyInstaller packaging/ping_pet.spec --noconfirm --distpath packaging/dist --workpath packaging/build/work

# dmg : 앱과 응용 프로그램 폴더 바로가기 (끌어다 놓아 설치)
STAGE=packaging/build/dmg
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp -R "packaging/dist/Ping Pet.app" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
OUT="packaging/dist/PingPet-$VER-macos.dmg"
rm -f "$OUT"
hdiutil create -volname "Ping Pet" -srcfolder "$STAGE" -ov -format UDZO "$OUT" >/dev/null
echo "$OUT"
