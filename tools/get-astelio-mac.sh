#!/bin/bash
# Downloads AstelioIME.app built by CI ("macOS input method" workflow) for the latest commit of a branch
# (default main) and installs it with install-astelio-mac.sh. Needs the GitHub CLI (brew install gh; gh auth login).
#
#   tools/get-astelio-mac.sh [branch]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
repository="geekfujiwara/japanese-input"
branch="${1:-main}"
workflow="macos.yml"
destination="$root/artifacts/macos"

sha="$(gh api "repos/$repository/commits/$branch" --jq .sha)"
run="$(gh run list --repo "$repository" --workflow "$workflow" --commit "$sha" --limit 1 --json databaseId,status,conclusion \
    --jq '.[0] | "\(.databaseId) \(.status) \(.conclusion)"')"
if [[ -z "$run" ]]; then
    echo "No macOS build for $branch ${sha:0:7} yet; try again in a minute." >&2
    exit 1
fi
read -r id status conclusion <<< "$run"
if [[ "$status" != "completed" ]]; then
    echo "Waiting for the macOS build of $branch ${sha:0:7} to finish..."
    gh run watch "$id" --repo "$repository" --exit-status > /dev/null || true
    conclusion="$(gh run view "$id" --repo "$repository" --json conclusion --jq .conclusion)"
fi
if [[ "$conclusion" != "success" ]]; then
    echo "The macOS build of $branch ${sha:0:7} did not succeed ($conclusion); nothing downloaded." >&2
    exit 1
fi

rm -rf "$destination"
mkdir -p "$destination"
gh run download "$id" --repo "$repository" -n astelio-ime-macos -D "$destination"
zip="$(ls "$destination"/AstelioIME-*-mac.zip | head -n 1)"
echo "Downloaded the build of $branch ${sha:0:7}."
"$root/tools/install-astelio-mac.sh" "$zip"
