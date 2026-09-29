r"""
spyon.py - rekam I/O device yang dipakai proses ZP 8.4 AMP (Delphi, Win32).

Cara kerja: attach ke proses yang SEDANG BERJALAN, lalu hook API Windows
yang dipakai untuk bicara dengan hardware:

    CreateFileW/A        -> path device yang dibuka (\\?\usb#..., \\.\HID#..., \\.\COM4)
    ReadFile / WriteFile -> byte yang keluar & masuk
    DeviceIoControl      -> control code vendor
    HidD_*               -> API USB HID (kalau memang HID)
    SetupDi* / CM_*      -> identitas device dari PnP

Jalankan:
    python spyon.py                       # attach ke "ZP 8.4 AMP_EN.exe"
    python spyon.py -p 9700               # attach by PID
    python spyon.py --seconds 60 -o cap.txt
    python spyon.py --usb                 # filter tampilan supaya device saja

Lalu selama perekaman: klik CH1..CH8, geser Gain, ubah HPF/LPF, klik Mixer.
Setiap aksi harus memunculkan baris baru di log.
"""

import argparse
import sys
import threading
import time

try:
    import frida
except ImportError:
    sys.exit("frida belum ada. Jalankan:  pip install frida-tools")


# --------------------------------------------------------------- JS payload

JS = r"""
'use strict';

const MAXBUF = 512;
const t0 = Date.now();

function ts() { return ((Date.now() - t0) / 1000).toFixed(4); }

function hx(ptr, len) {
    if (len <= 0) return '';
    len = Math.min(len, MAXBUF);
    const buf = new Uint8Array(ptr.readByteArray(len));
    const out = [];
    for (let off = 0; off < len; off += 16) {
        const chunk = buf.slice(off, off + 16);
        const hex = [];
        let text = '';
        for (let i = 0; i < chunk.length; i++) {
            hex.push(chunk[i].toString(16).padStart(2, '0').toUpperCase());
            text += (chunk[i] >= 32 && chunk[i] < 127) ? String.fromCharCode(chunk[i]) : '.';
        }
        out.push(off.toString(16).padStart(4, '0') + '  ' +
                 hex.join(' ').padEnd(47, ' ') + '  |' + text + '|');
    }
    return out.join('\n');
}

function emit(s) { send(s); }

function readWide(p) {
    try { return p.readUtf16String(); } catch (e) { return '<unreadable>'; }
}

function findExp(modName, expName) {
    // Frida 17 menghapus Module.findExportByName; coba beberapa API.
    let fn = null;
    try {
        const m = Process.getModuleByName(modName);
        fn = m.getExportByName(expName);
    } catch (e) { fn = null; }
    if (fn === undefined) fn = null;
    if (fn) return fn;

    try { fn = Module.getExportByName(modName, expName); } catch (e) { fn = null; }
    if (fn === undefined) fn = null;
    if (fn) return fn;

    try { fn = Module.getGlobalExportByName(expName); } catch (e) { fn = null; }
    if (fn === undefined) fn = null;
    return fn;
}

function isInteresting(path) {
    if (path === null) return false;
    const p = path.toLowerCase();
    return p.indexOf('usb') >= 0 ||
           p.indexOf('hid') >= 0 ||
           p.indexOf('com')  >= 0 ||
           p.indexOf('bt')   >= 0 ||
           p.indexOf('bth')  >= 0 ||
           p.indexOf('rfc')  >= 0 ||
           p.indexOf('vid_') >= 0 ||
           p.indexOf('vid&') >= 0;
}

// ---------------------------------------------------------------- createfile

function hookCreateFile(mod, name) {
    const fn = findExp(mod, name);
    if (!fn) return false;
    Interceptor.attach(fn, {
        onEnter(args) {
            this._wide = name.endsWith('W');
            this._path = this._wide ? readWide(args[0]) : null;
            if (!this._wide) { try { this._path = args[0].readAnsiString(); } catch (e) {} }
        },
        onLeave(retval) {
            const h = retval.toInt32();
            const ok = h !== -1 && h !== 0xFFFFFFFF;
            if (isInteresting(this._path)) {
                emit(`[${ts()}] CreateFile ${ok ? 'OK ' : 'FAIL'} handle=0x${h.toString(16)}  ${this._path}`);
            }
        }
    });
    emit(`# hooked ${name} (${mod})`);
    return true;
}

// ------------------------------------------------------------------ readfile

function hookReadFile(mod) {
    const fn = findExp(mod, 'ReadFile');
    if (!fn) return false;
    Interceptor.attach(fn, {
        onEnter(args) {
            this.h = args[0].toInt32();
            this.buf = args[1];
            this.n = args[2].toInt32();
            this.got = args[3];
        },
        onLeave(retval) {
            if (retval.toInt32() === 0) return;
            try {
                const n = this.got.readU32();
                if (n <= 0) return;
                emit(`[${ts()}] READ  h=0x${this.h.toString(16)} n=${n}\n${hx(this.buf, n)}`);
            } catch (e) { /* buffer sudah invalid */ }
        }
    });
    emit('# hooked ReadFile');
    return true;
}

function hookWriteFile(mod) {
    const fn = findExp(mod, 'WriteFile');
    if (!fn) return false;
    Interceptor.attach(fn, {
        onEnter(args) {
            this.h = args[0].toInt32();
            this.buf = args[1];
            this.n = args[2].toInt32();
            if (this.n > 0 && this.n < 65536) {
                emit(`[${ts()}] WRITE h=0x${this.h.toString(16)} n=${this.n}\n${hx(this.buf, this.n)}`);
            }
        },
        onLeave(retval) { /* status tidak dipakai */ }
    });
    emit('# hooked WriteFile');
    return true;
}

function hookDeviceIoControl(mod) {
    const fn = findExp(mod, 'DeviceIoControl');
    if (!fn) return false;
    Interceptor.attach(fn, {
        onEnter(args) {
            this.h = args[0].toInt32();
            this.code = args[1].toUInt32();
            this.inb = args[2];
            this.inLen = args[3].toInt32();
        },
        onLeave(retval) {
            const ok = retval.toInt32() !== 0;
            const outLen = this.inLen > 0 && this.inLen < 4096 ? this.inLen : 0;
            emit(`[${ts()}] IOCTL ${ok ? 'OK ' : 'FAIL'} h=0x${this.h.toString(16)} ` +
                 `code=0x${this.code.toString(16).toUpperCase().padStart(8, '0')} len=${this.inLen}` +
                 (outLen ? `\n${hx(this.inb, outLen)}` : ''));
        }
    });
    emit('# hooked DeviceIoControl');
    return true;
}

// --------------------------------------------------------------------- hid

function hookHid() {
    const probes = [
        ['HidD_GetHidGuid',        0],
        ['HidD_OpenDevice',        1],
        ['HidD_CreateFile',        1],
        ['HidD_Write',             3],
        ['HidD_Read',              3],
        ['HidD_SetOutputReport',   3],
        ['HidD_GetInputReport',    3],
        ['HidD_SetFeature',        3],
        ['HidD_GetFeature',        3],
        ['HidD_GetAttributes',     2],
        ['HidD_GetPreparsedData',  2],
        ['HidD_FreePreparsedData', 1],
        ['HidD_GetManufacturerString', 2],
        ['HidD_GetProductString',  2],
        ['HidD_GetSerialNumberString', 2],
    ];

    for (const [name, bufArg] of probes) {
        const fn = findExp('hid.dll', name);
        if (!fn) { emit(`# hid.dll: ${name} tidak ada`); continue; }
        Interceptor.attach(fn, {
            onEnter(args) { this.a = args; },
            onLeave(retval) {
                if (bufArg > 0 && name.startsWith('HidD_') &&
                    (name.includes('Report') || name.includes('Feature') ||
                     name.includes('Write') || name.includes('Read'))) {
                    try {
                        emit(`[${ts()}] ${name} handle=0x${args[0].toInt32().toString(16)} ` +
                             `len=${args[bufArg].toInt32()} ret=0x${retval.toInt32().toString(16)}` +
                             `\n${hx(args[bufArg - 1], args[bufArg].toInt32())}`);
                    } catch (e) { emit(`[${ts()}] ${name} ret=0x${retval.toInt32().toString(16)}`); }
                } else {
                    emit(`[${ts()}] ${name} ret=0x${retval.toInt32().toString(16)}`);
                }
            }
        });
        emit(`# hooked hid.dll!${name}`);
    }
}

// ------------------------------------------------------------------ pnp ids

function hookPnp() {
    const cfg = findExp('cfgmgr32.dll', 'CM_Get_Device_IDW');
    if (cfg) {
        Interceptor.attach(cfg, {
            onEnter(args) { this.p = args[1]; this.n = args[2].toUInt32(); },
            onLeave(retval) {
                if (retval.toUInt32() === 0) {
                    emit(`[${ts()}] CM_Get_Device_ID  ${readWide(this.p)}`);
                }
            }
        });
        emit('# hooked cfgmgr32!CM_Get_Device_IDW');
    }

    for (const [mod, name] of [
        ['setupapi.dll', 'SetupDiGetDeviceInstanceIdW'],
        ['setupapi.dll', 'SetupDiGetDeviceRegistryPropertyW'],
    ]) {
        const fn = findExp(mod, name);
        if (!fn) { emit(`# ${name} tidak ada`); continue; }
        Interceptor.attach(fn, {
            onEnter(args) { this.p = args[2]; this.n = args[3].toUInt32(); },
            onLeave(retval) {
                if (retval.toUInt32() === 0) {
                    let v = '';
                    try { v = readWide(this.p); } catch (e) { v = '<bin>'; }
                    if (v && v.length > 2) emit(`[${ts()}] ${name.replace('W', '')}  ${v}`);
                }
            }
        });
        emit(`# hooked ${name}`);
    }
}

// ------------------------------------------------------------------- socket

function hookSockets() {
    const wsa = findExp('ws2_32.dll', 'WSAConnect');
    if (wsa) {
        Interceptor.attach(wsa, {
            onEnter(args) { this.s = args[0].toInt32(); this.sa = args[1]; },
            onLeave(retval) {
                try {
                    // sockaddr: family(2) port(2,BE) addr(4)
                    const family = this.sa.readU16();
                    const port = this.sa.add(2).readU16();
                    const ip = [this.sa.add(4).readU8(), this.sa.add(5).readU8(),
                                this.sa.add(6).readU8(), this.sa.add(7).readU8()].join('.');
                    emit(`[${ts()}] WSAConnect s=${this.s} family=${family} ` +
                         `port=${(port >> 8) | ((port & 0xFF) << 8)} ip=${ip} ` +
                         `ret=${retval.toInt32()}`);
                } catch (e) { emit(`[${ts()}] WSAConnect ret=${retval.toInt32()}`); }
            }
        });
        emit('# hooked ws2_32!WSAConnect');
    }
}

// --------------------------------------------------------------------- main

setImmediate(function () {
    emit('# === ZP 8.4 AMP I/O spy mulai ===');
    hookCreateFile('kernel32.dll', 'CreateFileW');
    hookCreateFile('kernel32.dll', 'CreateFileA');
    hookReadFile('kernel32.dll');
    hookWriteFile('kernel32.dll');
    hookDeviceIoControl('kernel32.dll');
    hookHid();
    hookPnp();
    hookSockets();
    emit('# === semua hook terpasang, waiting for traffic ===');
});
"""


# ----------------------------------------------------------------- python side

def main():
    ap = argparse.ArgumentParser(description="SpI/USB/HID traffic recorder")
    ap.add_argument("-p", "--pid", type=int, help="PID target (default: cari by name)")
    ap.add_argument("-n", "--name", default="ZP 8.4 AMP_EN.exe", help="nama proses")
    ap.add_argument("--seconds", type=int, default=120, help="durasi rekam")
    ap.add_argument("-o", "--out", default="capture.txt", help="file log")
    ap.add_argument("--usb", action="store_true",
                    help="hanya tampilkan yang device (buang file/registry biasa)")
    args = ap.parse_args()

    dev = frida.get_local_device()

    if args.pid:
        pid = args.pid
    else:
        pid = None
        for proc in dev.enumerate_processes():
            if proc.name.lower() == args.name.lower():
                pid = proc.pid
                break
        if pid is None:
            cands = [p for p in dev.enumerate_processes()
                     if args.name.split('.')[0].lower() in p.name.lower()]
            if not cands:
                sys.exit(f"proses '{args.name}' tidak jalan. Cek:  tasklist | findstr ZP")
            print("kandidat: " + ", ".join(f"{p.name}({p.pid})" for p in cands))
            pid = cands[0].pid

    try:
        session = dev.attach(pid)
    except frida.PermissionDeniedError:
        sys.exit("ditolak -> jalankan PowerShell sebagai Administrator")
    except Exception as exc:
        sys.exit(f"gagal attach ke PID {pid}: {exc}")

    out = open(args.out, "w", encoding="utf-8", buffering=1)
    lines = [0]

    def on_message(message, data):
        if message["type"] != "send":
            print("FRIDA:", message)
            return
        text = message.get("payload", "")
        if args.usb and not text.startswith(("#", "[")):
            return
        if not text.startswith("#"):
            lines[0] += 1
            text = f"{text}"
        out.write(text + "\n")
        # tampilkan ringkas di layar
        head = text.split("\n")[0]
        print(head, flush=True)

    script = session.create_script(JS)
    script.on("message", on_message)
    script.load()

    print(f"\n# attach ke PID {pid} OK")
    print(f"# rekam {args.seconds} detik -> {args.out}")
    print("# sekarang: klik CH1..CH8, geser Gain, ubah HPF/LPF, klik Mixer")
    print("# Ctrl+C untuk berhenti lebih awal\n")

    try:
        time.sleep(args.seconds)
    except KeyboardInterrupt:
        print("\n# dihentikan user")

    script.unload()
    session.detach()
    out.close()
    print(f"\n# selesai. {lines[0]} event. Log: {args.out}")


if __name__ == "__main__":
    main()


