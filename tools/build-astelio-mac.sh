#!/bin/bash
# Builds the macOS input method (Universal 2) on a Mac: the system dictionary from azooKey_dictionary_storage,
# the Xcode project (XcodeGen) and AstelioIME.app, signed ad hoc. CI runs the same script.
#
#   tools/build-astelio-mac.sh [--test] [--output DIR]
#
# Needs Xcode, CMake and XcodeGen (brew install cmake xcodegen). The app ends up in DIR (default build/macos-app),
# next to AstelioIME-<version>-mac.zip. Install it with tools/install-astelio-mac.sh.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
output="$root/build/macos-app"
run_tests=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --test) run_tests=1 ;;
        --output) output="$2"; shift ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

# The same pinned source as the Windows builds (.github/workflows/ci.yml).
AZOOKEY_COMMIT=4d418525b090cf49c219819d05a7e3cc2a4346eb
AZOOKEY_SHA256=9a4f66c3f4b6a80d59471eb7890209faf49a6c856e7a72c1a6f9b862266aa026

version="$(sed -n 's/^project(AstelioIME VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
macos="$root/platform/macos"
work="$root/build/macos-work"
mkdir -p "$work" "$output"

for tool in cmake xcodegen xcodebuild; do
    if ! command -v "$tool" > /dev/null; then
        echo "$tool is not installed (brew install cmake xcodegen; Xcode from the App Store)." >&2
        exit 1
    fi
done

dictionary="$macos/Dictionary/system.dic"
if [[ ! -f "$dictionary" ]]; then
    echo "== Building the system dictionary"
    archive="$work/azookey.tar.gz"
    if [[ ! -f "$archive" ]]; then
        curl -fsSL -o "$archive" "https://github.com/azooKey/azooKey_dictionary_storage/archive/${AZOOKEY_COMMIT}.tar.gz"
    fi
    echo "${AZOOKEY_SHA256}  $archive" | shasum -a 256 -c -
    rm -rf "$work/azookey"
    mkdir -p "$work/azookey"
    tar -xzf "$archive" -C "$work/azookey" --strip-components=1
    cmake -S "$root" -B "$work/tools" -DCMAKE_BUILD_TYPE=Release -DASTELIO_BUILD_TESTS=OFF > /dev/null
    cmake --build "$work/tools" --target astelio_dicbuild --parallel > /dev/null
    dicbuild="$work/tools/dictionary/tools/astelio_dicbuild"
    "$dicbuild" import-azookey "$work/azookey/Dictionary" "$work/words.tsv" "$work/connection.tsv"
    mkdir -p "$macos/Dictionary"
    "$dicbuild" build "$work/connection.tsv" "$dictionary.tmp" "$work/words.tsv"
    "$dicbuild" convert "$dictionary.tmp" わたしはにほんごです 私は日本語です
    mv "$dictionary.tmp" "$dictionary"
    cp "$work/azookey/LICENSE" "$macos/Dictionary/LICENSE-azooKey_dictionary_storage.txt"
fi

echo "== Generating the Xcode project"
(cd "$macos" && xcodegen generate --quiet)

settings=(MARKETING_VERSION="$version" CURRENT_PROJECT_VERSION="${ASTELIO_BUILD_NUMBER:-1}")

if [[ $run_tests -eq 1 ]]; then
    echo "== Testing the bridge (XCTest)"
    TEST_RUNNER_ASTELIO_TEST_DICTIONARY="$dictionary" xcodebuild test -quiet \
        -project "$macos/AstelioIME.xcodeproj" -scheme AstelioIME -destination 'platform=macOS' \
        -derivedDataPath "$work/derived-test" "${settings[@]}"
fi

echo "== Building AstelioIME.app (Universal 2)"
xcodebuild build -quiet -project "$macos/AstelioIME.xcodeproj" -scheme AstelioIME -configuration Release \
    -destination 'generic/platform=macOS' -derivedDataPath "$work/derived" "${settings[@]}"

app="$work/derived/Build/Products/Release/AstelioIME.app"
archs="$(lipo -archs "$app/Contents/MacOS/AstelioIME")"
echo "slices: $archs"
[[ "$archs" == *arm64* && "$archs" == *x86_64* ]]
[[ -f "$app/Contents/Resources/system.dic" ]]
codesign --verify --deep --strict "$app"

rm -rf "$output/AstelioIME.app"
ditto "$app" "$output/AstelioIME.app"
zip="$output/AstelioIME-$version-mac.zip"
rm -f "$zip"
ditto -c -k --keepParent "$output/AstelioIME.app" "$zip"
echo "Built $output/AstelioIME.app and $zip"
