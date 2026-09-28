#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="$HOME/.local/bin/zp84gui"
APPDIR="$HOME/.local/share/applications"
ICONDIR="$HOME/.local/share/icons/hicolor/scalable/apps"

echo "==> building"
make -s tools
gcc -std=c11 -Wall -Wextra -O2 -Isrc -o build/zp84gui \
    src/gui/zp84gui.c src/dsp/biquad.c src/dsp/design.c src/hid/zp_hid.c \
    -lX11 -lm

echo "==> installing to $BIN"
mkdir -p "$HOME/.local/bin" "$APPDIR" "$ICONDIR"
install -m 0755 build/zp84gui "$BIN"

echo "==> installing the desktop entry"
sed "s|@EXEC@|$BIN|; s|@ARGS@||" scripts/zp84-dsp.desktop > "$APPDIR/zp84-dsp.desktop"
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
