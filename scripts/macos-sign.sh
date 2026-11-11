#!/usr/bin/env bash
# Sign and notarize an already packaged .app. Used when the Apple secrets
# are configured on the repository. Rewrites the zip next to the unsigned one.
set -euo pipefail

APP="${1:?path to rocket-switcher-config.app}"
ZIP="${2:?path to the zip to replace}"

: "${APPLE_CERTIFICATE_BASE64:?}"
: "${APPLE_CERTIFICATE_PASSWORD:?}"
: "${APPLE_SIGNING_IDENTITY:?}"
: "${APPLE_NOTARY_KEY:?}"
: "${APPLE_NOTARY_KEY_ID:?}"
: "${APPLE_NOTARY_ISSUER:?}"

WORK="${RUNNER_TEMP:-$(mktemp -d)}"
KEYCHAIN="$WORK/raketa.keychain-db"
PASS="$(openssl rand -hex 16)"

cleanup() {
  security delete-keychain "$KEYCHAIN" 2>/dev/null || true
}
trap cleanup EXIT

echo "$APPLE_CERTIFICATE_BASE64" | base64 -D > "$WORK/cert.p12"
printf '%s\n' "$APPLE_NOTARY_KEY" > "$WORK/AuthKey.p8"

security create-keychain -p "$PASS" "$KEYCHAIN"
security set-keychain-settings -lut 21600 "$KEYCHAIN"
security unlock-keychain -p "$PASS" "$KEYCHAIN"
security import "$WORK/cert.p12" -k "$KEYCHAIN" -P "$APPLE_CERTIFICATE_PASSWORD" \
  -T /usr/bin/codesign -T /usr/bin/security
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$PASS" "$KEYCHAIN"
security list-keychains -d user -s "$KEYCHAIN" $(security list-keychains -d user | tr -d '"')

codesign --force --options runtime --timestamp --sign "$APPLE_SIGNING_IDENTITY" --deep "$APP"

NOTE="$WORK/notarize.zip"
ditto -c -k --keepParent "$APP" "$NOTE"
xcrun notarytool submit "$NOTE" \
  --key "$WORK/AuthKey.p8" \
  --key-id "$APPLE_NOTARY_KEY_ID" \
  --issuer "$APPLE_NOTARY_ISSUER" \
  --wait
xcrun stapler staple "$APP"

rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"
echo "Signed $ZIP"

DMG="${ZIP%.zip}.dmg"
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/rocket-switcher-dmg.XXXXXX")"
cp -R "$APP" "$STAGE/Ракета.app"
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "Ракета" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"
echo "Signed $DMG"
