#!/usr/bin/env python3
"""Diff two flash/register dumps from the ZP 8.4 AMP and summarise the changes.

Workflow for reverse-engineering the register map:
  1. dump a baseline       -> tools/zpsniff read  --page 0 --pages N -o a.bin
  2. change ONE setting in the vendor tool (e.g. band 3 +2.0 dB)
  3. dump again            -> tools/zpsniff read  --page 0 --pages N -o b.bin
  4. diff                  -> tools/diffdump.py a.bin b.bin

Repeating step 2 for each control you care about produces a table of
address -> value that can be transcribed into chips/zp84.yaml.
"""

import sys
from collections import defaultdict


def runs(a, b):
    out = []
    start = None
    for i in range(min(len(a), len(b)) + 1):
        same = i < min(len(a), len(b)) and a[i] == b[i]
        if not same and start is None:
            start = i
        elif same and start is not None:
            out.append((start, i))
            start = None
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    with open(sys.argv[1], "rb") as f:
        a = f.read()
    with open(sys.argv[2], "rb") as f:
        b = f.read()

    if len(a) != len(b):
        print(f"warning: sizes differ ({len(a)} vs {len(b)}); comparing overlap")
    n = min(len(a), len(b))

    diffs = [i for i in range(n) if a[i] != b[i]]
    print(f"{len(diffs)} changed byte(s) out of {n}\n")

    if not diffs:
        print("no change - the setting you changed is not stored in this region,")
        print("or it lives in a different page/address range, or the device")
        print("needs a commit/flash step before it becomes readable.")
        return 0

    per_word = defaultdict(list)
    for i in diffs:
        per_word[i & ~0x3].append(i)

    print(f"{'offset':>8}  {'page':>5}  {'addr16':>7}  {'before':>16}  {'after':>16}")
    for w in sorted(per_word):
        for off in per_word[w]:
            ctx_a = a[max(0, off - 2):off + 6]
            ctx_b = b[max(0, off - 2):off + 6]
            print(f"0x{off:06x}  {off // 2048:>5}  0x{off & 0x7FF:04x}  "
                  f"{' '.join(f'{x:02x}' for x in ctx_a):>16}  "
                  f"{' '.join(f'{x:02x}' for x in ctx_b):>16}")

    print("\nchanged regions (contiguous):")
    for s, e in runs(a[:n], b[:n]):
        tag = "" if e - s > 1 else "  <- likely a single register"
        print(f"  0x{s:06x}-0x{e - 1:06x}  ({e - s} bytes, "
              f"{s // 2048 + 1} region){tag}")

    if len(diffs) <= 32:
        print("\nfull changed byte list (offset: before -> after):")
        print("  " + ", ".join(f"0x{i:06x}:{a[i]:02x}->{b[i]:02x}" for i in diffs))

    return 0


if __name__ == "__main__":
    sys.exit(main())
