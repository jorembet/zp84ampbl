r"""
capture-diff.py - rekam frame kontrol ZP 8.4 AMP, hanya yang BERUBAH.

Masalah: app poll status tiap ~300ms dengan frame konstan, jadi log biasa
kebanjiran frame identik. Alat ini:
  1. belajar frame baseline (polling) lalu
  2. hanya mencetak frame yang beda dari baseline, lengkap dengan diff byte

Sifatnya: attach ke proses yang jalan, atau --spawn untuk launch app di
bawah Frida sehingga CreateFile ikut tertangkap (beri tau device path-nya).

Pakai:
    python capture-diff.py --seconds 180
    python capture-diff.py --spawn --seconds 180
    python capture-diff.py --seconds 120 --raw semua.bin
"""

import argparse
import collections
import re
import sys
import threading
import time

try:
    import frida
except ImportError:
    sys.exit("frida belum ada:  pip install frida-tools")


HOOK_JS = r"""
'use strict';
const t0 = Date.now();
function ts() { return ((Date.now() - t0) / 1000).toFixed(3); }
function emit(s) { send(s); }

function findExp(modName, expName) {
    let fn = null;
    try {
        fn = Process.getModuleByName(modName).getExportByName(expName);
    } catch (e) { fn = null; }
    if (fn === undefined) fn = null;
    if (fn) return fn;
    try { fn = Module.getGlobalExportByName(expName); } catch (e) { fn = null; }
    if (fn === undefined) fn = null;
    return fn;
}

function hexLines(buf) {
    const out = [];
    for (let off = 0; off < buf.length; off += 16) {
        const c = buf.slice(off, off + 16);
        const hex = [];
        for (let i = 0; i < c.length; i++) hex.push(c[i].toString(16).padStart(2, '0').toUpperCase());
        out.push(hex.join(' '));
    }
    return out.join('\n');
}

function w(p) { try { return p.readUtf16String(); } catch (e) { return null; } }

function hookOpen(mod, name) {
    const fn = findExp(mod, name);
    if (!fn) { emit(`MISS|${mod}!${name}`); return; }
    Interceptor.attach(fn, {
        onEnter(args) {
            this.p = name.endsWith('W') ? w(args[0]) : null;
            if (!this.p) { try { this.p = args[0].readAnsiString(); } catch (e) {} }
        },
        onLeave(r) {
            const h = r.toInt32();
            if (h === -1) return;
            const q = (this.p || '').toLowerCase();
            if (q.indexOf('usb') >= 0 || q.indexOf('hid') >= 0 || q.indexOf('com') >= 0 ||
                q.indexOf('bth') >= 0 || q.indexOf('bt') >= 0 || q.indexOf('rfc') >= 0) {
                emit(`OPEN|${this.p}|0x${h.toString(16)}`);
            }
        }
    });
}

function hookIo(mod, fname, dirn) {
    const fn = findExp(mod, fname);
    if (!fn) { emit(`MISS|${mod}!${fname}`); return; }
    Interceptor.attach(fn, {
        onEnter(args) {
            this.h = args[0].toInt32();
            this.buf = args[1];
            this.len = args[2].toInt32();
            if (this.len > 0 && this.len <= 4096) {
                const b = new Uint8Array(this.buf.readByteArray(this.len));
                emit(`${dirn}|${this.h}|${hexLines(b)}`);
            }
        }
    });
}

function hookIoctl(mod) {
    const fn = findExp(mod, 'DeviceIoControl');
    if (!fn) { emit(`MISS|${mod}!DeviceIoControl`); return; }
    Interceptor.attach(fn, {
        onEnter(args) {
            this.h = args[0].toInt32();
            this.code = args[1].toUInt32().toString(16).toUpperCase();
            this.buf = args[2];
            this.len = args[3].toInt32();
        },
        onLeave(r) {
            if (r.toInt32() === 0 || this.len <= 0 || this.len > 4096) return;
            const b = new Uint8Array(this.buf.readByteArray(this.len));
            emit(`IOCTL|${this.h}|${this.code}|${hexLines(b)}`);
        }
    });
}

setImmediate(function () {
    hookOpen('kernel32.dll', 'CreateFileW');
    hookOpen('kernel32.dll', 'CreateFileA');
    hookIo('kernel32.dll', 'WriteFile', 'W');
    hookIo('kernel32.dll', 'ReadFile',  'R');
    hookIoctl('kernel32.dll');
    emit('READY');
});
"""


def parse_frames(txt):
    """txt dari Frida -> list of (t, dir, handle, bytes)."""
    out = []
    for line in txt.split("\n"):
        parts = line.split("|")
        if len(parts) < 4:
            continue
        t, dirn, handle, hexstr = parts[0], parts[1], parts[2], "|".join(parts[3:])
        try:
            data = bytes.fromhex(hexstr.replace("\n", " "))
        except ValueError:
            continue
        if not data:
            continue
        out.append((t, dirn, handle, data))
    return out


def diff_vs(base, cur):
    """[(offset, base_byte, new_byte), ...]"""
    res = []
    for i in range(max(len(base), len(cur))):
        b = base[i] if i < len(base) else None
        n = cur[i] if i < len(cur) else None
        if b != n:
            res.append((i, b, n))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=180)
    ap.add_argument("--spawn", action="store_true",
                    help="launch app di bawah Frida (tangkap CreateFile)")
    ap.add_argument("-n", "--name", default="ZP 8.4 AMP_EN.exe")
    ap.add_argument("-o", "--out", default="diff.txt")
    ap.add_argument("--raw", help="simpan semua frame ke file binary")
    ap.add_argument("--warmup", type=int, default=25,
                    help="jumlah frame awal yang dipakai jadi baseline")
    args = ap.parse_args()

    dev = frida.get_local_device()
    pid, session = None, None

    if args.spawn:
        try:
            pid = dev.spawn([args.name])
        except Exception as exc:
            print(f"# gagal spawn '{args.name}': {exc}")
            print("# tutup dulu app yang sedang jalan, lalu coba lagi.")
            sys.exit(1)
        session = dev.attach(pid)
        print(f"# spawn {args.name} -> PID {pid}")
    else:
        pid = None
        for p in dev.enumerate_processes():
            if p.name.lower() == args.name.lower():
                pid = p.pid
                break
        if pid is None:
            cands = [p for p in dev.enumerate_processes()
                     if args.name.split(".")[0].lower() in p.name.lower()]
            if not cands:
                sys.exit(f"proses '{args.name}' tidak jalan")
            print("kandidat: " + ", ".join(f"{p.name}({p.pid})" for p in cands))
            pid = cands[0].pid
        session = dev.attach(pid)
        print(f"# attach ke PID {pid}")

    events = []
    lock = threading.Lock()

    def on_message(msg, data):
        if msg["type"] == "error":
            print("!! JS ERROR:", msg.get("description"), flush=True)
            return
        if msg["type"] != "send":
            return
        payload = msg.get("payload", "")
        if payload.startswith("MISS|"):
            print("!! hook gagal:", payload, flush=True)
            return
        if payload == "READY":
            print("!! semua hook terpasang, menunggu traffic", flush=True)
            return
        with lock:
            events.append(payload)

    script = session.create_script(HOOK_JS)
    script.on("message", on_message)
    script.load()

    if args.spawn:
        dev.resume(pid)

    print(f"# rekam {args.seconds} detik")
    print("# AKSIKAN sekarang: klik CH1..CH8, geser Gain, ubah Freq/Oct/Type,")
    print("# toggle mute, ubah Delay, ubah Volume, klik Reset/GEQ/Link Output")
    print("# Ctrl+C berhenti lebih awal\n")

    deadline = time.time() + args.seconds
    try:
        while time.time() < deadline:
            time.sleep(0.5)
    except KeyboardInterrupt:
        print("\n# stop")

    script.unload()
    session.detach()

    with lock:
        raw_all = list(events)

    # pisahkan
    opens = [e for e in raw_all if e.startswith("OPEN|")]
    frames = parse_frames("\n".join(raw_all))

    if opens:
        print("=== DEVICE YANG DIBUKA ===")
        seen = set()
        for o in opens:
            if o not in seen:
                seen.add(o)
                print("  " + o.replace("|", "   "))

    # baseline = frame yang paling sering
    if not frames:
        print("\n# tidak ada frame I/O")
        return

    counts = collections.Counter((d, h, by) for _, d, h, by in frames)
    (base_dir, base_h, base_bytes), base_n = counts.most_common(1)[0]
    print(f"\n=== BASELINE: {base_n}x  {base_dir} h={base_h} len={len(base_bytes)} ===")
    print("  " + base_bytes.hex(" ").upper())

    # semua baseline unik
    base_set = {b for (_, _, b), _ in counts.most_common(6)}
    print(f"  (total {len(base_set)} jenis frame baseline)")

    novel = [(t, d, h, b) for (t, d, h, b) in frames if b not in base_set]

    print(f"\n=== FRAME NON-BASELINE: {len(novel)} dari {len(frames)} ===\n")
    with open(args.out, "w", encoding="utf-8") as f:
        for t, d, h, b in novel:
            dm = diff_vs(base_bytes, b)
            desc = " ".join(f"@{i}:{('-' if o is None else format(o,'02X'))}"
                            f"->{('-' if n is None else format(n,'02X'))}"
                            for i, o, n in dm[:20])
            line = (f"[{t}] {d} h={h} len={len(b)}  ndiff={len(dm)}  {desc}")
            print(line)
            f.write(line + "\n")
            f.write("  " + b.hex(" ").upper() + "\n\n")

    print(f"\n# -> {args.out}")

    if args.raw:
        with open(args.raw, "wb") as f:
            for _, d, _, b in frames:
                f.write(bytes([ord(d)]) + b)
        print(f"# raw -> {args.raw}")


if __name__ == "__main__":
    main()
