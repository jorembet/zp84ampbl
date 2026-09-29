r"""
tools/verify.py - sanity check paket zp84 terhadap data capture asli.

Jalankan:  python tools/verify.py
"""

import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import zp84
from zp84 import FRAME_LEN, Frame, build, describe, diff

HERE = os.path.dirname(os.path.abspath(__file__))
CAPTURE = os.path.join(HERE, "..", "data", "captures", "run1-poll.json")


def main():
    print("=" * 62)
    print("VERIFY zp84 vs data capture")
    print("=" * 62)

    # 1. paket import
    print(f"\n[1] import zp84 {zp84.__version__}  OK")
    print(f"    exports: {len(zp84.__all__)} nama")

    # 2. muat capture
    if not os.path.exists(CAPTURE):
        print(f"\n[2] capture tidak ada: {CAPTURE}")
        print("    jalankan lebih dulu:  python tools/tocapture.py <log> data/captures/run1-poll")
        return 1
    with open(CAPTURE, encoding="utf-8") as f:
        cap = json.load(f)
    print(f"\n[2] capture loaded: {cap['total_frames']} frame, "
          f"{cap['unique_frames']} unik, arah={cap['directions']}")

    # 3. cocokkan tiap frame unik dengan KNOWN_FRAMES
    print(f"\n[3] klasifikasi frame unik:")
    matched = 0
    for key, v in cap["unique"].items():
        raw = bytes.fromhex(v["hex"])
        ok = len(raw) == FRAME_LEN
        label = zp84.KNOWN_FRAMES.get(raw, "UNKNOWN")
        if label != "UNKNOWN":
            matched += 1
        print(f"    {label:12s} len={len(raw):3d} {'OK' if ok else 'SALAH'}  "
              f"x{v['count']:<4d} subfunc=0x{raw[4]:02X}  body={raw[7:21].hex(' ').upper()}")
    print(f"    -> {matched}/{cap['unique_frames']} dikenali")

    # 4. round-trip builder
    print(f"\n[4] round-trip builder:")
    src = zp84.frames._mk("00 AE 1E 00 0F C8 80 0D 06 06 12 06 20 00 0C 00 "
                          "20 00 21 F5 4B 00")
    print(f"    panjang sumber = {len(src)} (harus 65)")
    rebuilt = build(subfunc=0x0F, body=src[7:21]).raw
    same = rebuilt == src
    print(f"    build(subfunc=0x0F, body=src[7:21]) == sumber : {same}")
    if not same:
        print(f"    expected {src.hex(' ').upper()}")
        print(f"    got      {rebuilt.hex(' ').upper()}")
        for off, a, b in diff(src, rebuilt):
            print(f"      @{off:02X}: {a} -> {b}")

    # 5. FieldSet read/write
    print(f"\n[5] accessor Frame:")
    f = Frame(raw=bytearray(src))
    print(f"    prefix       = {f.prefix.hex(' ').upper()}")
    print(f"    subfunc      = 0x{f.subfunc:02X}")
    print(f"    const_c8/80  = 0x{f.const_c8:02X} / 0x{f.const_80:02X}")
    print(f"    body(7..20)  = {f.body.hex(' ').upper()}")
    print(f"    tail all zero= {f.is_padding_only}")
    print(f"    classify     = {f.classify()}")
    f.set(7, 0x99)
    print(f"    set(7,0x99)  = {f.raw[7]:02X}  (revert test set(7,{src[7]:02X}))")
    f.set(7, src[7])

    # 6. XOR checksum (hipotesis, belum terkonfirmasi)
    print(f"\n[6] hipotesis checksum XOR byte 0..20:")
    for key, v in cap["unique"].items():
        raw = bytes.fromhex(v["hex"])
        print(f"    {zp84.KNOWN_FRAMES.get(raw,'UNKNOWN'):12s} "
              f"xor(0..20)=0x{zp84.frames.xor_checksum(raw):02X}  "
              f"byte21=0x{raw[21]:02X}")

    print("\n" + "=" * 62)
    print("OK" if matched == cap["unique_frames"] and same else "ADA YANG GAGAL")
    print("=" * 62)
    return 0


if __name__ == "__main__":
    sys.exit(main())
