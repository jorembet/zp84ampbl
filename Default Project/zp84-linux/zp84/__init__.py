r"""
zp84/__init__.py - paket Linux untuk ZP 8.4 AMP.

Status: protocol BELUM lengkap. Yang sudah pasti (diverifikasi dari
205 frame tangkapan, lihat tools/verify.py):
  - panjang frame 65 byte
  - prefix 00 AE 1E 00
  - TX saja lewat WriteFile, tidak ada balasan RX
  - tiga frame polling: POLL_STATUS, POLL_INFO, POLL_SUB07

Yang belum: peta offset -> parameter DSP (Gain, Freq, Q, Delay, ...).
Offset itu hanya bisa dipetakan lewat capture diferensial: satu aksi UI
= satu frame, lalu bandingkan dengan frame polling idle.

Contoh:

    import zp84
    f = zp84.by_label("POLL_STATUS")     # bytes 65, siap kirim
    print(zp84.describe(f))

    with zp84.SerialTransport("/dev/ttyACM0") as t:
        t.send_frame(f)
"""

from .frames import (
    FRAME_LEN,
    KNOWN_FRAMES,
    POLL_INFO,
    POLL_STATUS,
    POLL_SUB07,
    Frame,
    all_frames,
    build,
    by_label,
    describe,
    diff,
    xor_checksum,
)
from .transport import (
    CHAR_NOTIFY_UUID,
    CHAR_WRITE_UUID,
    SERVICE_UUID,
    BleTransport,
    SerialTransport,
    Transport,
    TransportError,
    open_auto,
)

__version__ = "0.2.0"

__all__ = [
    # konstanta frame
    "FRAME_LEN",
    "KNOWN_FRAMES",
    "POLL_INFO",
    "POLL_STATUS",
    "POLL_SUB07",
    # frame
    "Frame",
    "all_frames",
    "build",
    "by_label",
    "describe",
    "diff",
    "xor_checksum",
    # transport
    "BleTransport",
    "SerialTransport",
    "Transport",
    "TransportError",
    "open_auto",
    "SERVICE_UUID",
    "CHAR_WRITE_UUID",
    "CHAR_NOTIFY_UUID",
]
