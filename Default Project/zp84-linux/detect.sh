#!/usr/bin/env bash
# detect.sh - identifikasi perangkat USB ZP 8.4 AMP di Linux
# Pakai:  sudo ./detect.sh
#
# Tujuannya: tahu persis device-nya muncul sebagai apa.
#   - ttyACM*  -> USB CDC-ACM serial  (paling gampang, pyserial langsung)
#   - ttyUSB*  -> USB-Serial adapter (FTDI/CH340/CP210x)
#   - tidak ada tty -> USB vendor-specific, perlu reverse USB descriptor

set -uo pipefail

hr() { printf '%s\n' "------------------------------------------------------------"; }

echo "=== 1. USB devices ==="
if command -v lsusb >/dev/null 2>&1; then
    lsusb
else
    echo "lsusb tidak ada -> apt install usbutils"
fi

hr
echo "=== 2. Serial devices (tty) ==="
ls -l /dev/ttyACM* /dev/ttyUSB* /dev/ttyS* 2>/dev/null || echo "(tidak ada)"

hr
echo "=== 3. Detail tiap ttyUSB/ttyACM ==="
for d in /dev/ttyACM* /dev/ttyUSB*; do
    [ -e "$d" ] || continue
    echo "--- $d ---"
    # driver + subsystem
    udevadm info -q property -n "$d" 2>/dev/null | grep -E 'ID_VENDOR=|ID_MODEL=|ID_VENDOR_ID=|ID_MODEL_ID=|ID_SERIAL=|ID_USB_DRIVER=' || echo "(udevadm tidak tersedia)"
    # baud default
    stty -F "$d" -a 2>/dev/null | head -1 || true
    echo
done

hr
echo "=== 4. CDC-ACM kernel log ==="
dmesg 2>/dev/null | grep -iE 'cdc_acm|usbserial|ch34|ftdi|cp210|pl2303' | tail -20 \
    || echo "(butuh root / journalctl -k)"

hr
echo "=== 5. Video/USB internal dari device tree ==="
for d in /sys/class/tty/ttyACM* /sys/class/tty/ttyUSB*; do
    [ -e "$d" ] || continue
    echo "--- ${d##*/} ---"
    readlink -f "$d/device/driver" 2>/dev/null
    cat "$d/device/../idVendor" 2>/dev/null | sed 's/^/  idVendor: 0x/'
    cat "$d/device/../idProduct" 2>/dev/null | sed 's/^/  idProduct: 0x/'
done

hr
echo "=== 6. USB device tree ==="
if [ -d /sys/bus/usb/devices ]; then
    for u in /sys/bus/usb/devices/*/; do
        [ -f "$u/idVendor" ] || continue
        v=$(cat "$u/idVendor"); p=$(cat "$u/idProduct")
        m=$(cat "$u/product" 2>/dev/null)
        i=$(cat "$u/iSerial" 2>/dev/null)
        [ "$m" ] && printf "%s:%s  %s  serial=%s\n" "$v" "$p" "$m" "$i"
    done
else
    echo "(tidak ada /sys/bus/usb)"
fi

hr
echo "=== 7. Hidraw (kalau ternyata HID) ==="
ls /dev/hidraw* 2>/dev/null || echo "(tidak ada hidraw)"

hr
echo "=== RINGKASAN CEPAT ==="
if ls /dev/ttyACM* >/dev/null 2>&1; then
    echo "-> USB CDC-ACM serial. Jalankan: ./zp84_log.py /dev/ttyACM0"
elif ls /dev/ttyUSB* >/dev/null 2>&1; then
    echo "-> USB-Serial adapter. Jalankan: ./zp84_log.py /dev/ttyUSB0"
else
    echo "-> BUKAN serial. Perlu reverse USB descriptor (libusb + sniff URB)."
fi
