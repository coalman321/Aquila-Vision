#!/bin/bash
# Bumps the release version the way `npm version` does: updates VERSION and the camera-app
# package.json, commits "Release vX.Y.Z" and tags it vX.Y.Z.
#
#   tools/bump_version.sh <major|minor|patch|X.Y.Z>
#
# Needs a clean working tree. Pushing is left to you: git push --follow-tags
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"

[ $# -eq 1 ] || { sed -n '2,8p' "$0" >&2; exit 2; }
[ -z "$(git status --porcelain)" ] || { echo "ERROR: working tree is not clean" >&2; exit 1; }

CURRENT=$(tr -d '[:space:]' < VERSION)
[[ $CURRENT =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || { echo "ERROR: VERSION '$CURRENT' is not X.Y.Z" >&2; exit 1; }
MAJOR=${BASH_REMATCH[1]} MINOR=${BASH_REMATCH[2]} PATCH=${BASH_REMATCH[3]}

case "$1" in
    major) NEW="$((MAJOR + 1)).0.0" ;;
    minor) NEW="$MAJOR.$((MINOR + 1)).0" ;;
    patch) NEW="$MAJOR.$MINOR.$((PATCH + 1))" ;;
    v[0-9]*) NEW=${1#v} ;;
    *) NEW=$1 ;;
esac
[[ $NEW =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "ERROR: '$NEW' is not X.Y.Z" >&2; exit 2; }
git rev-parse -q --verify "refs/tags/v$NEW" >/dev/null && { echo "ERROR: tag v$NEW already exists" >&2; exit 1; }

printf '%s\n' "$NEW" > VERSION
sed -i -E "0,/\"version\": \"[^\"]*\"/s//\"version\": \"$NEW\"/" camera_app/camera-app/package.json

git add VERSION camera_app/camera-app/package.json
git commit -q -m "Release v$NEW"
git tag -a "v$NEW" -m "Release v$NEW"
echo "$CURRENT -> $NEW; tagged v$NEW. Push with: git push --follow-tags"
