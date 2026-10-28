#!/bin/bash
# Vendored unchanged from FRC-Team-4143/MW-Lib (MIT), scripts/jetson_setup/usb_automount.sh.
# Run by setup-jetson.sh; can also be run on its own.

if [ "$EUID" -ne 0 ]; then
    echo "This script must be run as root"
    exit 1
fi

# Verify that the non-root user is set
if [ -z "$SUDO_USER" ]; then
    echo "This script must be run with sudo from a non-root user"
    exit 1
fi

# Create mount point directory
MOUNT_POINT="/mnt/usb" # also have to change the systemd script 
mkdir -p $MOUNT_POINT

echo "Setting up USB automount configuration..."

# Create systemd mount template for USB devices
cat > /etc/systemd/system/usb-mount@.service <<'EOF'
[Unit]
Description=Mount USB device %i
After=dev-%i.device

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/bash -c 'mkdir -p /mnt/usb/%i && mount -o uid=1000,gid=1000,umask=000 /dev/%i /mnt/usb/%i'
ExecStop=/bin/bash -c 'umount /mnt/usb/%i && rmdir /mnt/usb/%i'
TimeoutSec=10
EOF

# Create udev rules for USB automount
cat > /etc/udev/rules.d/99-usb-automount.rules <<'EOF'
# USB Automount Rules
# Mount USB storage devices when inserted

# Rule for USB mass storage devices
ACTION=="add", SUBSYSTEMS=="usb", SUBSYSTEM=="block", ENV{ID_FS_USAGE}=="filesystem", ENV{ID_FS_TYPE}!="", ENV{DEVTYPE}=="partition", TAG+="systemd", ENV{SYSTEMD_WANTS}="usb-mount@%k.service"

# Unmount when device is removed
ACTION=="remove", SUBSYSTEMS=="usb", SUBSYSTEM=="block", ENV{DEVTYPE}=="partition", RUN+="/bin/systemctl stop usb-mount@%k.service"
EOF

# Create a helper script for manual mounting/unmounting
cat > /usr/local/bin/usb-mount-helper <<'EOF'
#!/bin/bash

# USB Mount Helper Script
# Usage: usb-mount-helper [mount|unmount|list] [device]

show_usage() {
    echo "Usage: $0 [mount|unmount|list] [device]"
    echo "  mount [device]   - Mount a USB device (e.g., sdb1)"
    echo "  unmount [device] - Unmount a USB device"
    echo "  list             - List available USB storage devices"
    exit 1
}

list_usb_devices() {
    echo "Available USB storage devices:"
    lsblk -o NAME,SIZE,FSTYPE,MOUNTPOINT | grep -E "(sd[a-z][0-9]|nvme[0-9]n[0-9]p[0-9])" | head -10
}

mount_device() {
    local device=$1
    if [ -z "$device" ]; then
        echo "Error: No device specified"
        show_usage
    fi
    
    if [ ! -b "/dev/$device" ]; then
        echo "Error: Device /dev/$device does not exist"
        exit 1
    fi
    
    systemctl start usb-mount@$device.service
    if [ $? -eq 0 ]; then
        echo "Successfully mounted /dev/$device to /mnt/usb/$device"
    else
        echo "Failed to mount /dev/$device"
        exit 1
    fi
}

unmount_device() {
    local device=$1
    if [ -z "$device" ]; then
        echo "Error: No device specified"
        show_usage
    fi
    
    systemctl stop usb-mount@$device.service
    if [ $? -eq 0 ]; then
        echo "Successfully unmounted /dev/$device"
    else
        echo "Failed to unmount /dev/$device"
        exit 1
    fi
}

case "$1" in
    mount)
        mount_device "$2"
        ;;
    unmount)
        unmount_device "$2"
        ;;
    list)
        list_usb_devices
        ;;
    *)
        show_usage
        ;;
esac
EOF

# Make the helper script executable
chmod +x /usr/local/bin/usb-mount-helper

# Set proper ownership for the mount point
chown $SUDO_USER:$SUDO_USER $MOUNT_POINT

# Reload udev rules
udevadm control --reload-rules
udevadm trigger

# Reload systemd daemon
systemctl daemon-reload

echo "USB automount setup complete!"
echo ""
echo "Features configured:"
echo "- USB devices will automatically mount to /mnt/usb/[device] when plugged in"
echo "- Devices will automatically unmount when removed"
echo "- Manual control available via: usb-mount-helper [mount|unmount|list] [device]"
echo ""
echo "Examples:"
echo "  usb-mount-helper list              # List available USB devices"
echo "  usb-mount-helper mount sdb1        # Manually mount /dev/sdb1"
echo "  usb-mount-helper unmount sdb1      # Manually unmount /dev/sdb1"
echo ""
echo "Mount point: $MOUNT_POINT"
echo "To test: plug in a USB storage device and check /mnt/usb/"