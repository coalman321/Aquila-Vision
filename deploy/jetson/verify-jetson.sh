#!/bin/bash
# Checks that a Jetson can run Aquila Vision. Run on the Jetson (deploy.sh runs it before
# switching releases):
#
#   ./verify-jetson.sh                  the machine
#   ./verify-jetson.sh --tree DIR       the machine, and the unpacked release in DIR
#   ./verify-jetson.sh --running        the machine, and the running service
#
# Exits non-zero if any check failed; warnings (a camera that isn't plugged in) don't fail it.
set -uo pipefail

TREE=""
RUNNING=0
while [ $# -gt 0 ]; do
    case "$1" in
        --tree) TREE=${2:?--tree needs a directory}; shift 2 ;;
        --running) RUNNING=1; shift ;;
        -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done

failures=0
ok() { printf '  ok    %s\n' "$*"; }
warn() { printf '  WARN  %s\n' "$*"; }
fail() { printf '  FAIL  %s\n' "$*"; failures=$((failures + 1)); }

echo "Machine"
[ "$(uname -m)" = aarch64 ] && ok "aarch64" || fail "not aarch64: $(uname -m)"
if [ -f /etc/nv_tegra_release ] && grep -q "R39 (release), REVISION: 2.1" /etc/nv_tegra_release; then
    ok "Jetson Linux 39.2.1 (JetPack 7.2.1)"
else
    fail "not Jetson Linux 39.2.1: $(head -n1 /etc/nv_tegra_release 2>/dev/null || echo 'no /etc/nv_tegra_release')"
fi
if [ -r /proc/device-tree/model ]; then
    ok "$(tr -d '\0' < /proc/device-tree/model)"
fi
host_glibc=$(ldd --version | head -n1 | grep -oE '[0-9]+\.[0-9]+$')
ok "glibc $host_glibc"

# The driver side of CUDA comes from the host; the tarball brings the rest.
if python3 - <<'PY'
import ctypes, sys
try:
    cuda = ctypes.CDLL("libcuda.so.1")
except OSError as e:
    sys.exit("cannot load libcuda.so.1: %s" % e)
rc = cuda.cuInit(0)
if rc != 0:
    sys.exit("cuInit failed with %d" % rc)
count = ctypes.c_int()
cuda.cuDeviceGetCount(ctypes.byref(count))
if count.value < 1:
    sys.exit("no CUDA device")
v = ctypes.c_int()
cuda.cuDriverGetVersion(ctypes.byref(v))
print("driver supports CUDA %d.%d, %d device(s)" % (v.value // 1000, v.value % 1000 // 10, count.value))
PY
then ok "CUDA driver"; else fail "CUDA driver (above)"; fi

[ "$(cat /sys/module/usbcore/parameters/usbfs_memory_mb 2>/dev/null)" -ge 64 ] 2>/dev/null \
    && ok "usbfs_memory_mb >= 64" || warn "usbfs_memory_mb < 64 (aquila-runtime.service sets it at boot)"
if command -v lsusb >/dev/null; then
    if lsusb -d 2676: >/dev/null 2>&1; then ok "Basler camera on USB"; else warn "no Basler USB camera plugged in"; fi
fi
id aquila >/dev/null 2>&1 && ok "user aquila" || fail "user aquila (run setup-jetson.sh)"
[ -w /etc/app ] || [ "$(stat -c %U /etc/app 2>/dev/null)" = aquila ] && ok "/etc/app" || fail "/etc/app not owned by aquila"

if [ -n "$TREE" ]; then
    echo "Release $TREE"
    main="$TREE/runfiles/_main"
    [ -x "$TREE/bin/aquila_launch" ] && ok "bin/aquila_launch" || fail "bin/aquila_launch missing"

    # Every executable (rmw_zenohd, the zenoh router, is skipped: Fast DDS is the RMW), as the
    # launcher environment sees it: nothing may be unresolved, and glibc
    # is the only library the tree takes from the host.
    export LD_LIBRARY_PATH="$main/lib"
    unresolved=0
    checked=0
    while IFS= read -r -d '' f; do
        [ "$(head -c4 "$f" 2>/dev/null | tr -d '\0')" = $'\x7fELF' ] || continue
        checked=$((checked + 1))
        out=$(ldd "$f" 2>&1 | grep -E "not found|version .* not found") || true
        if [ -n "$out" ]; then
            unresolved=$((unresolved + 1))
            fail "${f#"$TREE"/}"
            echo "$out" | head -n 3 | sed 's/^/          /'
        fi
    done < <(find "$main" -maxdepth 4 -type f -perm -u+x -not -path '*/_solib_*' -not -path '*/python_3*' -not -name '*.so' -not -name '*.so.*' -not -name rmw_zenohd -print0)
    [ "$unresolved" -eq 0 ] && ok "$checked executables resolve all their libraries"

    # TensorRT is loaded by the detector at run time, and is the library most likely to be at
    # odds with the host's driver.
    nvinfer=$(find "$main" -name 'libnvinfer.so.10' -print -quit)
    if [ -n "$nvinfer" ]; then
        python3 -c "import ctypes,sys; ctypes.CDLL(sys.argv[1])" "$nvinfer" 2>/tmp/verify-nvinfer.err \
            && ok "libnvinfer loads" || fail "libnvinfer does not load: $(cat /tmp/verify-nvinfer.err)"
    else
        fail "libnvinfer.so.10 not in the release"
    fi
fi

if [ "$RUNNING" -eq 1 ]; then
    echo "Service"
    systemctl is-active --quiet aquila-vision && ok "aquila-vision is active" || fail "aquila-vision is not active"
    if curl -fsS -m 5 -o /dev/null http://localhost:5800/; then ok "web UI answers on :5800"; else fail "web UI does not answer on :5800"; fi
fi

echo
if [ "$failures" -eq 0 ]; then echo "All checks passed."; else echo "$failures check(s) failed."; fi
exit $((failures > 0))
