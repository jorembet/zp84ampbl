#!/usr/bin/env python3
"""Decode ZP 8.4 AMP USB traffic captured with usbmon (text format).

The device speaks a vendor HID transfer protocol with no ACK/NAK, so the only
ground truth about the framing is the raw bytes on the wire. This tool pulls
the OUT (host->device) payloads out of a usbmon text capture, reassembles them
per URB, and tries to interpret the leading bytes as the Nuvoton CMD_T header:

    offset  0  u8   cmd
    offset  1  u8   len   (checksum coverage, always 14 in stock firmware)
    offset  2  u32  arg1  (LE)
    offset  6  u32  arg2  (LE)
    offset 10  u32  signature (LE)
    offset 14  u32  checksum (LE, plain sum of bytes 0..len-1)
"""

import sys
import struct
from collections import Counter, defaultdict

HEX2 = frozenset("0123456789abcdefABCDEF")


def parse_tokens(line):
    parts = line.split()
    if len(parts) < 2 or parts[0] != "Snoop":
        return None
    out = []
    for tok in parts[1:]:
        if len(tok) == 2 and all(c in HEX2 for c in tok):
            out.append(int(tok, 16))
        elif len(tok) == 4 and all(c in HEX2 for c in tok):
            b = int(tok, 16)
            out.append((b >> 8) & 0xFF)
            out.append(b & 0xFF)
        else:
            return None if out else None
    return bytes(out) if out else None


def iter_payloads(path):
    hdr = None
    pending = bytearray()

    with open(path, "r", errors="replace") as fh:
        for raw in fh:
            line = raw.rstrip("\n")
            if not line.strip():
                continue
            parts = line.split()
            if parts[0] != "Snoop":
                continue
            if len(parts) >= 3 and parts[1].isdigit() and parts[2] and ":" in parts[2]:
                hdr = parts
                pending = bytearray()
                continue
            data = parse_tokens(line)
            if data is not None:
                pending.extend(data)

    if pending:
        yield None, bytes(pending)


def try_cmdt(buf, sig_known):
    if len(buf) < 18:
        return None
    cmd, ln, arg1, arg2, sig, csum = struct.unpack_from("<BBIIIII", buf, 0)
    computed = sum(buf[:ln]) if 0 < ln <= 18 else None
    ok = computed is not None and computed == csum
    return {
        "cmd": cmd,
        "len": ln,
        "arg1": arg1,
        "arg2": arg2,
        "sig": sig,
        "csum": csum,
        "csum_ok": ok,
        "sig_known": sig in sig_known,
    }


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]

    sig_known = {0x43444948, 0x00000000, 0xFFFFFFFF, 0xAAAAAAAA, 0x55555555}

    payloads = list(iter_payloads(path))
    if not payloads:
        print("no payloads found - is this really a usbmon text capture?")
        return 1

    print(f"{len(payloads)} payload(s) recovered from {path}\n")

    sig_hist = Counter()
    cmd_hist = Counter()
    shapes = Counter()
    hits = []

    for _hdr, buf in payloads:
        p = try_cmdt(buf, sig_known)
        if p and p["csum_ok"]:
            hits.append((buf, p))
            sig_hist[p["sig"]] += 1
            cmd_hist[p["cmd"]] += 1
            shapes[(p["cmd"], p["len"], p["sig"])] += 1

    print("=== checksum-valid CMD_T frames ===")
    if not hits:
        print("none. The vendor firmware does not use the stock Nuvoton layout.")
        print("Falling back to raw dump of the first 8 payloads:\n")
        for i, (_h, buf) in enumerate(payloads[:8]):
            print(f"[{i}] len={len(buf)}")
            for off in range(0, min(len(buf), 128), 16):
                chunk = buf[off:off + 16]
                hexs = " ".join(f"{b:02x}" for b in chunk)
                txt = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
                print(f"    {off:04x}  {hexs:<47}  |{txt}|")
            print()
        return 2

    print(f"signatures seen: {', '.join(f'0x{k:08x}x{v}' for k, v in sig_hist.most_common())}")
    print(f"opcodes seen:    {', '.join(f'0x{k:02x}x{v}' for k, v in cmd_hist.most_common())}\n")

    print("=== distinct (opcode, len, signature) ===")
    for (cmd, ln, sig), n in shapes.most_common():
        print(f"  cmd=0x{cmd:02x} len={ln} sig=0x{sig:08x}  x{n}")

    print("\n=== first 40 valid frames ===")
    for buf, p in hits[:40]:
        extra = ""
        if p["cmd"] == 0xD2:
            extra = f"  <- READ page={p['arg1']} count={p['arg2']} ({p['arg2'] * 2048}B)"
        elif p["cmd"] == 0xC3:
            extra = f"  <- WRITE page={p['arg1']} count={p['arg2']} ({p['arg2'] * 2048}B)"
        elif p["cmd"] == 0x71:
            extra = f"  <- ERASE sector={p['arg1']} count={p['arg2']}"
        print(f"cmd=0x{p['cmd']:02x} arg1=0x{p['arg1']:08x} arg2=0x{p['arg2']:08x}"
              f" sig=0x{p['sig']:08x} csum=0x{p['csum']:08x}{extra}")
        tail = buf[18:50]
        if tail:
            print("    payload: " + " ".join(f"{b:02x}" for b in tail))

    return 0


if __name__ == "__main__":
    sys.exit(main())
