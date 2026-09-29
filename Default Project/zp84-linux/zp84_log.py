#!/usr/bin/env python3
"""
zp84_log.py - logger traffic serial USB untuk ZP 8.4 AMP (ZEVOX).

Tujuan: menangkap bytes yang dikirim app Windows <-> amplifier, untuk
dipakai merekonstruksi protocol di Linux.

Cara pakai:
    pip install pyserial
    ./zp84_log.py /dev/ttyACM0                    # autodetect baud
    ./zp84_log.py /dev/ttyACM0 --baud 115200
    ./zp84_log.py /dev/ttyACM0 --out cap.bin      # simpan raw binary
    ./zp84_log.py /dev/ttyACM0 --seconds 30       # rekam 30 detik lalu stop
    ./zp84_log.py /dev/ttyACM0 --analyze cap.bin  # analisis file rekaman

Tanda yang bagus: klik CH1..CH8 satu per satu, geser Gain, ubah HPF/LPF.
Setiap aksi harus memunculkan burst byte baru.
"""

import argparse
import collections
import os
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial belum ada. Jalankan:  pip install pyserial")


BAUD_CANDIDATES = [115200, 9600, 38400, 57600, 19200, 230400, 1000000, 4800]


# ---------------------------------------------------------------- hexdump

HEXDIG = "0123456789ABCDEF"


def hexdump(data, base=0, width=16):
    """Hex dump ala xxd, plus kolom ASCII."""
    out = []
    for off in range(0, len(data), width):
        chunk = data[off:off + width]
        hexpart = " ".join(f"{b:02X}" for b in chunk)
        # kolom ke-2 untuk offset lebar >= 0x100
        hexpart = hexpart.ljust(width * 3 - 1)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append(f"{base + off:08X}  {hexpart}  |{text}|")
    return "\n".join(out)


# ------------------------------------------------------- framing analysis

def looks_like_frame(b):
    """Heuristik: b Probably satu frame lengkap (header + length byte)."""
    if len(b) < 3:
        return False
    # pola umum: 0xAA/0x55/0x5A 0x?? length
    if b[0] in (0xAA, 0x55, 0x5A, 0xA5) and b[1] in (0x00, 0xFF, 0xAA, 0x55):
        if len(b) > 2 and b[2] <= len(b) - 3:
            return True
    # marker ZEVOX / ZP
    for tag in (b"ZEVOX", b"ZEV", b"ZP8", b"AMP"):
        if b[:len(tag)] == tag:
            return True
    return False


def analyze(path):
    """Statistik file rekaman: frekuensi byte, tebakan header, pola panjang."""
    with open(path, "rb") as f:
        data = f.read()

    print(f"file      : {path}")
    print(f"ukuran    : {len(data)} byte")
    if not data:
        return

    print("\n--- 20 byte paling sering muncul ---")
    for byte, n in collections.Counter(data).most_common(20):
        bar = "#" * max(1, n * 40 // len(data))
        printable = chr(byte) if 32 <= byte < 127 else "."
        print(f"  0x{byte:02X} {byte:3d} {printable}  {n:7d}  {bar}")

    print("\n--- kandidat header ---")
    for byte, n in collections.Counter(data).most_common(5):
        if n < len(data) * 0.01:
            continue
        print(f"\n  header 0x{byte:02X} (muncul {n}x) - 20 kemunculan pertama:")
        seen = 0
        for i, b in enumerate(data):
            if b != byte or i + 1 >= len(data):
                continue
            nxt = data[i + 1]
            seg = data[i:i + 16]
            print(f"    @{i:7d}  next=0x{nxt:02X} ({nxt:3d})  {seg.hex(' ')}")
            seen += 1
            if seen >= 20:
                break

    print("\n--- ASCII yang terbaca (string >= 6 karakter) ---")
    cur = bytearray()
    for b in data:
        if 32 <= b < 127:
            cur.append(b)
        else:
            if len(cur) >= 6:
                print("  " + cur.decode("ascii"))
            cur = bytearray()
    if len(cur) >= 6:
        print("  " + cur.decode("ascii"))

    print("\n--- 32 byte pertama & terakhir ---")
    print("head:")
    print(hexdump(data[:32]))
    print("tail:")
    print(hexdump(data[-32:], base=len(data) - 32))


# ---------------------------------------------------------------- capture

def capture(port, baud, args):
    try:
        ser = serial.Serial(port, baud, timeout=0.1)
    except serial.SerialException as exc:
        sys.exit(f"gagal buka {port}: {exc}")

    print(f"# {port} @ {baud}  ->  {ser.name}")
    print(f"# {time.strftime('%H:%M:%S')} mulai rekam. Ctrl+C untuk stop.\n")

    raw = open(args.out, "wb") if args.out else None
    total = 0
    frames = 0
    started = time.time()
    deadline = started + args.seconds if args.seconds else None

    try:
        while True:
            if deadline and time.time() > deadline:
                break

            waiting = ser.in_waiting
            if waiting == 0:
                time.sleep(0.02)
                continue

            chunk = ser.read(waiting)
            if not chunk:
                continue

            total += len(chunk)
            if frames:
                print()          # pemisah antar burst
            frames += 1

            ts = time.strftime("%H:%M:%S")
            ms = int((time.time() - started) * 1000)
            print(f"--- frame #{frames}  t=+{ms} ms  {ts}  len={len(chunk)}")
            print(hexdump(chunk))
            sys.stdout.flush()

            if raw:
                raw.write(chunk)
    except KeyboardInterrupt:
        print("\n# dihentikan user")
    finally:
        ser.close()
        if raw:
            raw.close()

    print(f"\n# selesai: {frames} burst, {total} byte, "
          f"{time.time() - started:.1f} detik")
    if args.out:
        print(f"# raw tersimpan di {args.out}")
        print(f"# analisis:  ./zp84_log.py {port} --analyze {args.out}")


def autodetect(port):
    """Coba beberapa baud rate, lihat mana yang mengalir data."""
    print("# autodetect baud ...", flush=True)
    best = None
    for baud in BAUD_CANDIDATES:
        try:
            ser = serial.Serial(port, baud, timeout=0.35)
        except serial.SerialException as exc:
            print(f"  {baud:>8} : gagal ({exc})")
            continue
        got = ser.read(64)
        ser.close()
        n = len(got)
        print(f"  {baud:>8} : {n} byte  {got[:24].hex(' ')}")
        if n and (best is None or n > best[1]):
            best = (baud, n)
    if best is None:
        sys.exit("tidak ada baud rate yang merespons")
    print(f"# pilih {best[0]} ({best[1]} byte)\n")
    return best[0]


def main():
    ap = argparse.ArgumentParser(description="Logger serial ZP 8.4 AMP")
    ap.add_argument("port", help="mis. /dev/ttyACM0, atau --analyze <file>")
    ap.add_argument("--baud", type=int, help="baud rate (default: autodetect)")
    ap.add_argument("--out", help="simpan raw binary ke file")
    ap.add_argument("--seconds", type=int, default=0,
                    help="rekam selama N detik lalu stop (0 = sampai Ctrl+C)")
    ap.add_argument("--list", action="store_true", help="daftar port serial")
    ap.add_argument("--analyze", metavar="FILE", help="analisis file rekaman")
    args = ap.parse_args()

    if args.list:
        ports = list(list_ports.comports())
        if not ports:
            print("tidak ada port serial")
        for p in ports:
            print(f"{p.device}  {p.description}  [{p.hwid}]")
        return

    if args.analyze:
        if not os.path.exists(args.analyze):
            sys.exit(f"file tidak ada: {args.analyze}")
        analyze(args.analyze)
        return

    if not os.path.exists(args.port):
        sys.exit(f"port tidak ada: {args.port}\n"
                 f"coba:  ./zp84_log.py --list")

    baud = args.baud or autodetect(args.port)
    capture(args.port, baud, args)


if __name__ == "__main__":
    main()
