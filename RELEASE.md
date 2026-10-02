# Release notes — Zevox ZP 8.4 AMP DSP Editor

## Paket rilis — 2 Oktober 2026

Paket binary Linux x86_64 untuk desktop GUI, web UI, dan alat protokol DSP
Zevox ZP 8.4 AMP. Paket dibangun dari source tree repository pada tanggal di
atas; ini bukan klaim bahwa tag GitHub telah dibuat.

The USB protocol was reverse engineered from the vendor PC tool and the
parameter map was confirmed against real hardware. The Bluetooth path
follows the same framing but has **not** been confirmed on hardware — see
[Known limitations](#known-limitations).

---

## Highlights

- **A full console mixer front end** for the 1,934 parameter amplifier,
  rebuilt around the vendor panel layout: crossover, live response curve,
  31-band EQ table, channel overlay picker, car/room diagram, master
  volume, unit delay, and eight output strips.
- **Writes are verified, not assumed.** Every edit must be acknowledged by
  the device *and* re-read before the app accepts it. A failed transaction
  invalidates the snapshot so nothing half-applied can be edited on top of.
- **Wireless control over BLE** through a Python/BlueZ worker that speaks
  the same protocol as USB.
- **Speaker protection profiles** that pin crossover, level, and all 124 EQ
  registers of a channel to limits you verified against the speaker spec.
- **Eight local preset slots** with a verified load that mutes first, and
  an output lock for tuning EQ without moving level.
- **A browser UI** served by one static binary, for when X11 is not
  available.
- **A protocol tool** (`zpsniff`) with 12 subcommands for snapshot, diff,
  range scan, and raw block access.

---

## Added

### Mixer routing dialog

The **Mixer** toolbar button opens a routing matrix over the eight output
channels. Each cell has a power toggle for the input-to-output path and a
gain percentage. Two input rows for AUX and Bluetooth, four for High level.
Register ids follow the source: `1226 + 8c + i` (High level), `1482 + 8c + i`
(AUX), `1360 + 8c + i` (Bluetooth).

### Eight local preset slots

`Memory` in the menu bar holds eight named slots over the **876 of 1,934**
registers that are mapped. Saving rejects the scene if any value is outside
the supported range. Loading validates every line, pre-checks the speaker
protection profiles for all eight channels, refuses when nothing would
change, **mutes every channel first**, writes only the differences, then
restores the preset's own mute state. A load that fails halfway reports the
partial state and asks for a fresh read.

Format `ZP84_SCENE 2`; the older `ZP84_SCENE 1` layout still loads and is
shown as `Preset lama`.

### Output lock

`Lock output` in the right-hand action column freezes master volume, output
level, phase, mute, delay, mixer routing and gain, pair linking, auto-delay
apply, and preset loads. This is the mode to be in while tuning EQ after the
levels are dialled in. The Mixer dialog shows **Output terkunci** and refuses
clicks rather than silently ignoring them.

### Link pairs

The amplifier exposes four independent stereo pairs (1/2, 3/4, 5/6, 7/8).
`Link pairs` copies 100 parameters from the selected channel to its partner
— 93 EQ, 4 crossover, level, master, delay, mute — and marks the header with
`CHn =`. One-way, session-only, refused while locked or protected.

### Reset and restore

`Reset EQ` flattens all 31 band gains and keeps the captured curve;
`Restore EQ` puts it back. `Reset Output` returns the selected channel to
level 0, phase normal, delay 0.

### Input volume and noise gate

New panels for the two source volumes — `USB_VOL` (id 1908) and `BT_VOL`
(id 1900), both 0–100 — and for the vendor noise gate, which is exposed as
21 discrete levels (`0` = OFF) matching the vendor table.

### Speaker names and installation marks

Each output strip takes an editable speaker name, 1–24 characters, stored in
`$HOME/.zp84-speaker-names.conf`. Each strip also carries an **AMP** or
**RCA** mark recording how that channel is wired, stored in
`zp84-outputs.conf`. Both are notes only — neither switches a DSP parameter.

### Room mode

The auto-delay diagram now has a **Ruangan** (room) plan alongside **Mobil**,
with its own speaker positions and its own reference width.

### Response model controls

`F2` cycles the model through all → HPF → LPF → EQ → HPF+LPF. `F3` exports
the 48 kHz, 2,048-point model plus per-section coefficients to
`/tmp/zp84-response-*` for offline plotting.

### Bluetooth transport

Wireless control over the amplifier's `Mango3.0` BLE service through a
Python/BlueZ worker, using the same parameter protocol as USB over the
vendor `AE01` write and `AE02` notify characteristics. Reads are batched
three ids per transaction to stay inside the notification size. The worker
runs as a child of the GUI over a socketpair and never inherits the USB,
display, or lock descriptors.

### Headless verification hooks

`zp84gui --selftest MS` runs the event loop for a bounded time, spot-checks
that the window actually painted, and exits with a status. `--shot PATH`
writes a binary PPM of the backbuffer. Combined with `--read-dsp` this
gives a scripted way to confirm the app renders and talks to the amplifier
without a human present.

### Build and tooling

`make gui` and `make tools` for single-target builds. A CMake path with an
off-by-default `ZP84_GUI` option, since the GUI needs X11 and the CLI tools
do not.

### Snapshot export from the GUI

**Ekspor snapshot** writes the raw 1,934-register image as
`zevox-snapshot-*` for diffing between two states.

---

## Perubahan sejak snapshot 30 September

Binary pada `release/` sebelumnya berasal dari snapshot `579beaa`. Paket ini
dibangun ulang dari source tree saat ini dan menyertakan perbaikan baca BLE
serta perlindungan mixer saat output terkunci.

### Bluetooth read resilience — `d7f596b`

- A BLE read whose notification is lost is attempted up to three times
  instead of failing the transfer immediately.
- If GATT drops mid-read the worker reconnects **once** — disconnect,
  200 ms settle, `wait_for(connect(), 8 s)`, re-resolve the vendor
  characteristics, re-subscribe — and re-issues the same query. A second
  disconnect during the same exchange fails the read instead of looping.
- Writes are still never replayed, because a repeated write could apply a
  change twice. This is unchanged and deliberate.
- Each BLE response has a 2.0 s timeout.
- The GUI read budget was previously derived from the expected reply size
  and could kill the worker mid-reconnect. It is now a flat 25 s for an
  id-read header and 5 s for the body. USB is unchanged at 400 ms.
- A failed BLE read now keeps the link and only invalidates the snapshot, so
  the transport does not have to be torn down and rebuilt.

### Output lock now covers the Mixer

- The Mixer panel accepted clicks while the output was locked. It now
  refuses them with an explanation, and the panel header shows
  **Output terkunci** so the state is visible without attempting an edit.
- Numeric and drag edits of mixer gain were already refused; the message now
  says what to do about it.

### Documentation

- `README.md` was rewritten. It previously documented Bluetooth, speaker
  protection, and auto delay, but omitted the Mixer dialog, the eight preset
  slots, output lock, pair linking, the reset and restore actions, input
  volume, the noise gate, speaker names, the AMP/RCA marks, room mode,
  snapshot export, `--selftest` and `--shot`, and most of the `zpsniff`
  subcommands. It also claimed "50+ checks" for a suite that runs about 394.
- Added a **Known limitations** section to the README. The response graph is
  a parameter model rather than a measurement, Bessel crossovers are not
  modelled, the browser UI has no protection or lock, and BLE is unverified
  on hardware. None of that was stated in the README before.

### Known pre-existing behaviour worth stating

These are not new; they are now written down.

- **Bessel is not modelled.** Butterworth and Linkwitz-Riley are. Selecting
  Bessel reports `filter tidak didukung` in the status line instead of
  drawing a plausible but wrong curve.
- **A corrupt protection file blocks every write except mute**, rather than
  leaving the amplifier effectively unlocked.
- **The desktop and web front ends share a device lock** at
  `$HOME/.config/zp84-dsp/device.lock`; only one can hold the amplifier.
- **Auto delay excludes muted channels** from both the farthest-speaker
  reference and the write set, and cancels the whole batch if any channel
  would exceed 20 ms rather than sending a partial result.
- **A preset that would violate an active protection profile performs zero
  writes**, and un-mute is refused when the current snapshot violates a
  profile. Mute is always allowed.
- **Bluetooth MAC addresses are masked** in the BLE notes and the worker
  docstring.

---

## Status verifikasi paket

Binary paket dibangun sebagai ELF Linux x86_64 dan semua berkas pada manifest
lulus pemeriksaan SHA-256. `scripts2/build-release.sh` dapat digunakan untuk
membangun ulang paket dan checksum. Suite pengujian tersedia melalui
`make test`; suite tidak dijalankan pada proses pengemasan ini.

**Confirmed on hardware:** the USB path — framing, the 1,934-register read,
the write-then-verify cycle, and the parameter encodings.

---

## Known limitations

- **BLE read and write are unverified on hardware.** The worker uses the
  same framing as USB and has unit tests for retry and reconnect paths, but
  treat Bluetooth as unproven. See
  [docs/BLUETOOTH.md](docs/BLUETOOTH.md).
- **Speaker protection is application-side.** It guards this app's write
  path. It cannot stop the vendor app, another tool, or the amplifier's own
  front panel. It is not a DSP limiter and does not detect clipping,
  temperature, or excursion.
- **The browser UI has no protection and no output lock.** `zp84web` writes
  registers directly after a range check. Anything the desktop app refuses
  can be written from the browser. It binds to `127.0.0.1` only; keep it
  there.
- **Presets cover 876 of 1,934 registers.** The remainder are listed but not
  named.
- **Persistence is inconsistent.** Presets, the layout file, output marks,
  and snapshot exports are written to the **current working directory**;
  only protection profiles and speaker names are anchored in `$HOME`.
  Launching from a different directory gives you a different set of presets.
- **`zp84web --root` is accepted and ignored.** The server always serves the
  UI compiled into the binary.
- **One crossover slope control in the browser UI** drives both the HPF and
  LPF filter codes. The desktop UI sets them independently.

---

## Install

```sh
make && make test
./scripts2/install-app.sh
```

Then grant device access once, as root:

```sh
sudo install -m 0644 scripts2/99-zp84.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

The GUI also offers this on first launch through a password dialog.

## Artifacts

`release/linux-x86_64/` berisi binary stripped `zp84gui`, `zp84web`, dan
`zpsniff`, worker BLE `zp84-ble.py`, aturan udev, serta README paket. Helper
BLE harus tetap berada di sebelah `zp84gui`. Manifest checksum ada di
`release/SHA256SUMS`:

```sh
sha256sum -c release/SHA256SUMS
```

Untuk membangun ulang binary dan manifest pada Linux x86_64:

```sh
./scripts2/build-release.sh
```

## Requirements

- Linux with hidraw
- gcc or clang, make; `libx11-dev` for the GUI
- BlueZ, Python 3, `bleak`, and `dbus-fast` for Bluetooth

## License

MIT. See [LICENSE](LICENSE).
