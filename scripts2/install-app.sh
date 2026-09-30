#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="$HOME/.local/bin/zp84gui"
WEBBIN="$HOME/.local/bin/zp84web"
WEBROOT="$HOME/.local/share/zp84-dsp/web"
APPDIR="$HOME/.local/share/applications"
ICONDIR="$HOME/.local/share/icons/hicolor/scalable/apps"

echo "==> building"
make -s all

echo "==> installing to $BIN"
mkdir -p "$HOME/.local/bin" "$APPDIR" "$ICONDIR" "$WEBROOT"
install -m 0755 build/zp84gui "$BIN"
install -m 0644 build/zp84-ble.py "$HOME/.local/bin/zp84-ble.py"
install -m 0755 build/zp84web "$WEBBIN"
install -m 0644 web/index.html web/style.css web/app.js "$WEBROOT/"

echo "==> installing the desktop entry"
sed "s|@EXEC@|$BIN|; s|@ARGS@|--read-dsp|" scripts/zp84-dsp.desktop > "$APPDIR/zp84-dsp.desktop"
chmod 0644 "$APPDIR/zp84-dsp.desktop"
install -m 0644 scripts/zp84-dsp.svg "$ICONDIR/zp84-dsp.svg"

command -v update-desktop-database >/dev/null 2>&1 &&
    update-desktop-database "$APPDIR" 2>/dev/null || true
command -v gtk-update-icon-cache >/dev/null 2>&1 &&
    gtk-update-icon-cache -f -t "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

echo
echo "done."
echo "  menu entry : $APPDIR/zp84-dsp.desktop"
echo "  binary     : $BIN"
echo "  web        : $WEBBIN  (buka http://127.0.0.1:8085)"
echo
if [ ! -e /etc/udev/rules.d/99-zp84.rules ]; then
    echo "The app still needs permission to open the amplifier."
    echo "Run this once, as root:"
    echo
    echo "    sudo install -m 0644 $PWD/scripts/99-zp84.rules /etc/udev/rules.d/ && \\"
    echo "      sudo udevadm control --reload-rules && \\"
    echo "      sudo udevadm trigger --subsystem-match=hidraw"
    echo
    echo "Until then you can launch the app with sudo:"
    echo "    sudo $BIN"
fi
echo
echo "Launch it from the app menu, or run: $BIN"
