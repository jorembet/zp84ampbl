r"""
tools/analyze-diff.py - dari log spyon jadi peta offset -> parameter.

Cara kerja:
  1. parse log hexdump dari spyon.py
  2. frame yang paling sering = BASELINE (polling idle)
  3. frame yang berbeda dari baseline = AKSI (user gerakkan kontrol)
  4. cetak offset mana yang berubah, supaya jadi kandidat parameter

Pakai:
    python tools/analyze-diff.py ../zp84-capture/capture-run1.txt
    python tools/analyze-diff.py recheck.txt --min 2
"""

import collections
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tools.tocapture import parse  # parser yang sama

FRAME_LEN = 65


def hexs(b: bytes) -> str:
    return " ".join(f"{x:02X}" for x in b)


def diffs(base: bytes, cur: bytes) -> list[tuple[int, int | None, int | None]]:
    out = []
    for i in range(max(len(base), len(cur))):
        a = base[i] if i < len(base) else None
        b = cur[i] if i < len(cur) else None
        if a != b:
            out.append((i, a, b))
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)

    src = sys.argv[1]
    min_count = 2
    if "--min" in sys.argv:
        min_count = int(sys.argv[sys.argv.index("--min") + 1])

    if not os.path.exists(src):
        sys.exit(f"tidak ada: {src}")

    frames = parse(src)
    if not frames:
        sys.exit("tidak ada frame terbaca. Format log harus dari spyon.py")

    print("=" * 66)
    print(f"SUMBER: {src}")
    print("=" * 66)
    print(f"frame total   : {len(frames)}")
    print(f"arah          : {sorted({d for _, d, _, _ in frames})}")
    print(f"handle        : {sorted({h for _, _, h, _ in frames})}")
    print(f"panjang frame : {sorted({len(b) for _, _, _, b in frames})}")

    counts = collections.Counter((d, h, b) for _, d, h, b in frames)
    (bd, bh, base), base_n = counts.most_common(1)[0]

    print("\n" + "-" * 66)
    print(f"BASELINE  ({base_n}x, {len(counts)} jenis unik total)")
    print("-" * 66)
    for (d, h, b), n in counts.most_common(8):
        mark = " <== baseline" if b == base else ""
        print(f"  {d} {h} len={len(b):3d} x{n:<5d}{mark}")
        print(f"    {hexs(b[:21])} | {'zero' if set(b[21:]) == {0} else 'data'}")

    # semua frame baseline (>= min_count)
    baseline_set = {b for (_, _, b), n in counts.most_common() if n >= min_count}
    print(f"\n  dianggap baseline: {len(baseline_set)} jenis (>= {min_count}x)")

    novel = [(t, d, h, b) for (t, d, h, b) in frames if b not in baseline_set]

    print("\n" + "-" * 66)
    print(f"FRAME AKSI (beda dari baseline): {len(novel)}")
    print("-" * 66)

    if not novel:
        print("\n  Tidak ada. Artinya: hanya polling idle yang terekam.")
        print("  Ulangi capture sambil BENDAK-bENDAK kontrol di app,")
        print("  satu aksi satu per satu dengan jeda ~2 detik.")
        return 0

    # kumpulkan offset yang pernah berubah,across semua frame aksi
    offset_hits = collections.Counter()
    per_frame = []

    for t, d, h, b in novel:
        dm = diffs(base, b)
        per_frame.append((t, d, h, b, dm))
        for off, _, _ in dm:
            offset_hits[off] += 1

    for i, (t, d, h, b, dm) in enumerate(per_frame, 1):
        shown = dm[:24]
        desc = "  ".join(
            f"@{off}:{'--' if o is None else format(o, '02X')}"
            f"->{'--' if n is None else format(n, '02X')}"
            for off, o, n in shown)
        more = "" if len(dm) <= 24 else f"  (+{len(dm) - 24} lagi)"
        print(f"\n  [{i:3d}] t={t:<9} {d} {h} len={len(b)}  ndiff={len(dm)}{more}")
        print(f"        {hexs(b[:21])}")
        print(f"        {desc}")

    print("\n" + "=" * 66)
    print("KANDIDAT OFFSET PARAMETER (diurutkan sering berubah)")
    print("=" * 66)
    print(f"  {'off':>4}  {'hex':>6}  {'baseline':>9}  {'n':>4}  nilai yang pernah muncul")
    for off, n in sorted(offset_hits.items()):
        vals = sorted({b[off] for _, _, _, b, dm in per_frame
                       if any(o == off for o, _, _ in dm)})
        shown = " ".join(format(v, "02X") for v in vals[:12])
        more = "" if len(vals) <= 12 else " ..."
        bv = base[off] if off < len(base) else None
        print(f"  {off:>4}  0x{off:04X}  {format(bv, '02X') if bv is not None else '--':>9}  "
              f"{n:>4}  {shown}{more}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
