#!/usr/bin/env bash
# Pack rocket-switcher-config.app after a Release build.
# Version comes from the first line of packaging/deb/changelog.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VER="$(sed -n 's/.*(\([^)]*\)).*/\1/p' packaging/deb/changelog | head -n 1)"
if [[ -z "$VER" ]]; then
  echo "Cannot read version from packaging/deb/changelog" >&2
  exit 1
fi

APP="$ROOT/build/src/gui/rocket-switcher-config.app"
if [[ ! -d "$APP" ]]; then
  echo "Missing $APP — configure with -DBUILD_GUI=ON and build Release first" >&2
  exit 1
fi
if ! command -v macdeployqt >/dev/null; then
  echo "macdeployqt is not on PATH (Qt bin directory)" >&2
  exit 1
fi

macdeployqt "$APP" -always-overwrite
mkdir -p "$APP/Contents/Resources/dict" "$APP/Contents/Resources/sounds"
cp "$ROOT/data/dict/en.txt" "$ROOT/data/dict/ru.txt" "$APP/Contents/Resources/dict/"
cp "$ROOT/data/sounds/switch.wav" "$APP/Contents/Resources/sounds/"

# Ad-hoc sign after bundling Qt and resources so Apple Silicon will launch
# a local unsigned build. Real identity/notarization is macos-sign.sh.
codesign --force --deep --sign - "$APP"

mkdir -p "$ROOT/dist"
OUT="$ROOT/dist/rocket-switcher_${VER}_macos.zip"
rm -f "$OUT"
ditto -c -k --keepParent "$APP" "$OUT"
echo "Wrote $OUT"

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/rocket-switcher-dmg.XXXXXX")"
cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT
cp -R "$APP" "$STAGE/Ракета.app"
ln -s /Applications "$STAGE/Applications"
DMG="$ROOT/dist/rocket-switcher_${VER}_macos.dmg"
rm -f "$DMG"
hdiutil create -volname "Ракета ${VER}" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
echo "Wrote $DMG"
