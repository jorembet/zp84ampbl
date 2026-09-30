#!/usr/bin/env bash
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "run as root: sudo $0" >&2
    exit 1
fi

BUS="${1:-3}"
OUT="${2:-/tmp/zp84-usbmon.log}"
DAYSEC=10
mkdir -p /sys/kernel/debug/usb 2>/dev/null || true
mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug
modprobe usbmon || true

if [ ! -e "/sys/kernel/debug/usb/usbmon/${BUS}u" ]; then
    echo "no usbmon node for bus ${BUS}u; available:" >&2
    ls /sys/kernel/debug/usb/usbmon/ >&2
    exit 1
fi

echo "capturing bus ${BUS} -> ${OUT}"
echo "now run the vendor PC tool, do your thing, then press Ctrl-C here"
cat "/sys/kernel/debug/usb/usbmon/${BUS}u" > "$OUT" || true
echo "saved $(wc -l < "$OUT") lines to $OUT"
python3 tools/zpdecode.py "$OUT"
