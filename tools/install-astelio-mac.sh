#!/bin/bash
# Installs AstelioIME.app (or the zip of it) for this user and turns it on in the input sources.
#
#   tools/install-astelio-mac.sh [path/to/AstelioIME.app | path/to/AstelioIME-*-mac.zip]
#   tools/install-astelio-mac.sh --uninstall
#
# No administrator rights are needed: the app goes to ~/Library/Input Methods.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
target_dir="$HOME/Library/Input Methods"
target="$target_dir/AstelioIME.app"

stop_running() {
    # The input method server keeps running; stop it so the new copy is loaded.
    pkill -x AstelioIME 2> /dev/null || true
}

if [[ "${1:-}" == "--uninstall" ]]; then
    if [[ -d "$target" ]]; then
        "$target/Contents/MacOS/AstelioIME" --uninstall || true
        stop_running
        rm -rf "$target"
    fi
    echo "Removed Astelio IME. Your history and words stay in ~/Library/Application Support/AstelioIME."
    exit 0
fi

source="${1:-$root/build/macos-app/AstelioIME.app}"
if [[ ! -e "$source" ]]; then
    source="$root/artifacts/macos/AstelioIME.app"
fi
temp=""
if [[ "$source" == *.zip ]]; then
    temp="$(mktemp -d)"
    ditto -x -k "$source" "$temp"
    source="$temp/AstelioIME.app"
fi
if [[ ! -d "$source/Contents/MacOS" ]]; then
    echo "AstelioIME.app was not found (build it with tools/build-astelio-mac.sh or download it with tools/get-astelio-mac.sh)." >&2
    exit 1
fi

mkdir -p "$target_dir"
stop_running
rm -rf "$target"
ditto "$source" "$target"
[[ -n "$temp" ]] && rm -rf "$temp"
# Downloaded copies carry the quarantine flag, which keeps an unsigned input method from loading.
xattr -dr com.apple.quarantine "$target" 2> /dev/null || true

"$target/Contents/MacOS/AstelioIME" --install
cat <<'TEXT'

Installed to ~/Library/Input Methods/AstelioIME.app.
- If Astelio IME is not in the input menu yet, open System Settings > Keyboard > Text Input > Edit...,
  press +, choose Japanese and add "Astelio IME" (log out and in once if it is not listed).
- Right Command (single press) switches to Japanese, left Command to English.
TEXT
