#!/bin/bash
# Builds the version of Astelio IME outside the Mac App Store: AstelioIME.app signed with Developer ID (hardened
# runtime, no App Sandbox), in an installer package that puts it in /Library/Input Methods, notarized and stapled.
#
#   tools/release-astelio-mac-developer-id.sh --version X.Y.Z --build-number N [--no-notarize]
#
# Needs, on the developer's Mac: the "Developer ID Application" and "Developer ID Installer" identities in the login
# keychain (only the Account Holder can create them: developer.apple.com > Certificates), Xcode, CMake, XcodeGen, and
# ASC_KEY_ID / ASC_ISSUER_ID / ASC_KEY_PATH for notarytool. Unlike the Mac App Store certificates, these are kept and
# never revoked: notarized software signed with a revoked Developer ID stops opening.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
version=""
build_number=""
notarize=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --version) version="$2"; shift ;;
        --build-number) build_number="$2"; shift ;;
        --no-notarize) notarize=0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
if [[ -z "$version" || -z "$build_number" ]]; then
    echo "--version and --build-number are required." >&2
    exit 2
fi

identity() {
    security find-identity -v -p "$1" | sed -n "s/.*) \([0-9A-F]\{40\}\) \"$2.*/\1/p" | head -n 1
}
application_sha1="$(identity codesigning "Developer ID Application")"
installer_sha1="$(identity basic "Developer ID Installer")"
if [[ -z "$application_sha1" || -z "$installer_sha1" ]]; then
    echo "The Developer ID Application and Developer ID Installer identities are not in the keychain." >&2
    exit 1
fi
team="$(security find-certificate -Z -a -c "Developer ID Application" | sed -n 's/.*(\([A-Z0-9]\{10\}\))".*/\1/p' | head -n 1)"

macos="$root/platform/macos"
work="$root/build/macos-developer-id"
rm -rf "$work"
mkdir -p "$work"

"$root/tools/build-astelio-mac.sh" --prepare-only

echo "== Archiving $version ($build_number)"
archive="$work/AstelioIME.xcarchive"
xcodebuild archive -quiet -project "$macos/AstelioIME.xcodeproj" -scheme AstelioIME -configuration Release \
    -destination 'generic/platform=macOS' -archivePath "$archive" \
    MARKETING_VERSION="$version" CURRENT_PROJECT_VERSION="$build_number" \
    ASTELIO_ENTITLEMENTS=AstelioIME/AstelioIME.entitlements ASTELIO_SIGN_IDENTITY="$application_sha1" \
    ASTELIO_PROFILE="" ASTELIO_TEAM="$team" OTHER_CODE_SIGN_FLAGS="--timestamp"

echo "== Exporting for Developer ID"
cat > "$work/ExportOptions.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>method</key><string>developer-id</string>
	<key>teamID</key><string>$team</string>
	<key>signingStyle</key><string>manual</string>
	<key>signingCertificate</key><string>$application_sha1</string>
</dict>
</plist>
PLIST
xcodebuild -exportArchive -archivePath "$archive" -exportOptionsPlist "$work/ExportOptions.plist" \
    -exportPath "$work/export"
app="$work/export/AstelioIME.app"
codesign --verify --deep --strict --verbose=2 "$app"
codesign -dvv "$app" 2>&1 | grep -E "^Authority=Developer ID Application|^Timestamp|flags=.*runtime" || true
codesign -d --entitlements :- "$app" 2> /dev/null | grep -q "app-sandbox" && {
    echo "The Developer ID version must not use the App Sandbox entitlements." >&2
    exit 1
}

echo "== Building the installer package"
root_dir="$work/pkgroot"
mkdir -p "$root_dir"
ditto "$app" "$root_dir/AstelioIME.app"
# Install exactly there: without this, the installer "relocates" the update to any other copy with the same bundle
# ID it finds (for example a TestFlight copy in /Applications).
pkgbuild --analyze --root "$root_dir" "$work/components.plist"
plutil -replace 0.BundleIsRelocatable -bool NO "$work/components.plist"
pkgbuild --root "$root_dir" --component-plist "$work/components.plist" --install-location "/Library/Input Methods" \
    --scripts "$root/tools/macos-pkg-scripts" --identifier "com.geekfujiwara.inputmethod.Astelio.pkg" \
    --version "$version.$build_number" "$work/component.pkg"
package="$root/build/Astelio-$version-$build_number.pkg"
# The installer's pages (tools/macos-pkg-resources): welcome with what happens and why, the license, a background
# with the icon, and the last page pointing to the setup wizard that postinstall opens.
productbuild --synthesize --package "$work/component.pkg" "$work/distribution.xml"
python3 - "$work/distribution.xml" "$version" <<'PY'
import sys
path, version = sys.argv[1], sys.argv[2]
text = open(path, encoding="utf-8").read()
pages = f"""    <title>Astelio {version}</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <license file="license.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <background file="background.png" alignment="bottomleft" scaling="none"/>
    <background-darkAqua file="background-dark.png" alignment="bottomleft" scaling="none"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <allowed-os-versions><os-version min="14.0"/></allowed-os-versions>
"""
text = text.replace('<options customize="never"', pages + '    <options customize="never"', 1)
text = text.replace('minSpecVersion="1"', 'minSpecVersion="2"', 1)
open(path, "w", encoding="utf-8").write(text)
PY
grep -q "<welcome" "$work/distribution.xml" || { echo "The distribution file has no welcome page." >&2; exit 1; }
productbuild --distribution "$work/distribution.xml" --resources "$root/tools/macos-pkg-resources" \
    --package-path "$work" --sign "$installer_sha1" --timestamp "$package"
pkgutil --check-signature "$package" | head -n 5

if [[ $notarize -eq 1 ]]; then
    : "${ASC_KEY_ID:?}" "${ASC_ISSUER_ID:?}" "${ASC_KEY_PATH:?}"
    echo "== Notarizing"
    xcrun notarytool submit "$package" --key "${ASC_KEY_PATH/#\~/$HOME}" --key-id "$ASC_KEY_ID" \
        --issuer "$ASC_ISSUER_ID" --wait
    xcrun stapler staple "$package"
    spctl -a -t install -vv "$package"
fi
echo "Built $package"
