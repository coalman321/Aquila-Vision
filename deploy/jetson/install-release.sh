#!/bin/bash
# Runs on the Jetson, as root, from deploy.sh: unpacks an uploaded release, checks it, switches
# /opt/aquila-vision/current to it and restarts the service, or puts the previous release back.
#
#   install-release.sh RELEASE SHA256 STAGE_DIR KEEP
set -uo pipefail

RELEASE=$1 SHA=$2 STAGE=$3 KEEP=$4
BASE=/opt/aquila-vision
DEST=$BASE/releases/$RELEASE

cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

echo "$SHA  $STAGE/release.tar.gz" | sha256sum -c - || { echo "ERROR: upload is corrupt"; exit 1; }

mkdir -p "$DEST.partial"
tar -xzf "$STAGE/release.tar.gz" -C "$DEST.partial" --strip-components=1 --no-same-owner || {
    echo "ERROR: could not unpack"; rm -rf "$DEST.partial"; exit 1; }
mv "$DEST.partial" "$DEST"

if ! bash "$STAGE/verify-jetson.sh" --tree "$DEST"; then
    echo "ERROR: preflight failed; the running release is untouched"
    rm -rf "$DEST"
    exit 1
fi

previous=$(readlink -f "$BASE/current" 2>/dev/null || true)
ln -sfn "$DEST" "$BASE/current.new"
mv -T "$BASE/current.new" "$BASE/current"

echo "==> Restarting aquila-vision"
systemctl restart aquila-vision

# The web server is the last of the launch node's first children to bind; give the rest a moment
# after it answers, and make sure the service didn't die and restart in between.
echo "==> Waiting for the web UI on :5800 (up to 60 s)"
healthy=0
for i in $(seq 1 60); do
    sleep 1
    systemctl is-active --quiet aquila-vision || continue
    if curl -fs -m 2 -o /dev/null http://localhost:5800/; then
        echo "    answered after ${i}s; checking it stays up"
        sleep 5
        systemctl is-active --quiet aquila-vision && healthy=1
        break
    fi
done

if [ "$healthy" -ne 1 ]; then
    echo "ERROR: $RELEASE did not come up. Last log lines:"
    journalctl -u aquila-vision -n 30 --no-pager
    if [ -n "$previous" ] && [ -d "$previous" ]; then
        echo "==> Rolling back to $(basename "$previous")"
        ln -sfn "$previous" "$BASE/current.new"
        mv -T "$BASE/current.new" "$BASE/current"
        systemctl restart aquila-vision
    else
        systemctl stop aquila-vision
    fi
    rm -rf "$DEST"
    exit 1
fi

echo "==> $RELEASE is running"
# Keep the newest $KEEP releases (the running one among them).
ls -1dt "$BASE"/releases/*/ 2>/dev/null | tail -n +$((KEEP + 1)) | while read -r old; do
    [ "$(readlink -f "$BASE/current")" = "${old%/}" ] || { echo "    removing $(basename "$old")"; rm -rf "$old"; }
done
