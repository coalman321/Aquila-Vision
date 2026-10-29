<p align="center">
  <img src="assets/logo-full.svg" alt="Aquila" width="560">
</p>

# Aquila Vision

FRC 4143's GPU accelerated vision system, run on a Jetson Orin Nano. It reads Basler USB3 Vision cameras, detects AprilTags (and, optionally, objects with TensorRT) in each camera's images, and sends the results to the roboRIO over UDP. A web UI configures camera sources, pipelines, field maps and calibrations, and shows live video over WebRTC. It is built with Bazel on ROS 2 Lyrical and the in-house `basin` libraries, and deployed as a single tarball.

## Application Installation

### One-time setup of a Jetson

1. Flash JetPack 7.2.1. use NVIDIA's unified ISO from a USB stick. We also recommend you install to an NVME SSD for best performance.
2. From the repository, with [SSH access](#setting-up-ssh-access) to the Jetson, run
   `deploy/jetson/provision.sh -u <user> -t 4143 -o 200 -r <host>`. It copies [deploy/jetson/](deploy/jetson/) over, runs `setup-jetson.sh --yes` with those flags, and reboots (`-r`). Any extra arguments after `<host>` go to `setup-jetson.sh`. Or do it by hand: copy the directory to the Jetson and run, as a non-root user with sudo:

   ```
   sudo ./setup-jetson.sh --team 4143 --octet 200
   sudo reboot
   ```

   It is safe to run again. It performs the following: 
    - checks for L4T 39.2.1; creates the `aquila` service user; writes the team number to `/etc/app/team` and makes `/etc/app` (the app's cameras, pipelines and calibrations) owned by `aquila`;
    - boots to a text console; gives the Ethernet port the static address `10.TE.AM.<octet>` with systemd-networkd; 
    - installs a udev rule for Basler USB3 cameras; raises the UDP buffer limits Fast DDS needs; 
    - installs `aquila-runtime.service` (MAXN power mode, `jetson_clocks`, fan, `usbfs_memory_mb=64`);
    - holds the `nvidia-l4t-*` packages; sets up USB-stick automount (`/mnt/usb`); and installs and enables `aquila-vision.service`. `--skip-network` leaves networking alone; `--no-hold` leaves apt alone.

### Deploying a release

From the repository, with SSH and sudo access to the Jetson:

```
deploy/jetson/deploy.sh -u <user> <host>
```

It uploads the tarball, checks its SHA-256, unpacks it to `/opt/aquila-vision/releases/<time>-<sha>/`, runs [verify-jetson.sh](deploy/jetson/verify-jetson.sh) on it, then points `/opt/aquila-vision/current` at it and restarts `aquila-vision.service`. If the web UI (`:5800`) doesn't answer within 60 s, or the service dies, it puts the previous release back. The newest two releases are kept (`-k N`).

`verify-jetson.sh` can also be run by hand: on its own for the machine, with `--tree DIR` for an unpacked release, or with `--running` for the live service.

### What runs

`aquila-vision.service` runs `bin/aquila_launch`, which is `basin/launch`'s launch node
([launch.yaml](deploy/launch.yaml)). It loads `camera_webserver`'s two launch descriptions:

- `daemon.prod.py`: `camera_web` (web UI and API, port 5800), `proxy_client` (roboRIO bridge, UDP 5809),
  `webrtc_streamer_node` (port 5801), `camera_cal_node`.
- `application.prod.py`: for every enabled source in `/etc/app/camera_sources/*.json`, a `basler_driver` and a
  `detector_node` (AprilTags).

Each process's output goes to `/dev/shm/<name>.out`; the service's own is in `journalctl -u aquila-vision`.

### Setting up SSH access

`deploy.sh` makes several `ssh` and `rsync` connections per run, so set up key-based login first to avoid a password prompt for each. Do this once per developer machine (the devcontainer included).

1. Generate a key, if you don't have one already:

   ```
   ssh-keygen -t ed25519 -C "you@example.com"
   ```

   Accept the default path (`~/.ssh/id_ed25519`) and set a passphrase. Use `ssh-agent` so you only type it once per session.

2. Install the public key on the Jetson. This needs password login to still be enabled, which it is on a fresh JetPack install:

   ```
   ssh-copy-id -i ~/.ssh/id_ed25519.pub <user>@<host>
   ```

   `<host>` is the Jetson's static address from setup (`10.TE.AM.<octet>`, e.g. `10.41.43.200`) or its hostname. Without `ssh-copy-id`, append the contents of the `.pub` file to `~/.ssh/authorized_keys` on the Jetson, with `~/.ssh` at mode `700` and `authorized_keys` at `600`.

3. Check that it logs in without a password:

   ```
   ssh <user>@<host> true
   ```

Optionally add a host entry to `~/.ssh/config` so `deploy.sh` needs only the name:

```
Host jetson
    HostName 10.41.43.200
    User <user>
    IdentityFile ~/.ssh/id_ed25519
```

`deploy.sh` asks for the sudo password once, at the start, unless the user has passwordless sudo. Once key login works, you can turn off password logins by setting `PasswordAuthentication no` in `/etc/ssh/sshd_config` on the Jetson and running `sudo systemctl reload ssh`.

## Development

### Application Targets

Aquila Vision ships as one tarball that runs on a **Jetson Orin Nano with JetPack 7.2.1** (Jetson Linux 39.2.1, Ubuntu 24.04). The tarball carries its own CUDA, TensorRT, ROS and Python; the Jetson only supplies the kernel, the GPU driver (`libcuda`), glibc and `libstdc++`.

```
bazel build //deploy:aquila_vision          # bazel-bin/deploy/aquila_vision_release.tar.gz (~2.8 GB)
bazel build //deploy:aquila_vision_debug    # the same, unstripped, to symbolize a crash
```

Build on an aarch64 machine (the devcontainer is). The default CUDA architecture is Orin's `sm_87`
(`.bazelrc`); pass `--config=thor` to build for Jetson Thor instead.

### Compile Commands

Generate `compile_commands.json` for clangd and clang-tidy with:

```
bazel run //:refresh_compile_commands
```

Rerun it after adding or removing source files or changing build flags. External sources (ROS, LLVM) are left out; see [BUILD.bazel](BUILD.bazel).

### Releasing a version

The version lives in [VERSION](VERSION) and is shown on the UI's Status tab (served at `/api/version`). Bump it before building, like `npm version`; with a clean working tree:

```
tools/bump_version.sh patch    # or minor, major, or an explicit X.Y.Z
git push --follow-tags         # the commit "Release vX.Y.Z" and its tag vX.Y.Z
bazel build //deploy:aquila_vision
```
