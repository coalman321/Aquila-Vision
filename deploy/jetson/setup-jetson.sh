#!/bin/bash
# One-time (and safe to repeat) configuration of a Jetson Orin Nano running JetPack 7.2.1 for
# Aquila Vision. Run on the Jetson, from this directory:
#
#   sudo ./setup-jetson.sh --team 4143 [--octet 200]
#
# Based on FRC-Team-4143/MW-Lib's scripts/jetson_setup/base_config.sh (MIT): the team file, text
# console, static IP and runtime settings are its steps, made repeatable and without Docker. The
# application itself is installed by deploy.sh.
set -euo pipefail

TEAM=4143
OCTET=200
ASSUME_YES=0
SKIP_NETWORK=0
FORCE=0
HOLD_NVIDIA=1
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

usage() {
    cat <<USAGE
Usage: sudo $0 [--team N] [--octet N] [--yes] [--skip-network] [--no-hold] [--force]
  --team N        FRC team number, 0-9999 (default $TEAM). Written to /etc/app/team and, with
                  --octet, gives the static address 10.TE.AM.<octet>.
  --octet N       Last octet of the static address, 1-254 (default $OCTET).
  --yes           Don't wait for confirmation.
  --skip-network  Leave NetworkManager and the ethernet configuration alone.
  --no-hold       Don't apt-mark hold the nvidia-l4t packages.
  --force         Continue on a release other than JetPack 7.2.1 (L4T 39.2.1).
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        --team) TEAM=${2:?--team needs a value}; shift 2 ;;
        --octet) OCTET=${2:?--octet needs a value}; shift 2 ;;
        --yes|-y) ASSUME_YES=1; shift ;;
        --skip-network) SKIP_NETWORK=1; shift ;;
        --no-hold) HOLD_NVIDIA=0; shift ;;
        --force) FORCE=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown argument $1" >&2; usage >&2; exit 1 ;;
    esac
done

die() { echo "ERROR: $*" >&2; exit 1; }
step() { echo "==> $*"; }

[ "$EUID" -eq 0 ] || die "run as root (sudo $0)"
[[ "$TEAM" =~ ^[0-9]+$ ]] && [ "$TEAM" -le 9999 ] || die "team number must be 0-9999"
[[ "$OCTET" =~ ^[0-9]+$ ]] && [ "$OCTET" -ge 1 ] && [ "$OCTET" -le 254 ] || die "octet must be 1-254"

# Writes stdin to $1 (mode $2, default 644) unless it already has that content. Returns 0 if changed.
install_file() {
    local dest=$1 mode=${2:-644} tmp
    tmp=$(mktemp)
    cat > "$tmp"
    if [ -f "$dest" ] && cmp -s "$tmp" "$dest"; then
        rm -f "$tmp"
        return 1
    fi
    install -D -m "$mode" "$tmp" "$dest"
    rm -f "$tmp"
    return 0
}

step "Checking the platform"
[ "$(uname -m)" = aarch64 ] || die "not an aarch64 machine"
if [ -f /etc/nv_tegra_release ]; then
    release=$(head -n1 /etc/nv_tegra_release)
    echo "    $release"
    # "# R39 (release), REVISION: 2.1, ..." is Jetson Linux 39.2.1, which is JetPack 7.2.1.
    if ! grep -q "R39 (release), REVISION: 2.1" /etc/nv_tegra_release; then
        [ "$FORCE" -eq 1 ] || die "this is not Jetson Linux 39.2.1 (JetPack 7.2.1); --force to continue anyway"
    fi
else
    [ "$FORCE" -eq 1 ] || die "/etc/nv_tegra_release is missing: not a Jetson running JetPack"
fi

TEAM_STR=$(printf "%04d" "$TEAM")
DEVICE_IP="10.${TEAM_STR:0:2}.${TEAM_STR:2:2}.${OCTET}"
SUBNET=${DEVICE_IP%.*}
ETH_INTERFACE=$(ip -o link show | awk '$2 ~ /^e/ {gsub(/[@:].*/, "", $2); print $2; exit}')
if [ "$SKIP_NETWORK" -eq 0 ] && [ -z "$ETH_INTERFACE" ]; then
    die "could not detect an ethernet interface (--skip-network to leave networking alone)"
fi

echo "------------------------------------------------------------------"
echo "Team number:        $TEAM"
if [ "$SKIP_NETWORK" -eq 0 ]; then
    echo "Device IP address:  $DEVICE_IP/24 on $ETH_INTERFACE (gateway ${SUBNET}.1)"
else
    echo "Networking:         unchanged"
fi
echo "------------------------------------------------------------------"
if [ "$ASSUME_YES" -eq 0 ]; then
    read -r -p "Press enter to continue with the configuration, or Ctrl+C to cancel "
fi

step "Creating the aquila service user"
getent group aquila >/dev/null || groupadd --system aquila
id -u aquila >/dev/null 2>&1 || \
    useradd --system --gid aquila --home-dir /var/lib/aquila --shell /usr/sbin/nologin aquila
for group in video render plugdev dialout; do
    getent group "$group" >/dev/null || groupadd --system "$group"
    usermod -aG "$group" aquila
done
# The user who ran sudo needs the serial ports too (as in MW-Lib's base_config.sh).
[ -z "${SUDO_USER:-}" ] || usermod -aG dialout "$SUDO_USER"

step "Creating directories"
# /etc/app is the application's data directory: its team file, camera sources, pipelines and
# calibrations (camera_webserver/launch/utils.py falls back to /tmp, which a reboot empties, if the
# service user can't write to it).
install -d -o aquila -g aquila -m 755 /etc/app /var/lib/aquila /var/lib/aquila/ros \
    /var/lib/aquila/engines /var/lib/aquila/logs
install -d -m 755 /opt/aquila-vision /opt/aquila-vision/releases /etc/aquila
echo "$TEAM" > /etc/app/team
chown aquila:aquila /etc/app/team
# Existing contents (from a previous setup, or restored) must belong to the service user too.
chown -R aquila:aquila /etc/app

if [ ! -f /etc/aquila/env ]; then
    cat > /etc/aquila/env <<'ENV'
# Environment of aquila-vision.service, on top of what bin/aquila_launch exports.
# The default ROS domain is used on purpose; don't set ROS_DOMAIN_ID.
ENV
fi

step "Disabling the desktop"
systemctl set-default multi-user.target

if [ "$SKIP_NETWORK" -eq 0 ]; then
    step "Configuring the static IP address for $ETH_INTERFACE"
    install_file /etc/systemd/network/20-wired.network <<NETWORK || true
# Static IP configuration for ethernet port
# Sets the IP address for this computer
[Match]
Name=${ETH_INTERFACE}

[Link]
RequiredForOnline=yes

[Network]
DHCP=no
LinkLocalAddressing=no
Address=${DEVICE_IP}/24
Gateway=${SUBNET}.1
DNS=8.8.8.8 1.1.1.1
NETWORK
    # Takes effect at the next boot, so this doesn't cut an SSH session that is running now.
    if systemctl list-unit-files NetworkManager.service >/dev/null 2>&1; then
        systemctl disable NetworkManager 2>/dev/null || true
        systemctl mask NetworkManager
    fi
    systemctl unmask systemd-networkd
    systemctl enable systemd-networkd
fi

step "Installing the USB3 Vision camera udev rule"
install_file /etc/udev/rules.d/99-basler-usb.rules <<'RULES' || true
# Basler USB3 Vision cameras: readable by the aquila service user (group plugdev).
SUBSYSTEM=="usb", ATTRS{idVendor}=="2676", MODE="0664", GROUP="plugdev"
RULES
udevadm control --reload-rules
udevadm trigger --subsystem-match=usb

step "Tuning the network stack for DDS"
# Fast DDS sends large messages (camera images) as UDP fragments; the kernel's default buffers and
# fragment reassembly limits drop them.
install_file /etc/sysctl.d/90-aquila-vision.conf <<'SYSCTL' || true
net.core.rmem_max = 2147483647
net.core.wmem_max = 2147483647
net.ipv4.ipfrag_time = 3
net.ipv4.ipfrag_high_thresh = 134217728
SYSCTL
sysctl --system >/dev/null

step "Installing aquila-runtime.service (power mode, clocks, fan, USB buffers)"
# MAXN is the power mode with every CPU and GPU clock unlocked. Its ID differs between modules and
# releases, so look it up rather than hard-coding it.
MAXN_ID=$(awk '/^< *POWER_MODEL +ID=/ { id=$0; sub(/.*ID=/, "", id); sub(/ .*/, "", id) }
               /NAME=.*MAXN/ { print id; exit }' /etc/nvpmodel.conf 2>/dev/null || true)
NVPMODEL_LINE=""
if [ -n "$MAXN_ID" ]; then
    NVPMODEL_LINE="ExecStart=/usr/sbin/nvpmodel -m ${MAXN_ID}"
else
    echo "    WARNING: no MAXN power mode in /etc/nvpmodel.conf; leaving the power mode alone"
fi
install_file /etc/systemd/system/aquila-runtime.service <<RUNTIME || true
[Unit]
Description=Aquila Vision Jetson runtime settings
After=network.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStartPre=/bin/sleep 5
${NVPMODEL_LINE}
# 16 MB is too little for USB3 Vision streaming; MW-Lib's base_config.sh uses 64.
ExecStart=/bin/sh -c 'echo 64 > /sys/module/usbcore/parameters/usbfs_memory_mb'
ExecStart=/usr/bin/jetson_clocks --fan

[Install]
WantedBy=multi-user.target
RUNTIME
# Replaces the unit MW-Lib's base_config.sh installs, which does the same minus the power mode.
if [ -f /etc/systemd/system/fan-full-speed.service ]; then
    systemctl disable fan-full-speed.service 2>/dev/null || true
    rm -f /etc/systemd/system/fan-full-speed.service
fi

step "Installing aquila-vision.service"
install_file /etc/systemd/system/aquila-vision.service < "$HERE/aquila-vision.service" || true
systemctl daemon-reload
systemctl enable aquila-runtime.service aquila-vision.service
# The service has nothing to run until deploy.sh installs a release.

if [ "$HOLD_NVIDIA" -eq 1 ]; then
    step "Holding the nvidia-l4t packages"
    # An unattended apt upgrade replacing the driver stack would leave the CUDA/TensorRT in the
    # tarball talking to a driver it wasn't tested with.
    mapfile -t l4t < <(dpkg-query -W -f='${Package}\n' 'nvidia-l4t-*' 2>/dev/null || true)
    [ "${#l4t[@]}" -eq 0 ] || apt-mark hold "${l4t[@]}" >/dev/null
fi

step "Setting up USB storage automount"
if [ -n "${SUDO_USER:-}" ]; then
    bash "$HERE/usb_automount.sh" >/dev/null
else
    echo "    skipped: usb_automount.sh needs SUDO_USER (run via sudo from a non-root user)"
fi

echo
echo "Setup complete. Reboot to apply the network, power and desktop changes, then run"
echo "deploy.sh from your workstation to install a release."
