#!/usr/bin/env bash
# detect-hid.sh - cari perangkat USB HID (ZP 8.4 AMP) + dump report descriptor
# Pakai:  chmod +x detect-hid.sh && sudo ./detect-hid.sh > report.txt
#
# Output ini yang perlu dikirim ke下一步 analisis: contains
#   - VID/PID + nama device
#   - report descriptor mentah (dalam hex)
#   - hasil decode kalau hid-decode terpasang

set -uo pipefail
hr() { printf '%s\n' "============================================================"; }

hr; echo "=== 1. Daftar semua HID di sistem ==="
if [ -d /sys/class/hidraw ]; then
    for h in /sys/class/hidraw/hidraw*; do
        [ -e "$h" ] || continue
        n=${h##*/}
        phys=$(cat "$h/device/report_descriptor" >/dev/null 2>&1 && echo ok || echo "GAGAL")
        bus=$(cat "$h/device/uevent" 2>/dev/null | grep -E '^HID_ID=|^HID_NAME=|^HID_UNIQ=|^HID_PHYS=')
        echo "--- $n (descriptor: $phys) ---"
        echo "$bus"
    done
else
    echo "tidak ada /sys/class/hidraw"
fi

hr; echo "=== 2. lsusb -v : bagian HID ==="
if command -v lsusb >/dev/null 2>&1; then
    for dev in /sys/bus/usb/devices/*/; do
        [ -f "$dev/idVendor" ] || continue
        # hanya device yang punya report_descriptor
        [ -e "${dev}report_descriptor" ] || continue
        v=$(cat "$dev/idVendor"); p=$(cat "$dev/idProduct")
        echo ">>> $v:$p  $(cat "$dev/product" 2>/dev/null)  $(cat "$dev/manufacturer" 2>/dev/null)"
        # lsusb -v butuh root untuk descriptor lengkap
        lsusb -v -d "$v:$p" 2>/dev/null | \
            awk '/Interface Descriptor/,0' | \
            sed -n '1,/^$/p;/HID Device Descriptor/,/^$/p;/Report Descriptors:/,$p' | head -60
    done
else
    echo "lsusb tidak ada -> apt install usbutils"
fi

hr; echo "=== 3. Report descriptor (hex) per hidraw ==="
for h in /sys/class/hidraw/hidraw*; do
    [ -e "$h" ] || continue
    n=${h##*/}
    echo "--- $n ---"
    od -An -tx1 -v "$h/device/report_descriptor" | tr -s ' ' | sed 's/^ //' | tr '\n' ' '
    echo
    echo
done

hr; echo "=== 4. Decode report descriptor (kalau hid-decode ada) ==="
if command -v hid-decode >/dev/null 2>&1; then
    for h in /sys/class/hidraw/hidraw*; do
        [ -e "$h" ] || continue
        echo "--- ${h##*/} ---"
        hid-decode "$h/device/report_descriptor"
    done
else
    echo "hid-decode tidak ada. Pasang hidrd:"
    echo "    sudo apt install hidrd        # Debian/Ubuntu"
    echo "    sudo dnf install hidrd        # Fedora"
    echo "    sudo pacman -S hidrd          # Arch"
fi

hr; echo "=== 5. Ringkasan ==="
echo "Jumlah hidraw : $(ls /sys/class/hidraw 2>/dev/null | wc -l)"
echo "Untuk|zP 8.4 AMP|, cari device yang:"
echo "  - nama/produk mengandung ZP, ZEVOX, AMP, DSP, atau SOUND"
echo "  - punya report descriptor cukup besar (>30 byte = ada report custom)"
echo "  - punya collection/application usage page 0xFF00 (vendor page)"
echo
echo "Kalau tidak ada yang cocok, jalankan:"
echo "  sudo ./detect-hid.sh > report.txt  lalu kirim isinya"
