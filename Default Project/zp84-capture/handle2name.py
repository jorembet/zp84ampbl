r"""
handle2name.py - resolusi nama device dari handle angka di dalam proses lain.

Pakai NtQueryObject. Dipakai untuk tahu handle 0x5bc milik device apa.

    python handle2name.py 9700 0x5bc
    python handle2name.py 9700          # semua handle yang bisa dibaca namanya
"""

import ctypes
import ctypes.wintypes as wt
import sys

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
ntdll = ctypes.WinDLL("ntdll", use_last_error=True)

PROCESS_DUPLICATE_HANDLE = 0x0040
PROCESS_QUERY_INFORMATION = 0x0400
DUPLICATE_SAME_ACCESS = 0x00000002
ObjectNameInformation = 1


class UNICODE_STRING(ctypes.Structure):
    _fields_ = [
        ("Length", wt.USHORT),
        ("MaximumLength", wt.USHORT),
        ("Buffer", ctypes.POINTER(ctypes.c_wchar)),
    ]


def resolve(pid, handle):
    hproc = kernel32.OpenProcess(PROCESS_DUPLICATE_HANDLE | PROCESS_QUERY_INFORMATION,
                                 False, pid)
    if not hproc:
        raise ctypes.WinError(ctypes.get_last_error())

    hdup = wt.HANDLE()
    ok = kernel32.DuplicateHandle(hproc, wt.HANDLE(handle),
                                  kernel32.GetCurrentProcess(),
                                  ctypes.byref(hdup), 0, False, DUPLICATE_SAME_ACCESS)
    kernel32.CloseHandle(hproc)
    if not ok:
        raise ctypes.WinError(ctypes.get_last_error())

    us = UNICODE_STRING()
    ntdll.NtQueryObject(hdup, ObjectNameInformation,
                        ctypes.byref(us), ctypes.sizeof(us), None)
    # NtQueryObject menunjuk ke buffer milik proses target -> harus di-copy
    # ke buffer lokal kita sendiri (two-pass).
    if us.Length and us.Buffer:
        size = us.Length + 2
        local = ctypes.create_string_buffer(size)
        st = ntdll.NtQueryObject(hdup, ObjectNameInformation,
                                 local, size, None)
        if st == 0:
            name = local.raw.decode("utf-16-le", errors="replace").rstrip("\x00")
        else:
            name = f"(NtQueryObject status=0x{st & 0xFFFFFFFF:08X})"
    else:
        name = "(tanpa nama - bukan object kernel)"
    kernel32.CloseHandle(hdup)
    return name


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)

    pid = int(sys.argv[1])

    if len(sys.argv) >= 3:
        handles = [int(sys.argv[2], 0)]
    else:
        # rentang handle userland yang wajar
        handles = range(0x10, 0x1000)

    for h in handles:
        try:
            name = resolve(pid, h)
        except Exception:
            continue
        if name and name != "(tidak bernama / bukan object kernel)":
            print(f"0x{h:04X}  {name}")


if __name__ == "__main__":
    main()
