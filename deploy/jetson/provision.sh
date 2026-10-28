#!/bin/bash
# Copies this directory to a freshly flashed Jetson and runs setup-jetson.sh on it.
#
#   ./provision.sh [-u USER] [-t TEAM] [-o OCTET] [-r] HOST [SETUP_ARGS...]
#
# Run from the repository. Needs SSH access to HOST (see "Setting up SSH access" in the README) and sudo
# there. TEAM and OCTET default to setup-jetson.sh's own defaults. -r reboots the Jetson afterwards;
# anything after HOST (e.g. --skip-network, --no-hold) is passed to setup-jetson.sh as is.
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REMOTE_USER=""
TEAM=""
OCTET=""
REBOOT=0

usage() { sed -n '2,8p' "$0"; }
while getopts "u:t:o:rh" opt; do
    case "$opt" in
        u) REMOTE_USER=$OPTARG ;;
        t) TEAM=$OPTARG ;;
        o) OCTET=$OPTARG ;;
        r) REBOOT=1 ;;
        h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || { usage >&2; exit 2; }
HOST=${REMOTE_USER:+$REMOTE_USER@}$1
shift

SETUP_ARGS=(--yes)
[ -z "$TEAM" ] || SETUP_ARGS+=(--team "$TEAM")
[ -z "$OCTET" ] || SETUP_ARGS+=(--octet "$OCTET")
SETUP_ARGS+=("$@")

# One SSH connection is reused for every step below (opening a new one each time is slow on the Jetson).
CTL_DIR=$(mktemp -d /tmp/aquila-ssh.XXXXXX)
SSH_OPTS=(-o ConnectTimeout=15 -o ControlMaster=auto -o "ControlPath=$CTL_DIR/%C" -o ControlPersist=120)
trap 'ssh "${SSH_OPTS[@]}" -O exit "$HOST" 2>/dev/null || true; rm -rf "$CTL_DIR"' EXIT
rssh() { ssh "${SSH_OPTS[@]}" "$HOST" "$@"; }

echo "==> Connecting to $HOST"
rssh true || { echo "ERROR: can't ssh to $HOST" >&2; exit 1; }

# Ask for the sudo password once, up front, and check it. It goes to sudo -S on ssh's stdin, never on a
# command line. Skipped when the remote user has passwordless sudo.
SUDO_PASS=""
if ! rssh sudo -n true 2>/dev/null; then
    read -r -s -p "sudo password for $HOST: " SUDO_PASS
    echo
    echo "==> Checking sudo"
    printf '%s\n' "$SUDO_PASS" | rssh sudo -S -p "''" -v \
        || { echo "ERROR: sudo on $HOST rejected the password (message above)" >&2; exit 1; }
fi
as_root() { printf '%s\n' "$SUDO_PASS" | rssh sudo -S -p "''" "$@"; }

STAGE="/tmp/aquila-setup-$(date -u +%Y%m%d-%H%M%S)"
echo "==> Copying $HERE to $HOST:$STAGE"
rssh "mkdir -p '$STAGE'"
scp -q "${SSH_OPTS[@]}" "$HERE"/* "$HOST:$STAGE/"

echo "==> Running setup-jetson.sh ${SETUP_ARGS[*]}"
as_root bash "$STAGE/setup-jetson.sh" "${SETUP_ARGS[@]}"
rssh "rm -rf '$STAGE'"

if [ "$REBOOT" -eq 1 ]; then
    echo "==> Rebooting $HOST"
    as_root reboot || true  # the connection drops as it goes down
else
    echo "==> Done. Reboot the Jetson (or rerun with -r) for the network, console and runtime settings to apply."
fi
