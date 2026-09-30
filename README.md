# Zevox 8.4 AMP — Linux & Web DSP Editor

A native Linux control app for the Zevox ZP 8.4 car amplifier. No vendor
software, no Electron, no runtime dependencies beyond a C11 compiler and
Xlib. The USB protocol was recovered from the vendor PC tool and the
parameter map was confirmed against real hardware.

<img src="docs/screenshots/main-ui.png" alt="ZP 8.4 AMP DSP editor" width="900">

| | |
|---|---|
| Device | `Bus 003 Device 005: ID 4084:4357 Nuvoton HID Transfer` |
| Node | `/dev/hidraw*`, udev symlink `/dev/zp84amp` |
| Verified on | Linux, x86_64, 29 September 2026 |
| Language | C11, ~15k lines, Xlib + hidraw |
| Product page | [voxresearch.id](https://voxresearch.id/katalog/digital-signal-processors/zp-8-4-amp/) |

## Product specifications

Specifications from the [official VOX Research product page](https://voxresearch.id/katalog/digital-signal-processors/zp-8-4-amp/):

- DSP amplifier, 8 channel out with built-in 4 channel amplifier
- 4 channel high-level input, 2 channel low-level input
- 8 channel low-level output (RCA)
- 31-band independent parametric/graphic EQ for 8 channel out
- Crossover 6–48 dB/octave, Linkwitz-Riley, Butterworth & Bessel
- Delay: 0.000–20.00 ms, 0.00–692.0 cm, 0.00–273 inch
- Power output (4 ohm): RMS 25 W x 4 CH
- Power output (2 ohm): RMS 40 W x 4 CH
- Peak power output (4 ohm): 60 W x 4 CH
- Lossless audio player (USB): FLAC, WAV, MP3, WMA, AIFF, APE, M4A, MKA, AU, MPC, ALAC, TTA, OGG, AAC
- Bluetooth audio player (external BT 5.0 included)
- Android & iOS audio player app
- Android & iOS app for DSP setup & tuning
- AD chip: AKM 5720, DA chip: Cirrus Logic CS4344, DSP chip: ADI BF592, OPAMP: ST TL084
- OEM plug & play cable (optional)
- Remote controller ZRC-1 included

## What it does

- **Console mixer layout** mirroring the vendor panel: input and mixer,
  crossover, live EQ curve, 31 band EQ table with faders, channel overlay
  picker, car diagram, master volume, unit delay, and eight output strips.
- **Reads and writes the amplifier** over USB or Bluetooth. Verified
  parameters include HPF/LPF frequency and filter code, EQ frequency/gain/Q,
  channel level, phase, and delay.
- **Bluetooth (BLE Mango3.0)** as a wireless alternative to USB. The app
  scans for the amplifier's BLE service, connects via BlueZ, and uses the
  same parameter read/write protocol over GATT characteristics.
- **Speaker protection** — per-channel safety profiles that lock crossover,
  EQ, and level settings to verified limits. Prevents accidental changes
  that could damage tweeters or subwoofers.
- **Auto delay distance** — drag speakers on the car/room diagram, calibrate
  the scale, and the app calculates delay per channel based on distance from
  the listening position. One click applies all delays to the DSP.
- **Browser UI** as an alternative front end, served by a single static
  binary on `http://127.0.0.1:8085`.
- **Local simulation mode** so the UI can be exercised with no amplifier
  plugged in.
- **Snapshot and diff tooling** (`zpsniff`) that reads all 1,934 parameter
  ids and writes a plain text baseline you can diff between two states.

## Requirements

- Linux with hidraw support
- gcc or clang, make
- `libx11-dev` (or equivalent) for the GUI binary
- Python 3 + `bleak` package for Bluetooth (BLE) support
- The amplifier, reachable as `/dev/zp84amp` after the udev rule is installed

`zp84web` and `zpsniff` need no X11 at all.

## Build

```sh
make            # zpsniff + zp84gui
make web        # zp84web only, no X11 needed
make test       # build and run the test binaries
```

Everything lands in `build/`.

Prebuilt stripped binaries for x86_64 Linux are in
[`release/linux-x86_64/`](release/linux-x86_64) with a
[`SHA256SUMS`](release/SHA256SUMS) manifest:

```sh
sha256sum -c release/SHA256SUMS
```

## Install

The install script builds, copies the binaries into `~/.local/bin`,
registers a desktop entry, and installs the web assets:

```sh
./scripts2/install-app.sh
```

Grant access to the amplifier once, as root:

```sh
sudo install -m 0644 scripts2/99-zp84.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

Until that is done, run the app with `sudo`.

## Run

```sh
zp84gui --read-dsp          # open and read the amplifier immediately
zp84gui --read-dsp --scale 1.5
zp84web                     # browser UI on 127.0.0.1:8085
zpsniff ping                # check the amplifier answers
zpsniff snap -o baseline.txt
```

`--scale` accepts 0.75 to 2.5. The window is resizable and every panel
re-lays out proportionally so the 31 EQ columns are never clipped.

## Bluetooth (BLE)

The toolbar has a **Koneksi: USB** / **BLE Mango3.0** toggle. Disconnect
first if connected, select BLE, click Connect, then Baca DSP. Scanning takes
about 6 seconds. The computer needs Bluetooth/BlueZ active, Python 3, and
the `bleak` package. See [docs/BLUETOOTH.md](docs/BLUETOOTH.md).

## Speaker Protection

The **Proteksi speaker** toolbar button opens per-channel limit profiles.
Lock a channel as tweeter or subwoofer after verifying settings against
speaker specifications. The app then blocks any edit that would violate
the locked profile (crossover, EQ gain, levels). Profiles are stored in
`$HOME/.zp84-protection.conf`. See
[docs/SPEAKER-PROTECTION.md](docs/SPEAKER-PROTECTION.md).

## Auto Delay Distance

The car/room diagram lets you drag speakers and the listening position (P).
After calibrating the real-world width, the app previews delay per channel
based on distance from P to the farthest speaker. Click **Terapkan Delay**
to write all channel delays to the DSP. Formula:
`(farthest_distance - channel_distance) / 0.346` ms. See
[docs/DESKTOP-UI.md](docs/DESKTOP-UI.md) for details.

## USB permission

The amplifier appears as a vendor HID device with 64-byte interrupt
endpoints and no report ID. hidraw wants exactly 64 bytes per transfer; the
Windows HID API wants a leading `0x00`. Throughput is capped at 64 KB/s,
so **audio cannot flow over USB** — the PC feeds the amp over analog RCA
and USB carries configuration only.

## Layout

```
src/hid/     hidraw transport and the AE 1E len tag / 80 len cmd .. CRC16 framing
src/ble/     Bluetooth LE worker (Python/BlueZ) and GUI IPC
src/dsp/     biquad design, filter families, fixed point, response model, autoeq solver
src/gui/     the Xlib console: layout, controls, presets, bluetooth, protection, embedded assets
src/web/     the HTTP server and its inlined UI
tests/       frame, math, response, BLE, and console data tests
tools/       zpsniff (C) plus zpdecode.py and diffdump.py
docs/        protocol notes, console mapping, USB verification, BLE, protection
web/         UI assets served by zp84web
examples     reference captures under "Default Project/"
```

## Documentation

- [docs/PROTOCOL.md](docs/PROTOCOL.md) — frame format, commands, what is
  confirmed versus still open
- [docs/CONSOLE-MAPPING.md](docs/CONSOLE-MAPPING.md) — parameter id to
  control mapping
- [docs/USB-VERIFICATION.md](docs/USB-VERIFICATION.md) — what was actually
  observed on hardware
- [docs/DESKTOP-UI.md](docs/DESKTOP-UI.md) — UI behaviour and interaction
- [docs/BLUETOOTH.md](docs/BLUETOOTH.md) — BLE connection, protocol, and
  verification status
- [docs/SPEAKER-PROTECTION.md](docs/SPEAKER-PROTECTION.md) — protection
  profiles, rules, and limitations
- [docs/FREQUENCY-RESPONSE.md](docs/FREQUENCY-RESPONSE.md) — DSP response
  model corrections and verification

## Tests

`make test` runs 50+ checks across frame handling, filter maths, response
model, BLE protocol, and the console data tables, all against real vendor
constants.

---

# Bahasa Indonesia

Aplikasi kontrol native Linux atau menggunakan web untuk amplifier mobil Zevox ZP 8.4 AMP. Tanpa
aplikasi vendor, tanpa Electron, tanpa dependency runtime selain compiler
C11 dan Xlib. Protokol USB direkonstruksi dari tool PC vendor, dan peta
parameter sudah diverifikasi pada perangkat asli.

## Kemampuan

- **Tampilan mixer konsol** mengikuti panel vendor: input dan mixer,
  crossover, grafik EQ, tabel 31 band beserta fader, pemilih channel,
  diagram mobil, master volume, unit delay, dan delapan strip output.
- **Membaca dan menulis ke amplifier** lewat USB atau Bluetooth. Parameter
  terverifikasi mencakup frekuensi dan kode filter HPF/LPF, frekuensi/gain/Q
  EQ, level, fase, dan delay per channel.
- **Bluetooth (BLE Mango3.0)** sebagai alternatif nirkabel ke USB. Aplikasi
  memindai layanan BLE amplifier, terhubung via BlueZ, dan memakai protokol
  baca/tulis parameter yang sama melalui karakteristik GATT.
- **Proteksi speaker** — profil batas per channel yang mengunci crossover,
  EQ, dan level sesuai batas terverifikasi. Mencegah perubahan yang bisa
  merusak tweeter atau subwoofer.
- **Delay otomatis dari jarak** — geser speaker di diagram mobil/ruangan,
  kalibrasi skala, dan aplikasi menghitung delay per channel berdasarkan jarak
  dari posisi dengar. Satu klik menerapkan semua delay ke DSP.
- **UI browser** sebagai alternatif, dilayani satu binary statis di
  `http://127.0.0.1:8085`.
- **Mode simulasi lokal** supaya UI bisa dicoba tanpa amplifier terhubung.
- **Alat snapshot dan diff** (`zpsniff`) yang membaca 1.934 id parameter
  dan menulis baseline teks yang bisa dibandingkan antar kondisi.

## Kebutuhan

- Linux dengan dukungan hidraw
- gcc atau clang, make
- `libx11-dev` untuk binary GUI
- Python 3 + paket `bleak` untuk dukungan Bluetooth (BLE)
- Amplifier yang sudah muncul sebagai `/dev/zp84amp` setelah aturan udev dipasang

`zp84web` dan `zpsniff` tidak butuh X11 sama sekali.

## Build

```sh
make            # zpsniff + zp84gui
make web        # zp84web saja, tanpa X11
make test       # build dan jalankan test
```

Binary hasil build ada di `build/`. Binary siap pakai untuk Linux x86_64
juga tersedia di [`release/linux-x86_64/`](release/linux-x86_64) beserta
manifest [`SHA256SUMS`](release/SHA256SUMS).

## Instalasi

```sh
./scripts2/install-app.sh
```

Izin akses amplifier, sekali jalan sebagai root:

```sh
sudo install -m 0644 scripts2/99-zp84.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

Sebelum itu selesai, jalankan aplikasinya dengan `sudo`.

## Menjalankan

```sh
zp84gui --read-dsp          # buka langsung dan baca amplifier
zp84gui --read-dsp --scale 1.5
zp84web                     # UI browser di 127.0.0.1:8085
zpsniff ping                # cek amplifier merespons
zpsniff snap -o baseline.txt
```

## Bluetooth (BLE)

Toolbar punya tombol **Koneksi: USB** / **BLE Mango3.0**. Disconnect dulu
jika sedang terhubung, pilih BLE, klik Connect, lalu Baca DSP. Pemindaian
sekitar 6 detik. Komputer memerlukan Bluetooth/BlueZ aktif, Python 3, dan
paket `bleak`. Lihat [docs/BLUETOOTH.md](docs/BLUETOOTH.md).

## Proteksi Speaker

Tombol **Proteksi speaker** di toolbar membuka profil batas per channel.
Kunci channel sebagai tweeter atau subwoofer setelah memverifikasi setelan
sesuai spesifikasi speaker. Aplikasi then memblokir edit yang melanggar
profil terkunci (crossover, gain EQ, level). Profil tersimpan di
`$HOME/.zp84-protection.conf`. Lihat
[docs/SPEAKER-PROTECTION.md](docs/SPEAKER-PROTECTION.md).

## Delay Otomatis dari Jarak

Diagram mobil/ruangan memungkinkan menggambar speaker dan posisi dengar (P).
Setelah kalibrasi lebar dunia nyata, aplikasi menampilkan preview delay
per channel berdasarkan jarak dari P ke speaker terjauh. Klik
**Terapkan Delay** untuk menulis semua delay channel ke DSP. Rumus:
`(jarak_terjauh - jarak_channel) / 0.346` ms. Lihat
[docs/DESKTOP-UI.md](docs/DESKTOP-UI.md) untuk detail.

## Catatan penting

Batas throughput USB adalah 64 KB/s, jadi **audio tidak bisa mengalir lewat
USB**. PC memberi feed ke amplifier lewat RCA analog; USB hanya membawa
konfigurasi.

## Dokumentasi

- [docs/PROTOCOL.md](docs/PROTOCOL.md) — format frame, daftar perintah,
  mana yang sudah terkonfirmasi dan mana yang masih terbuka
- [docs/CONSOLE-MAPPING.md](docs/CONSOLE-MAPPING.md) — pemetaan id parameter
  ke kontrol
- [docs/USB-VERIFICATION.md](docs/USB-VERIFICATION.md) — apa yang benar-benar
  diamati di perangkat
- [docs/DESKTOP-UI.md](docs/DESKTOP-UI.md) — perilaku dan interaksi UI
- [docs/BLUETOOTH.md](docs/BLUETOOTH.md) — koneksi BLE, protokol, dan
  status verifikasi
- [docs/SPEAKER-PROTECTION.md](docs/SPEAKER-PROTECTION.md) — profil proteksi,
  aturan, dan batasan
- [docs/FREQUENCY-RESPONSE.md](docs/FREQUENCY-RESPONSE.md) — koreksi model
  respons DSP dan verifikasi

## Test

`make test` menjalankan 50+ pemeriksaan covering frame, matematika filter,
model respons, protokol BLE, dan tabel data konsol, semuanya memakai
konstanta asli vendor.

---

## License

MIT, see [LICENSE](LICENSE).

## Status

Reverse engineering of the parameter id space is still in progress. The
mapping above covers the controls the vendor PC tool exposes; the
remaining ids are listed but not yet named. See the status table in
[docs/PROTOCOL.md](docs/PROTOCOL.md).
