r"""
tools/tocapture.py - ubah log spyon / capture-diff jadi dataset terstruktur.

Keluaran:
    <nama>.frames.bin   raw frame, dipisah newline
    <nama>.json         index: waktu, arah, handle, hex, kemunculan

Pakai:
    python tools/tocapture.py capture-run1.txt captures/run1
"""

import collections
import json
import os
import re
import sys


RE_WRITE = re.compile(
    r"^\[(?P<t>[\d.]+)\]\s+WRITE\s+h=(?P<h>0x[0-9a-fA-F]+)\s+n=(?P<n>\d+)\s*$")
RE_READ = re.compile(
    r"^\[(?P<t>[\d.]+)\]\s+READ\s+h=(?P<h>0x[0-9a-fA-F]+)\s+n=(?P<n>\d+)\s*$")
RE_HEXLINE = re.compile(r"^([0-9a-fA-F]{4})\s+((?:[0-9A-F]{2}\s+){1,16})\s*\|")


def parse(path):
    """Baca log hexdump gaya spyon -> [(t, dir, handle, bytes)]."""
    frames = []
    cur = None

    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")

            m = RE_WRITE.match(line) or RE_READ.match(line)
            if m:
                if cur:
                    frames.append(cur)
                cur = {
                    "t": float(m.group("t")),
                    "dir": "TX" if line.startswith("[") and "WRITE" in line else "RX",
                    "handle": m.group("h").lower(),
                    "expect": int(m.group("n")),
                    "data": bytearray(),
                }
                continue

            if cur is not None:
                hm = RE_HEXLINE.match(line)
                if hm:
                    hexpart = hm.group(2)
                    try:
                        cur["data"].extend(bytes.fromhex(hexpart))
                    except ValueError:
                        pass
                    continue
                # baris hex selesai -> frame selesai
                if cur["data"]:
                    frames.append(cur)
                cur = None

    if cur and cur["data"]:
        frames.append(cur)

    return [(f["t"], f["dir"], f["handle"], bytes(f["data"])) for f in frames]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)

    src = sys.argv[1]
    stem = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(src)[0]
    os.makedirs(os.path.dirname(stem) or ".", exist_ok=True)

    frames = parse(src)
    if not frames:
        sys.exit("tidak ada frame yang bisa diparse dari " + src)

    # ringkasan per frame unik
    counter = collections.Counter((d, h, b) for _, d, h, b in frames)
    index = {}
    for (d, h, b), n in counter.most_common():
        key = f"{d}_{h}_{b[:8].hex().upper()}"
        index[key] = {
            "direction": d,
            "handle": h,
            "length": len(b),
            "count": n,
            "hex": b.hex(" ").upper(),
        }

    meta = {
        "source": src,
        "total_frames": len(frames),
        "unique_frames": len(index),
        "handles": sorted({h for _, _, h, _ in frames}),
        "directions": sorted({d for _, d, _, _ in frames}),
        "first_t": frames[0][0],
        "last_t": frames[-1][0],
        "unique": index,
        "timeline": [
            {"t": t, "dir": d, "handle": h, "hex": b.hex(" ").upper()}
            for t, d, h, b in frames
        ],
    }

    with open(stem + ".json", "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)

    with open(stem + ".frames.bin", "wb") as f:
        for _, d, _, b in frames:
            tag = ord(d[0])          # 'T' atau 'R'
            f.write(bytes([tag]) + b + b"\n")

    print(f"{src}")
    print(f"  frame total    : {len(frames)}")
    print(f"  frame unik     : {len(index)}")
    print(f"  handle         : {', '.join(meta['handles'])}")
    print(f"  arah           : {', '.join(meta['directions'])}")
    print(f"  -> {stem}.json")
    print(f"  -> {stem}.frames.bin")
    print()
    for k, v in index.items():
        print(f"  [{v['direction']}] {v['handle']} len={v['length']} x{v['count']}")
        print(f"      {v['hex'][:110]}")


if __name__ == "__main__":
    main()
