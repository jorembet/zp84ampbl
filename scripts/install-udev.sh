#!/usr/bin/env bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "run as root: sudo $0" >&2
    exit 1
fi

cd "$(dirname "$0")/.."

echo "==> installing udev rule"
install -m 0644 scripts/99-zp84.rules /etc/udev/rules.d/99-zp84.rules
udevadm control --reload-rules
udevadm trigger --subsystem-match=hidraw

echo "==> building tools"
make -s tools || { echo "build failed" >&2; exit 1; }

echo
echo "done. verify with:"
echo "  ls -l /dev/zp84amp /dev/hidraw7"
echo "  tools/zpsniff find"
echo "  tools/zpsniff probe --cmd 0xD2 --page 0 --pages 1"
