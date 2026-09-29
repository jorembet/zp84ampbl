r"""
zp84/frames.py - codec frame untuk ZP 8.4 AMP (vendor: ZP 8.4AMP / ZEVOX)

DISIMPULKAN DARI DATA, bukan dari dokumentasi vendor. Yang "pasti" ditandai
dengan [OBS]. Yang "tebakan" ditandai dengan [?] dan harus diverifikasi
dengan capture diferensial.

Struktur frame (65 byte, selalu):

    offset  lebar  arti
    ------  -----  ---------------------------------------------------
    0       1      0x00  [OBS] selalu 0x00 pada semua frame yang tertangkap
    1       1      0xAE  [OBS] konstan -> kandidat opcode/prefix modul
    2       1      0x1E  [OBS] konstan (30). [?] panjang payload 30 byte?
    3       1      0x00  [OBS] selalu 0x00
    4       1      varying  [?] sub-fungsi / index blok
    5       1      0xC8  [OBS] konstan di kedua frame
    6       1      0x80  [OBS] konstan di kedua frame
    7..20   14     varying  [?] isi perintah
    21..64  44     0x00  [OBS] padding nol

Dua frame polling yang sudah terkonfirmasi:

    POLL_STATUS  00 AE 1E 00 0F C8 80 0D 06 06 12 06 20 00 0C 00
                 20 00 21 F5 4B 00
    POLL_INFO    00 AE 1E 00 09 C8 80 07 03 06 1C 00 01 F1 DF 00
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

FRAME_LEN = 65
PREFIX = bytes((0x00, 0xAE, 0x1E, 0x00))
PAD_BYTE = 0x00


# --------------------------------------------------------------- frame types

POLL_STATUS = "POLL_STATUS"
POLL_INFO = "POLL_INFO"
POLL_SUB07 = "POLL_SUB07"

def _mk(hexstr: str) -> bytes:
    """Bangun frame 65 byte dari hex, auto-pad ke panjang yang benar."""
    raw = bytes.fromhex(hexstr)
    if len(raw) > FRAME_LEN:
        raise ValueError(f"frame melebihi {FRAME_LEN} byte: {len(raw)}")
    return raw + bytes(FRAME_LEN - len(raw))


#: frame polling yang tertangkap (capture run1 + recheck)
KNOWN_FRAMES: dict[bytes, str] = {
    # subfunc 0x0F - 135x/154 di run1 (~3.4 Hz), rasio 7:1 thd POLL_INFO
    # body: 0D 06 06 12 06 20 00 0C 00 20 00 21 F5 4B
    _mk("00 AE 1E 00 0F C8 80 0D 06 06 12 06 20 00 0C 00 "
        "20 00 21 F5 4B 00"): POLL_STATUS,

    # subfunc 0x09 - muncul 19x. Body: 07 03 06 1C 00 01 F1 DF 00
    _mk("00 AE 1E 00 09 C8 80 07 03 06 1C 00 01 F1 DF 00"): POLL_INFO,

    # subfunc 0x07 - baru ketemu di recheck.txt (1x). body: 05 06 06 12 BE 46
    _mk("00 AE 1E 00 07 C8 80 05 06 06 12 BE 46 00"): POLL_SUB07,
}


# ------------------------------------------------------------------ checksum
# Belum ada checksum yang terkonfirmasi (semua framerecv tidak dibaca balik),
# jadi ini placeholder: XOR semua byte 0..20 lalu bandingkan dengan byte 21.

def xor_checksum(data: bytes) -> int:
    """XOR byte 0..20 (indeks 0 sampai 20 inklusif-ish)."""
    acc = 0
    for b in data[:21]:
        acc ^= b
    return acc


# ----------------------------------------------------------------- container

@dataclass
class Frame:
    raw: bytes
    label: str = "UNKNOWN"

    def __post_init__(self) -> None:
        if len(self.raw) != FRAME_LEN:
            raise ValueError(
                f"frame harus {FRAME_LEN} byte, dapat {len(self.raw)}")

    # -- field accessor -------------------------------------------------
    @property
    def prefix(self) -> bytes:
        return self.raw[0:4]

    @property
    def subfunc(self) -> int:
        """offset 4"""
        return self.raw[4]

    @property
    def const_c8(self) -> int:
        return self.raw[5]

    @property
    def const_80(self) -> int:
        return self.raw[6]

    @property
    def body(self) -> bytes:
        """offset 7..20 (14 byte)"""
        return self.raw[7:21]

    @property
    def tail(self) -> bytes:
        return self.raw[21:]

    @property
    def is_padding_only(self) -> bool:
        return set(self.tail) == {PAD_BYTE}

    def classify(self) -> str:
        return KNOWN_FRAMES.get(bytes(self.raw), self.label)

    # -- byte helpers ---------------------------------------------------
    def get(self, off: int, width: int = 1, signed: bool = False) -> int:
        if off < 0 or off + width > len(self.raw):
            raise IndexError(f"offset {off}+{width} di luar frame {len(self.raw)}")
        fmt = {1: "b", 2: "h", 4: "i"}[width]
        if not signed:
            fmt = fmt.upper()
        return struct.unpack_from(">" + fmt, self.raw, off)[0]

    def set(self, off: int, value: int, width: int = 1, signed: bool = False) -> None:
        if off < 0 or off + width > len(self.raw):
            raise IndexError(f"offset {off}+{width} di luar frame {len(self.raw)}")
        fmt = {1: "b", 2: "h", 4: "i"}[width]
        if not signed:
            fmt = fmt.upper()
        struct.pack_into(">" + fmt, self.raw, off, value)

    # -- debug ----------------------------------------------------------
    def hexdump(self, width: int = 16) -> str:
        out = []
        for off in range(0, len(self.raw), width):
            chunk = self.raw[off:off + width]
            hexpart = " ".join(f"{b:02X}" for b in chunk).ljust(width * 3 - 1)
            text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
            out.append(f"{off:04X}  {hexpart}  |{text}|")
        return "\n".join(out)

    def __repr__(self) -> str:
        return (f"<Frame {self.label} subfunc=0x{self.subfunc:02X} "
                f"body={self.body.hex(' ').upper()}>")


# ------------------------------------------------------------------ builder

def build(subfunc: int, body: bytes = b"", c8: int = 0xC8,
          c80: int = 0x80, length_byte: int = 0x1E) -> Frame:
    """
    Rakit frame 65 byte dari bagian-bagiannya.

    subfunc   -> offset 4
    body      -> mulai offset 7, dipotong/di-pad ke 14 byte
    """
    if len(body) > 14:
        raise ValueError("body maksimum 14 byte untuk slot offset 7..20")
    buf = bytearray(FRAME_LEN)
    buf[0:4] = PREFIX
    buf[4] = subfunc & 0xFF
    buf[5] = c8 & 0xFF
    buf[6] = c80 & 0xFF
    buf[7:7 + len(body)] = body
    # buf[2] = length_byte  <- sengaja dibiarkan default 0x1E
    del length_byte
    return Frame(raw=bytes(buf))


def by_label(label: str) -> bytes:
    """Ambil frame known berdasarkan labelnya.

    KNOWN_FRAMES di-key oleh byte frame, jadi tidak bisa dipakai
    KNOWN_FRAMES["POLL_STATUS"]. Gunakan fungsi ini.
    """
    for raw, lab in KNOWN_FRAMES.items():
        if lab == label:
            return raw
    raise KeyError(f"label frame tidak dikenal: {label!r} "
                   f"(yang ada: {sorted(set(KNOWN_FRAMES.values()))})")


def all_frames() -> list[tuple[str, bytes]]:
    """[(label, frame_bytes), ...] untuk semua frame yang dikenal."""
    return [(lab, raw) for raw, lab in KNOWN_FRAMES.items()]


# ---------------------------------------------------------------- utilities

def diff(a: bytes, b: bytes) -> list[tuple[int, int | None, int | None]]:
    """[(offset, a_byte, b_byte), ...] untuk dua frame."""
    out = []
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else None
        y = b[i] if i < len(b) else None
        if x != y:
            out.append((i, x, y))
    return out


def describe(data: bytes) -> str:
    """Ringkasan satu frame dalam bahasa manusia."""
    if len(data) != FRAME_LEN:
        return f"<bukan frame: {len(data)} byte>"
    f = Frame(raw=bytes(data)).classify()
    head = " ".join(f"{b:02X}" for b in data[:21])
    return f"{f:12s} | {head} | tail={'zero' if set(data[21:]) == {0} else 'data'}"
