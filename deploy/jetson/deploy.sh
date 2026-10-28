#!/bin/bash
# Installs an Aquila Vision release on a Jetson set up with setup-jetson.sh, and rolls back to the
# previous release if the new one doesn't come up.
#
#   ./deploy.sh [-t TARBALL] [-k KEEP] [-u USER] HOST
#
# Run from the repository after `bazel build //deploy:aquila_vision`. Needs SSH access to HOST and
# sudo there. Layout on the Jetson:
#
#   /opt/aquila-vision/releases/<timestamp>-<sha>/   unpacked tarballs
#   /opt/aquila-vision/current -> releases/<...>     what aquila-vision.service runs
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TARBALL="$HERE/../../bazel-bin/deploy/aquila_vision_release.tar.gz"
KEEP=2
REMOTE_USER=""

usage() { sed -n '2,10p' "$0"; }
while getopts "t:k:u:h" opt; do
    case "$opt" in
        t) TARBALL=$OPTARG ;;
        k) KEEP=$OPTARG ;;
        u) REMOTE_USER=$OPTARG ;;
        h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -eq 1 ] || { usage >&2; exit 2; }
HOST=${REMOTE_USER:+$REMOTE_USER@}$1

[ -f "$TARBALL" ] || { echo "ERROR: $TARBALL not found (bazel build //deploy:aquila_vision)" >&2; exit 1; }
SHA=$(sha256sum "$TARBALL" | cut -d' ' -f1)
RELEASE="$(date -u +%Y%m%d-%H%M%S)-${SHA:0:8}"
STAGE="/tmp/aquila-deploy-$RELEASE"

echo "==> Uploading $(basename "$TARBALL") ($(du -h "$TARBALL" | cut -f1)) as release $RELEASE"
ssh "$HOST" "mkdir -p '$STAGE'"
# rsync resumes a dropped transfer of the multi-gigabyte archive; scp is the fallback.
if command -v rsync >/dev/null && ssh "$HOST" command -v rsync >/dev/null; then
    rsync --partial --info=progress2 "$TARBALL" "$HERE/verify-jetson.sh" "$HERE/install-release.sh" "$HOST:$STAGE/"
else
    scp "$TARBALL" "$HERE/verify-jetson.sh" "$HERE/install-release.sh" "$HOST:$STAGE/"
fi
ssh "$HOST" "mv '$STAGE/$(basename "$TARBALL")' '$STAGE/release.tar.gz'"

echo "==> Installing on $HOST"
# -t: sudo may want a password.
ssh -t "$HOST" sudo bash "$STAGE/install-release.sh" "$RELEASE" "$SHA" "$STAGE" "$KEEP"
