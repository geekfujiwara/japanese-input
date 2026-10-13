#!/bin/bash
# Builds the Mac App Store version of AstelioIME.app, signs it for the store and uploads it to App Store Connect
# (it then appears in TestFlight). Run it on the developer's Mac: the API key stays there, and this public repository
# has no signing workflow.
#
#   tools/release-astelio-mac-appstore.sh --build-number N [--version X.Y.Z] [--tester EMAIL] [--for-review] [--no-upload]
#
# --for-review keeps the signing certificates valid: App Review rejects a build whose certificate was revoked
# (ITMS-90238 / ITMS-90721). Revoke them after the version is released (the script prints how).
#
# --version is the App Store version (default: the version of CMakeLists.txt, which the Windows version shares).
#
# Needs ASC_KEY_ID, ASC_ISSUER_ID and ASC_KEY_PATH (an App Store Connect API key with the Admin role), Xcode, CMake
# and XcodeGen. The signing certificates and the profile are made for this run by tools/asc_mac_release.py and
# revoked at the end; nothing is left in the login keychain.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
bundle_id="com.geekfujiwara.inputmethod.Astelio"
build_number=""
version_override=""
tester=""
upload=1
keep_signing=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-number) build_number="$2"; shift ;;
        --version) version_override="$2"; shift ;;
        --tester) tester="$2"; shift ;;
        --no-upload) upload=0 ;;
        --for-review) keep_signing="--keep" ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
if [[ -z "$build_number" ]]; then
    echo "--build-number is required (it must be larger than every build uploaded before)." >&2
    exit 2
fi
: "${ASC_KEY_ID:?}" "${ASC_ISSUER_ID:?}" "${ASC_KEY_PATH:?}"

version="$(sed -n 's/^project(AstelioIME VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
if [[ -n "$version_override" ]]; then
    version="$version_override"
fi
macos="$root/platform/macos"
work="$root/build/macos-appstore"
rm -rf "$work"
mkdir -p "$work"
state="$work/signing.json"
keychain="$work/signing.keychain-db"
keychain_password="$(openssl rand -hex 24)"
helper=(python3 "$root/tools/asc_mac_release.py")

original_keychains=()
while IFS= read -r line; do
    line="${line#"${line%%[![:space:]]*}"}"
    original_keychains+=("${line//\"/}")
done < <(security list-keychains -d user)

cleanup() {
    "${helper[@]}" cleanup --state "$state" $keep_signing || true
    security list-keychains -d user -s "${original_keychains[@]}" > /dev/null 2>&1 || true
    security delete-keychain "$keychain" > /dev/null 2>&1 || true
}
trap cleanup EXIT

"$root/tools/build-astelio-mac.sh" --prepare-only

echo "== Signing identities and profile"
security create-keychain -p "$keychain_password" "$keychain"
security set-keychain-settings -lut 21600 "$keychain"
security unlock-keychain -p "$keychain_password" "$keychain"
security list-keychains -d user -s "$keychain" "${original_keychains[@]}"
"${helper[@]}" prepare --bundle-id "$bundle_id" --keychain "$keychain" --keychain-password "$keychain_password" \
    --state "$state"
read_state() {
    python3 -c "import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])" "$state" "$1"
}
team="$(read_state team_id)"
profile="$(read_state profile_name)"
application_sha1="$(read_state application_sha1)"
installer_sha1="$(read_state installer_sha1)"

echo "== Archiving $version ($build_number)"
archive="$work/AstelioIME.xcarchive"
xcodebuild archive -quiet -project "$macos/AstelioIME.xcodeproj" -scheme AstelioIME -configuration Release \
    -destination 'generic/platform=macOS' -archivePath "$archive" \
    MARKETING_VERSION="$version" CURRENT_PROJECT_VERSION="$build_number" \
    ASTELIO_ENTITLEMENTS=AstelioIME/AstelioIME-AppStore.entitlements ASTELIO_SIGN_IDENTITY="$application_sha1" \
    ASTELIO_PROFILE="$profile" ASTELIO_TEAM="$team" OTHER_CODE_SIGN_FLAGS="--keychain $keychain"

app="$archive/Products/Applications/AstelioIME.app"
codesign -dvv "$app" 2>&1 | grep -E "^Authority=Apple Distribution|^TeamIdentifier" || {
    echo "The archive is not signed with Apple Distribution." >&2
    exit 1
}
codesign -d --entitlements :- "$app" 2> /dev/null | grep -q "com.apple.security.app-sandbox" || {
    echo "The archive has no App Sandbox entitlement." >&2
    exit 1
}

echo "== Exporting the installer package"
cat > "$work/ExportOptions.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>method</key><string>app-store-connect</string>
	<key>destination</key><string>export</string>
	<key>teamID</key><string>$team</string>
	<key>signingStyle</key><string>manual</string>
	<key>signingCertificate</key><string>$application_sha1</string>
	<key>installerSigningCertificate</key><string>$installer_sha1</string>
	<key>provisioningProfiles</key>
	<dict><key>$bundle_id</key><string>$profile</string></dict>
	<key>manageAppVersionAndBuildNumber</key><false/>
</dict>
</plist>
PLIST
xcodebuild -exportArchive -archivePath "$archive" -exportOptionsPlist "$work/ExportOptions.plist" \
    -exportPath "$work/export"
package="$(ls "$work"/export/*.pkg | head -n 1)"
pkgutil --check-signature "$package" | head -n 4
cp "$package" "$root/build/AstelioIME-$version-$build_number-appstore.pkg"

if [[ $upload -eq 0 ]]; then
    echo "Built $package (not uploaded)."
    exit 0
fi

echo "== Uploading to App Store Connect"
mkdir -p "$HOME/.appstoreconnect/private_keys"
key_copy="$HOME/.appstoreconnect/private_keys/AuthKey_${ASC_KEY_ID}.p8"
if [[ ! -f "$key_copy" ]]; then
    cp "$ASC_KEY_PATH" "$key_copy"
    chmod 600 "$key_copy"
fi
xcrun altool --upload-app -t macos -f "$package" --apiKey "$ASC_KEY_ID" --apiIssuer "$ASC_ISSUER_ID"

echo "== Waiting for App Store Connect to process the build"
args=(testflight --state "$state" --build "$build_number")
if [[ -n "$tester" ]]; then
    args+=(--tester "$tester")
fi
"${helper[@]}" "${args[@]}"
