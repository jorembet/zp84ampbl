# Zevox 8.4 AMP — Linux & Web DSP Editor

<p align="center">
  <img src="https://voxresearch.id/wp-content/uploads/2021/04/ZP-8.4-AMP-3-web.jpg"
       alt="Zevox ZP 8.4 AMP"
       width="400">
</p>

A native Linux control app for the Zevox ZP 8.4 car amplifier. No vendor
software, no Electron, no runtime dependencies beyond a C11 compiler and
Xlib. The USB protocol was recovered from the vendor PC tool and the
parameter map was confirmed against real hardware.

<img src="docs/screenshots/main-ui.png" alt="ZP 8.4 AMP DSP editor" width="900">

| | |
|---|---|
| Device | `Bus 003 Device 005: ID 4084:4357 Nuvoton HID Transfer` |
| Node | `/dev/hidraw*`, udev symlink `/dev/zp84amp` |
| Verified on | Linux, x86_64, USB path 29 September 2026 |
| Language | C11, ~15k lines, Xlib + hidraw, plus a Python BLE worker |
| Registers | 1,934 parameter ids (`0x0000`–`0x078D`), 876 exposed as presets |
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
  channel level, phase, and delay. Every edit is acknowledged by the device
  *and* re-read before it is accepted.
- **Bluetooth (BLE Mango3.0)** as a wireless alternative to USB. The app
  scans for the amplifier's BLE service, connects via BlueZ, and uses the
  same parameter read/write protocol over GATT characteristics.
- **Mixer routing dialog** — per output channel, enable or mute each input
  into the mix and set its gain in percent. Two rows for AUX or Bluetooth,
  four rows for High level.
- **Eight local preset slots** (`Memory`) — name, save, and load scenes.
  Loading mutes every channel first, writes the differences, then restores
  the preset's own mute state.
- **Speaker protection** — per-channel safety profiles that lock crossover,
  EQ, and level settings to verified limits. Prevents accidental changes
  that could damage tweeters or subwoofers.
- **Output lock** — one switch that freezes master volume, output levels,
  phase, mute, delay, mixer routing and gain, pair linking, and preset loads
  while you tune the EQ.
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
make gui        # zp84gui only
make tools      # zpsniff only
make web        # zp84web only, no X11 needed
make test       # build and run the test binaries
make clean      # remove build/
```

Everything lands in `build/`. `zp84gui` looks for `zp84-ble.py` next to its
own executable, so keep the two together if you move the binary.

There is also a CMake build. The GUI is off by default because it needs X11:

```sh
cmake -S . -B build-cmake -DZP84_GUI=ON
cmake --build build-cmake
ctest --test-dir build-cmake
```

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

Until that is done, run the app with `sudo`. On the very first launch the
app offers a password dialog that installs the same udev rule through
`sudo -S` so you do not have to leave the app; **Lewati** skips it.

## Run

```sh
zp84gui --read-dsp          # open and read the amplifier immediately
zp84gui --read-dsp --scale 1.5
zp84web                     # browser UI on 127.0.0.1:8085
zpsniff find                # locate the hidraw node and check permissions
zpsniff ping                # check the amplifier answers
zpsniff snap -o baseline.txt
```

| Flag | Effect |
|---|---|
| `--read-dsp` | connect and read all 1,934 ids at startup |
| `--scale N` | fixed UI scale, 0.75 to 2.5 |
| `--selftest MS` | run the event loop for `MS` ms, spot-check the rendered window, print a result, exit. Must be the **first** argument |
| `--shot PATH` | write a binary PPM screenshot of the rendered window |

`--scale` accepts 0.75 to 2.5. The window is resizable and every panel
re-lays out proportionally so the 31 EQ columns are never clipped. The
minimum window is 930×600.

```sh
zp84gui --selftest 400 --read-dsp --shot /tmp/zp84.ppm
```

## Desktop tour

Toolbar, left to right: **Input** source picker, **Mixer**, **Jarak / Delay**,
**Input volume**, **Noise Gate**, **Proteksi speaker**, the **Koneksi: USB** /
**BLE Mango3.0** transport toggle, **Baca DSP**, and Connect/Disconnect with a
status lamp.

The right-hand action column holds **Edit EQ**, **Reset EQ**, **Restore EQ**,
**Reset Output**, **Link pairs**, and **Lock output**.

### Keyboard

| Key | Action |
|---|---|
| `1`–`8` | select channel, and show only that channel's curve |
| `F2` | cycle the response model: all → HPF → LPF → EQ → HPF+LPF |
| `F3` | export the response log to `/tmp/zp84-response-*` |
| `F11` | toggle fullscreen |
| `R` | read the DSP again |
| `Page Up` / `Page Down` | page the raw register view |
| `Esc` | close modal, then cancel a drag, then quit |
| `q` | quit |

In the **Register USB** and **Simulasi lokal** views, `↑`/`↓` nudge the
selected EQ band by ±0.5 dB in simulation and the mouse wheel pages the
register list.

### Response model

The graph is a 48 kHz, 2,048-point model computed from the parameter values,
not measured filter coefficients. Grid is 20 Hz to 20 kHz on a log axis and
±20 dB. Butterworth and Linkwitz-Riley are modelled; **Bessel is not**, and
the status line says `filter tidak didukung` when it is selected. `F3`
exports the model, including per-section coefficients, for offline plotting.

### Crossover, EQ, output

- HPF/LPF frequency 10–23,000 Hz, three families (`Butter-W`, `Bessel`,
  `Link_R`) and nine slope positions (6, 12, 18, 24, 30, 36, 42, 48 dB/oct
  plus OFF). Click the type or slope cell to cycle it.
- 31 bands, exact ⅓-octave from 20 Hz to 20,480 Hz. Frequency 20–20,000 Hz,
  Q 0.1–30, gain ±12 dB.
- Per output strip: level 0–60 on the vendor display scale, phase 0°/180°,
  mute, delay, an editable speaker name, and an AMP/RCA installation mark.
- Delay unit switches between Ms, Cm, and Inch at 346 m/s.

`Reset EQ` flattens all 31 gains and keeps the previous curve so
`Restore EQ` can bring it back. `Reset Output` returns the selected channel
to level 0, phase normal, delay 0.

### Link pairs

The amplifier exposes four independent stereo pairs: 1/2, 3/4, 5/6, 7/8.
`Link pairs` copies 100 parameters from the selected channel to its partner
and marks the pair with `CHn =` in the strip header. It is a one-way copy,
session-only, and is refused while the output is locked or protected.

## Presets (Memory)

`Memory` in the menu bar holds **8 slots**. Saving writes the 876 mapped
registers for the current DSP state; loading mutes all channels, applies the
differences, and restores the saved mute state. Names are 1–24 characters.

A preset that would violate an active speaker protection profile is rejected
before anything is written. A load that fails halfway reports the partial
state and asks you to read the DSP again.

**Ekspor snapshot** writes the raw 1,934-register image to
`zevox-snapshot-*` in the current directory. This is a capture for diffing,
not a restorable preset.

## Speaker Protection

The **Proteksi speaker** toolbar button opens per-channel limit profiles.
Set crossover and level from the speaker specification, tick the confirmation
box, then lock the channel as tweeter or subwoofer. The profile pins the
crossover type, frequency and slope, the output and master level, the EQ
enable flag, and all 124 EQ registers for that channel — EQ gain may be
lowered but never raised above the locked value. While any profile is
active, input and mixer writes are refused too.

Profiles are stored in `$HOME/.zp84-protection.conf`. A corrupt file blocks
every write except mute rather than silently unlocking the amplifier. See
[docs/SPEAKER-PROTECTION.md](docs/SPEAKER-PROTECTION.md).

Protection is an application-side guard. It is not a DSP limiter and does
not detect clipping, temperature, or excursion.

## Output lock

`Lock output` freezes master volume, output level, phase, mute, delay,
mixer routing and gain, pair linking, auto-delay apply, and preset loads, so
the EQ can be tuned without moving the level. The Mixer dialog shows
**Output terkunci** and refuses clicks. `Unlock output` releases everything.

## Auto Delay Distance

The **Mobil**/**Ruangan** diagram lets you drag eight speakers and the
listening position (P). After setting the real-world width, the app previews
delay per channel based on distance from P to the farthest speaker. Click
**Terapkan Delay** to write all channel delays to the DSP. Formula:
`(farthest_distance - channel_distance) / 0.346` ms.

Muted channels are excluded from both the reference and the write. If any
channel would exceed 20 ms the whole batch is cancelled and nothing is
sent. Applying a distance-derived delay set while channels are pair-linked
is refused, because the link would overwrite the result. See
[docs/DESKTOP-UI.md](docs/DESKTOP-UI.md).

## Bluetooth (BLE)

The toolbar has a **Koneksi: USB** / **BLE Mango3.0** toggle. Disconnect
first if connected, select BLE, click Connect, then Baca DSP. Scanning takes
about 6 seconds. The computer needs Bluetooth/BlueZ active, Python 3, and
the `bleak` package. See [docs/BLUETOOTH.md](docs/BLUETOOTH.md).

Reads over BLE are batched three ids per transaction instead of eight, and
a read whose notification is lost is retried up to three times. If GATT
drops mid-read the worker reconnects once and re-issues the same query.
Writes are never replayed, because a repeated write could apply a change
twice.

## Browser UI

`zp84web` serves a single-page UI on `127.0.0.1:8085` with the channel
strip, a draggable EQ curve, crossover card, per-channel level/delay/phase/
mute, input source and volumes, master volume, the 21-level noise gate, the
mixer grid, and a 31-row band table. Use `--port N` to move it.

The browser UI is a lighter front end: it has no speaker protection, no
output lock, no presets, no auto delay, no Bluetooth transport, and it
cannot edit AMP/RCA marks or speaker names. It writes registers directly, so
the protection and lock guarantees that hold in the desktop app do **not**
apply there. Keep it on loopback.

## Where the app stores files

| Path | Contents |
|---|---|
| `$HOME/.config/zp84-dsp/device.lock` | exclusive lock shared by `zp84gui` and `zp84web` |
| `$HOME/.config/zp84-dsp/first-run` | suppresses the root password prompt |
| `$HOME/.zp84-protection.conf` | speaker protection profiles |
| `$HOME/.zp84-speaker-names.conf` | per-channel speaker names |
| `zp84-preset-1..8.scene` | local preset slots |
| `zp84-layout.conf` | diagram positions and scale |
| `zp84-outputs.conf` | AMP/RCA installation marks |
| `zevox-snapshot-*`, `/tmp/zp84-response-*` | snapshot export, F3 response log |

Note that presets, layout, output marks, and snapshot exports are written to
the **current working directory**, not `$HOME`. Only the first four files are
anchored in `$HOME`. Launching from a different directory therefore gives you
a different set of presets.

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
release/     prebuilt x86_64 binaries and SHA256SUMS
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
- [RELEASE.md](RELEASE.md) — release notes

## Tests

`make test` runs five suites: 109 named checks in the framing and filter
maths binaries, 254 assertions across the console data tables, 24 in the
response model, and 7 BLE protocol cases in Python — about 394 assertions in
total, all against real vendor constants. The console suite exercises the
whole UI layer without hardware or an X server by mocking the transport.

`build/zpresponse_test out.csv` writes the response sweep to a CSV.

## Known limitations

- **BLE read and write are not yet verified on hardware.** The worker
  follows the same framing as USB and is covered by unit tests, but treat the
  Bluetooth path as unproven. See [docs/BLUETOOTH.md](docs/BLUETOOTH.md).
- **Bessel crossovers are not modelled** in the response graph.
- **The response graph is a parameter model**, not a measurement of the
  amplifier.
- **Speaker protection is application-side.** It cannot stop another tool, or
  the vendor app, from changing the amplifier.
- **Presets cover 876 of 1,934 registers.** The remainder are listed but not
  named.
- **The browser UI has no protection or output lock.**

## Status

Reverse engineering of the parameter id space is still in progress. The
mapping covers the controls the vendor PC tool exposes; the remaining ids
are listed but not yet named. See the status table in
[docs/PROTOCOL.md](docs/PROTOCOL.md).

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
  EQ, level, fase, dan delay per channel. Setiap edit baru diterima setelah
  perangkat mengonfirmasi dan aplikasi membaca ulang nilainya.
- **Bluetooth (BLE Mango3.0)** sebagai alternatif nirkabel ke USB. Aplikasi
  memindai layanan BLE amplifier, terhubung via BlueZ, dan memakai protokol
  baca/tulis parameter yang sama melalui karakteristik GATT.
- **Dialog routing Mixer** — per output channel, aktifkan atau mute tiap
  input yang masuk ke mix dan atur gain dalam persen. Dua baris untuk AUX atau
  Bluetooth, empat baris untuk High level.
- **Delapan slot preset lokal** (`Memory`) — beri nama, simpan, dan muat
  scene. Saat dimuat, semua channel di-mute dulu, lalu yang berbeda ditulis,
  lalu status mute dari preset dipulihkan.
- **Proteksi speaker** — profil batas per channel yang mengunci crossover,
  EQ, dan level sesuai batas terverifikasi. Mencegah perubahan yang bisa
  merusak tweeter atau subwoofer.
- **Lock output** — satu tombol yang membekukan master volume, level output,
  fase, mute, delay, routing dan gain mixer, link pasangan, serta pemuatan
  preset, sehingga EQ bisa disetel tanpa memindahkan level.
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
make gui        # zp84gui saja
make tools      # zpsniff saja
make web        # zp84web saja, tanpa X11
make test       # build dan jalankan test
make clean      # hapus build/
```

Binary hasil build ada di `build/`. `zp84gui` mencari `zp84-ble.py` di
sebelah executable-nya, jadi jangan memisahkan keduanya kalau binary-nya
dipindah.

Build dengan CMake juga tersedia. GUI nonaktif secara default karena
membutuhkan X11:

```sh
cmake -S . -B build-cmake -DZP84_GUI=ON
cmake --build build-cmake
ctest --test-dir build-cmake
```

Binary siap pakai untuk Linux x86_64 juga tersedia di
[`release/linux-x86_64/`](release/linux-x86_64) beserta manifest
[`SHA256SUMS`](release/SHA256SUMS).

## Instalasi

Script instalasi melakukan build, menyalin binary ke `~/.local/bin`,
mendaftarkan entri desktop, dan memasang aset web:

```sh
./scripts2/install-app.sh
```

Izin akses amplifier, sekali jalan sebagai root:

```sh
sudo install -m 0644 scripts2/99-zp84.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

Sebelum itu selesai, jalankan aplikasinya dengan `sudo`. Pada peluncuran
pertama, aplikasi menampilkan dialog password untuk memasang aturan udev yang
sama lewat `sudo -S`, jadi tidak perlu keluar dari aplikasi. **Lewati** untuk
melewatinya.

## Menjalankan

```sh
zp84gui --read-dsp          # buka langsung dan baca amplifier
zp84gui --read-dsp --scale 1.5
zp84web                     # UI browser di 127.0.0.1:8085
zpsniff find                # cari node hidraw dan cek izin akses
zpsniff ping                # cek amplifier merespons
zpsniff snap -o baseline.txt
```

| Flag | Fungsi |
|---|---|
| `--read-dsp` | terhubung dan membaca 1.934 id saat start |
| `--scale N` | skala UI tetap, 0.75 sampai 2.5 |
| `--selftest MS` | jalankan event loop selama `MS` ms, periksa hasil render, cetak ringkasan, lalu keluar. Harus jadi **argumen pertama** |
| `--shot PATH` | tulis screenshot PPM biner dari tampilan yang sudah dirender |

Skala 0.75 sampai 2.5. Jendela bisa di-resize dan semua panel menata ulang
seproporsional sehingga 31 kolom EQ tidak pernah terpotong. Ukuran minimum
jendela 930×600.

```sh
zp84gui --selftest 400 --read-dsp --shot /tmp/zp84.ppm
```

## Tour desktop

Toolbar dari kiri: pemilih sumber **Input**, **Mixer**, **Jarak / Delay**,
**Input volume**, **Noise Gate**, **Proteksi speaker**, tombol transport
**Koneksi: USB** / **BLE Mango3.0**, **Baca DSP**, lalu Connect/Disconnect
dengan lampu status.

Kolom aksi di kanan berisi **Edit EQ**, **Reset EQ**, **Restore EQ**,
**Reset Output**, **Link pairs**, dan **Lock output**.

### Pintasan papan ketik

| Tombol | Fungsi |
|---|---|
| `1`–`8` | pilih channel, dan tampilkan hanya kurvanya |
| `F2` | ganti mode model respons: semua → HPF → LPF → EQ → HPF+LPF |
| `F3` | ekspor log respons ke `/tmp/zp84-response-*` |
| `F11` | toggle layar penuh |
| `R` | baca ulang DSP |
| `Page Up` / `Page Down` | halaman register mentah |
| `Esc` | tutup panel, batalkan drag, lalu keluar |
| `q` | keluar |

Pada tampilan **Register USB** dan **Simulasi lokal**, `↑`/`↓` mengubah gain
band EQ terpilih ±0,5 dB di simulasi, dan roda mouse mem halaman daftar
register.

### Model respons

Grafik adalah model 48 kHz dengan 2.048 titik yang dihitung dari nilai
parameter, bukan koefisien filter hasil pengukuran. Sumbu 20 Hz sampai
20 kHz logaritmik dan ±20 dB. Butterworth dan Linkwitz-Riley dimodelkan;
**Bessel tidak**, dan baris status menampilkan `filter tidak didukung` saat
dipilih. `F3` mengekspor model beserta koefisien per section untuk diplot
di luar aplikasi.

### Crossover, EQ, output

- Frekuensi HPF/LPF 10–23.000 Hz, tiga keluarga filter (`Butter-W`, `Bessel`,
  `Link_R`) dan sembilan posisi slope (6, 12, 18, 24, 30, 36, 42, 48 dB/oct
  plus OFF). Klik sel tipe atau slope untuk memutar.
- 31 band, tepat ⅓ oktav dari 20 Hz sampai 20.480 Hz. Frekuensi
  20–20.000 Hz, Q 0,1–30, gain ±12 dB.
- Per strip output: level 0–60 pada skala tampilan vendor, fase 0°/180°,
  mute, delay, nama speaker yang bisa diedit, dan penanda pemasangan AMP/RCA.
- Satuan delay bisa Ms, Cm, atau Inch pada 346 m/s.

`Reset EQ` meratakan seluruh 31 gain dan menyimpan kurva sebelumnya supaya
`Restore EQ` bisa mengembalikannya. `Reset Output` mengembalikan channel
terpilih ke level 0, fase normal, delay 0.

### Link pairs

Amplifier menyediakan empat pasangan stereo independen: 1/2, 3/4, 5/6, 7/8.
`Link pairs` menyalin 100 parameter dari channel terpilih ke pasangannya dan
memberi tanda `CHn =` di header strip. Ini salinan satu arah, hanya untuk
sesi berjalan, dan ditolak saat output terkunci atau proteksi aktif.

## Preset (Memory)

`Memory` di menu bar menyediakan **8 slot**. Menyimpan menulis 876 register
yang dipetakan untuk kondisi DSP saat ini; memuat akan mute semua channel,
menerapkan bagian yang berbeda, lalu memulihkan status mute dari preset.
Nama 1–24 karakter.

Preset yang melanggar profil proteksi aktif ditolak sebelum satu pun nilai
ditulis. Pemuatan yang gagal di tengah melaporkan kondisi parsial dan meminta
baca ulang DSP.

**Ekspor snapshot** menulis citra 1.934 register mentah ke
`zevox-snapshot-*` di direktori kerja saat ini. Ini tangkapan untuk
perbandingan, bukan preset yang bisa dipulihkan.

## Proteksi Speaker

Tombol **Proteksi speaker** di toolbar membuka profil batas per channel.
Atur crossover dan level dari spesifikasi speaker, centang kotak konfirmasi,
lalu kunci channel sebagai tweeter atau subwoofer. Profilnya memfreeze tipe
crossover, frekuensi, slope, level output dan master, flag enable EQ, dan
keempat 124 register EQ channel tersebut — gain EQ boleh diturunkan tetapi
tidak boleh dinaikkan di atas nilai terkunci. Selama ada profil aktif,
penulisan input dan mixer juga ditolak.

Profil tersimpan di `$HOME/.zp84-protection.conf`. File yang rusak
memblokir semua penulisan kecuali mute, bukan diam-diam membuka kunci
amplifier. Lihat [docs/SPEAKER-PROTECTION.md](docs/SPEAKER-PROTECTION.md).

Proteksi adalah pengaman di sisi aplikasi. Ini bukan limiter DSP dan tidak
mendeteksi clipping, suhu, maupun excursion.

## Lock Output

`Lock output` membekukan master volume, level output, fase, mute, delay,
routing dan gain mixer, link pasangan, penerapan delay otomatis, serta
pemuatan preset, sehingga EQ bisa disetel tanpa memindahkan level. Dialog
Mixer menampilkan **Output terkunci** dan menolak klik. `Unlock output`
melepaskan semuanya.

## Delay Otomatis dari Jarak

Diagram **Mobil**/**Ruangan** memungkinkan menggambar delapan speaker dan
posisi dengar (P). Setelah mengatur lebar dunia nyata, aplikasi menampilkan
preview delay per channel berdasarkan jarak dari P ke speaker terjauh. Klik
**Terapkan Delay** untuk menulis semua delay channel ke DSP. Rumus:
`(jarak_terjauh - jarak_channel) / 0.346` ms.

Channel yang di-mute dikecualikan baik dari acuan maupun dari daftar
penulisan. Kalau ada channel yang akan melebihi 20 ms, seluruh batch dibatalkan
dan tidak ada yang dikirim. Applying delay hasil jarak saat channel sudah
dipasangkan akan ditolak, karena link akan menimpa hasilnya. Lihat
[docs/DESKTOP-UI.md](docs/DESKTOP-UI.md).

## Bluetooth (BLE)

Toolbar punya tombol **Koneksi: USB** / **BLE Mango3.0**. Disconnect dulu
jika sedang terhubung, pilih BLE, klik Connect, lalu Baca DSP. Pemindaian
sekitar 6 detik. Komputer memerlukan Bluetooth/BlueZ aktif, Python 3, dan
paket `bleak`. Lihat [docs/BLUETOOTH.md](docs/BLUETOOTH.md).

Pembacaan lewat BLE dikelompokkan tiga id per transaksi, bukan delapan, dan
query yang notifikasinya hilang diulang sampai tiga kali. Kalau GATT putus di
tengah pembacaan, worker menyambung ulang sekali dan mengulang query yang
sama. Write tidak pernah diulang, karena write yang terulang bisa menerapkan
perubahan dua kali.

## UI Browser

`zp84web` menyajikan UI satu halaman di `127.0.0.1:8085` dengan strip
channel, kurva EQ yang bisa diseret, kartu crossover, level/delay/fase/mute
per channel, sumber dan volume input, master volume, noise gate 21 tingkat,
grid mixer, dan tabel 31 baris band. Gunakan `--port N` untuk memindahkannya.

UI browser adalah front end yang lebih ringan: tidak ada proteksi speaker,
tidak ada lock output, tidak ada preset, tidak ada delay otomatis, tidak ada
transport Bluetooth, dan tidak bisa mengedit penanda AMP/RCA maupun nama
speaker. UI ini menulis register secara langsung, jadi jaminan proteksi dan
lock yang berlaku di aplikasi desktop **tidak berlaku** di sana. Gunakan
hanya di loopback.

## Lokasi penyimpanan file

| Path | Isi |
|---|---|
| `$HOME/.config/zp84-dsp/device.lock` | lock eksklusif dipakai bersama `zp84gui` dan `zp84web` |
| `$HOME/.config/zp84-dsp/first-run` | menandai dialog password root sudah selesai |
| `$HOME/.zp84-protection.conf` | profil proteksi speaker |
| `$HOME/.zp84-speaker-names.conf` | nama speaker per channel |
| `zp84-preset-1..8.scene` | slot preset lokal |
| `zp84-layout.conf` | posisi diagram dan skala |
| `zp84-outputs.conf` | penanda pemasangan AMP/RCA |
| `zevox-snapshot-*`, `/tmp/zp84-response-*` | ekspor snapshot, log respons F3 |

Perhatikan bahwa preset, layout, penanda output, dan ekspor snapshot ditulis
ke **direktori kerja saat ini**, bukan `$HOME`. Hanya empat file pertama yang
berkaitan dengan `$HOME`. Menjalankan aplikasi dari direktori yang berbeda
berarti memakai set preset yang berbeda.

## Catatan penting

Batas throughput USB adalah 64 KB/s, jadi **audio tidak bisa mengalir lewat
USB**. PC memberi feed ke amplifier lewat RCA analog; USB hanya membawa
konfigurasi.

## Layout

```
src/hid/     transport hidraw dan framing AE 1E len tag / 80 len cmd .. CRC16
src/ble/     worker Bluetooth LE (Python/BlueZ) dan IPC ke GUI
src/dsp/     desain biquad, keluarga filter, fixed point, model respons, solver autoeq
src/gui/     konsol Xlib: layout, controls, presets, bluetooth, protection, aset tertanam
src/web/     server HTTP beserta UI yang di-inline
tests/       test frame, matematika, respons, BLE, dan tabel data konsol
tools/       zpsniff (C) plus zpdecode.py dan diffdump.py
docs/        catatan protokol, pemetaan konsol, verifikasi USB, BLE, proteksi
web/         aset UI yang dilayani zp84web
release/     binary x86_64 siap pakai dan SHA256SUMS
examples     tangkapan referensi di "Default Project/"
```

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
- [RELEASE.md](RELEASE.md) — catatan rilis

## Test

`make test` menjalankan lima suite: 109 check bernama pada binary framing dan
matematika filter, 254 assertion di seluruh tabel data konsol, 24 di model
respons, dan 7 kasus protokol BLE di Python — sekitar 394 assertion
total, semuanya memakai konstanta asli vendor. Suite konsol menguji seluruh
lapisan UI tanpa perangkat keras atau X server dengan memalsukan transport.

`build/zpresponse_test out.csv` menulis sapuan respons ke CSV.

## Batasan yang diketahui

- **Baca dan tulis BLE belum diverifikasi di perangkat keras.** Worker
  mengikuti framing yang sama dengan USB dan tercakup unit test, tapi jalur
  Bluetooth belum terbukti. Lihat [docs/BLUETOOTH.md](docs/BLUETOOTH.md).
- **Crossover Bessel tidak dimodelkan** di grafik respons.
- **Grafik respons adalah model parameter**, bukan pengukuran amplifier.
- **Proteksi speaker ada di sisi aplikasi.** Tool lain atau aplikasi vendor
  tetap bisa mengubah amplifier.
- **Preset mencakup 876 dari 1.934 register.** Sisanya terdaftar tapi belum
  diberi nama.
- **UI browser tidak punya proteksi maupun lock output.**

## License

MIT, lihat [LICENSE](LICENSE).

## Status

Reverse engineering dari id parameter masih berjalan. Pemetaan di atas
mencakup kontrol yang diekspos tool PC vendor; id sisanya terdaftar tapi
belum diberi nama. Lihat tabel status di
[docs/PROTOCOL.md](docs/PROTOCOL.md).
