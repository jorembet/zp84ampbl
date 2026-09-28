# Protocol notes — ZP 8.4 AMP

Device: `Bus 003 Device 005: ID 4084:4357 Nuvoton HID Transfer`
(`/dev/hidraw7`)

## Status

| Layer | State |
|---|---|
| USB enumeration, endpoints, report descriptor | **confirmed** (observed) |
| HID report framing (64-byte chunking) | **confirmed** (observed) |
| HID frame header `AE 1E len tag` | **confirmed** (reverse engineered) |
| Inner frame `80 len cmd .. CRC16` | **confirmed** (reverse engineered) |
| CRC-16/MODBUS | **confirmed**, check value verified |
| 15 command bytes | **recovered** (opcodes listed below) |
| Payload layout per command | **unknown** — open work |
| DSP register map | **unknown** — open work |

## USB layer (observed)

```
Interface 0, class 3 (HID), 2 endpoints
  0x81  Interrupt IN   64 bytes, bInterval 1ms
  0x02  Interrupt OUT  64 bytes
bcdDevice 0.00, no serial, 200mA, bus powered
```

Report descriptor (29 bytes), read from
`/sys/class/hidraw/hidraw7/device/report_descriptor`:

```
06 00 FF   Usage Page (0xFF00 vendor defined)
09 01      Usage (0x01)
A1 01      Collection (Application)
19 01      Usage Minimum (1)
29 40      Usage Maximum (64)
15 00      Logical Minimum (0)
26 FF 00   Logical Maximum (255)
75 08      Report Size (8)
95 40      Report Count (64)
81 00      Input  (Data,Array,Abs)   -> 64 bytes in
19 01      Usage Minimum (1)
29 40      Usage Maximum (64)
91 00      Output (Data,Array,Abs)   -> 64 bytes out
C0         End Collection
```

No report ID is defined, so the Windows HID API requires a leading `0x00` byte
on every `ReadFile`/`WriteFile`; on hidraw you write exactly 64 bytes.

Throughput ceiling is 64 KB/s (64 bytes every 1 ms). 48 kHz/16-bit stereo
needs 192 KB/s, so **audio cannot flow over USB**. The PC feeds the amp over
analog RCA; USB carries configuration only.

## Frame format (reverse engineered from the vendor PC tool)

Source: `ZP 8.4 AMP_EN.exe`, a native MSVC x86 binary that imports
`HID.DLL` and `SETUPAPI.dll` and uses `ReadFile`/`WriteFile` directly (it does
**not** use `HidD_SetupReport`/`HidD_GetReport`).

```
offset  size  meaning
  0      1    0xAE      sync
  1      1    0x1E      sync
  2      1    len >> 8        \
  3      1    len & 0xFF       > payload length, 16-bit BIG endian
  4      1    0xC8      tag (constant in this firmware)
  5      len  payload
```

Total frame = `len + 5`, split into 64-byte HID reports. Every transfer is
exactly 64 bytes; the final chunk is zero padded.

Reassembly in the firmware is verified: the receive path checks
`byte[0] == 0xAE`, `byte[1] == 0x1E`, `byte[4] == 0xC8`, then computes
`len = (byte[2] << 8) | byte[3]` and copies from offset 5.

Byte 4 is always `0xC8` in this build. It is most likely a device/bus
address, kept configurable as `zp_conn.tag` in case other values appear.

### What this is NOT

The earlier assumption that this device speaks the stock Nuvoton
`USBD_HID_Transfer` protocol is **disproven**. The signature
`0x43444948` (`"HIDC"`) and opcodes `READ 0xD2` / `WRITE 0xC3` /
`ERASE 0x71` do not appear anywhere in the vendor binary, and there is no
18-byte `CMD_T` header, no byte-sum checksum and no 2048-byte page geometry.
Only the USB-level report layout is inherited from the Nuvoton sample.

## Inner frame (the command envelope)

The HID frame above is itself a transport. Everything the application sends
carries one more envelope, built at `0x41f8e0`:

```
offset      size  meaning
  0          1    0x80
  1          1    payload length + 3, or 0xFF when the length is >= 250
  2          1    command byte
  3          len  payload
  3+len      1    CRC >> 8
  4+len      1    CRC & 0xFF
```

`len` in the table is the payload length, so the inner frame is
`payload + 5` bytes. The CRC covers bytes `0 .. len+2` (header plus payload,
excluding the CRC itself).

The CRC at `0x41f8a0` is **CRC-16/MODBUS**: polynomial `0x8005` reflected
to `0xA001`, init `0xFFFF`, both refin and refout true, no final xor. The
standard check value for the ASCII string `123456789` is `0x4B37`, which the
implementation in `src/hid/zp_hid.c` reproduces.

So a complete request on the wire is:

```
AE 1E <L:16 BE> C8          L = payload length + 5
  80 <len+3> <cmd> <payload> <CRC:16 BE>
    split into 64-byte HID reports
```

The application has three send paths selected by a mode variable at
`0x6d7fdc`: mode 1 goes through the HID framer, mode 2 uses another transport
(`0x6338d8`), and anything else writes the buffer raw. The same payload
builder serves all three, which confirms the envelope is transport agnostic.

## Command bytes

Fifteen distinct command bytes are passed to the inner frame builder across its
35 call sites:

| cmd | call sites | cmd | call sites |
|---|---|---|---|
| `0x00` | 1 | `0x57` | 1 |
| `0x03` | 4 | `0x5A` | 2 |
| `0x04` | 6 | `0x5B` | 1 |
| `0x06` | 8 | `0x5C` | 1 |
| `0x10` | 2 | `0xF0` | 1 |
| `0x20` | 1 | `0xFC` | 1 |
| `0x21` | 1 | `0xFE` | 1 |
|  |  | `0xFF` | 1 |

`0x06` is by far the most used and is the first one sent at connect time, so it
is the most likely handshake or status poll.

## Open work: the payload layer

Everything above describes the envelope only. The payload carries the actual
DSP commands, and its layout is not yet known. That is the next task, and it
is the bulk of the remaining reverse engineering.

Approaches, in order of preference:

1. **Read the higher layer out of the binary.** The envelope is done; what
   remains is the payload each command carries. The call sites listed above
   are the entry points, and correlating them with the UI handlers that write
   EQ, crossover and delay values gives the register map.
2. **Differential dumps.** With a working frame layer, send a minimal read
   request, vary one setting in the vendor tool, and diff the responses. This
   maps payload opcodes and offsets to DSP parameters empirically.

## Tooling

```sh
make                    # builds build/zpsniff
make test               # 68 checks, no device required
sudo ./scripts/install-udev.sh    # grants access to /dev/hidraw7
```

`zpsniff` speaks the real framing:

```sh
./build/zpsniff find
./build/zpsniff ping                      # empty frame, see if anything replies
./build/zpsniff xfer -i cmd.bin           # send a payload, print the reply
./build/zpsniff xfer --hex "01 02 03"     # inline payload
./build/zpsniff raw -i bytes.bin           # bypass framing entirely
```

`tools/zpdecode.py` decodes a usbmon text capture, and
`tools/diffdump.py` diffs two response captures. Both still assume the older
CMD_T layout and need updating once the payload format is known.

```sh
./build/zpsniff ping --cmd 0x06            # handshake / status poll
./build/zpsniff xfer --cmd 0x06 --hex ""   # empty payload
./build/zpsniff xfer --cmd 0x5C -i req.bin # a captured request, replayed
```

## Companion files

- `ZP 8.4AMP-V1517-release.apk` — Android app. It controls the amp over
  **Bluetooth** (custom GATT service `ae00`/`ae01`/`ae02`), not USB, so it
  does not describe the USB payload. Package `com.tigerapp.jheqchart_application`
  is an EQ chart demo. Decompiled sources are in `work/apk_src/`.
- `ZP 8.4 AMP_EN.exe` — the vendor PC tool, and the source of truth for the
  USB protocol. Disassembly kept in `work/zp.dis`.
