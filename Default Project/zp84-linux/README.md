# zp84-linux

Paket reverse-engineering + scaffolding Linux untuk **ZP 8.4 AMP**
(vendor `ZP 8.4AMP` / ZEVOX, aplikasi tuning DSP audio mobil 8 channel).

> **Status: BELUM SIAP PAKAI.** Struktur frame sudah terverifikasi, tapi
> peta offset → parameter DSP (Gain, Freq, Q, Delay, dll) belum ada.
> Yang bisa dilakukan sekarang: kirim frame polling, baca frame, dan
>obili protokol lebih lanjut. Lihat [Yang sudah pasti](#yang-sudah-pasti)
> dan [Yang belum diketahui](#yang-belum-diketahui).

---

## Struktur folder

```
zp84-linux/
├── zp84/                      paket python (library)
│   ├── __init__.py            ekspor publik
│   ├── frames.py              codec frame 65 byte + database frame known
│   └── transport.py           serial / RFCOMM / BLE
│
├── data/
│   ├── captures/              data mentah hasil tangkapan
│   │   ├── capture-run1.txt   154 frame, 60 detik
│   │   ├── recheck.txt        51 frame, 20 detik
│   │   ├── run1-poll.json     hasil parser: index + timeline
│   │   └── run1-poll.frames.bin
│   └── skin/                  aset GUI untuk bikin tampilan Linux
│       ├── assets.json        25 sprite + palet + content bbox
│       ├── theme.css          28 warna semantik (kurasi, untuk tema)
│       ├── palette-full.css   94 warna lengkap hasil ekstraksi
│       ├── build-manifest.ps1 regenerate assets.json
│       └── images/            25 PNG
│
└── tools/
    ├── tocapture.py           log spyon -> dataset terstruktur
    ├── analyze-diff.py        cari offset yang berubah (inti RE)
    ├── verify.py              sanity check paket vs data asli
    ├── detect.sh              deteksi device di Linux
    ├── detect-hid.sh          deteksi USB HID di Linux
    ├── zp84_log.py            logger serial generic
    └── windows/
        ├── spyon.py           hook I/O proses Windows (frida)  <- utama
        ├── capture-diff.py    varian diferensial
        └── handle2name.py     resolusi handle -> nama device
```

## Yang sudah pasti

Semua di bawah ini **diubah dari tangkapan lalu diuji ulang oleh
`tools/verify.py`**, bukan dari asumsi.

| Fakta | Nilai | Bukti |
|---|---|---|
| Panjang frame | **65 byte**, selalu | 205 frame di 2 capture |
| Prefix | `00 AE 1E 00` | konstan di semua frame |
| Arah | **TX saja** (WriteFile) | tidak ada satu pun RX |
| Metode | `WriteFile` 65 byte, bukan HID report | `HidD_SetOutputReport` dkk. tidak pernah dipanggil |
| Frekuensi | 3–4 frame/detik | timer polling status |
| Tail | offset 21..64 selalu `0x00` | padding |

Tiga frame polling yang sudah teridentifikasi:

```
POLL_STATUS   subfunc=0x0F  x135/154   rasio 7:1 thd POLL_INFO
              00 AE 1E 00 0F C8 80 0D 06 06 12 06 20 00 0C 00 20 00 21 F5 4B 00

POLL_INFO     subfunc=0x09  x19/154
              00 AE 1E 00 09 C8 80 07 03 06 1C 00 01 F1 DF 00 00 00 00 00 00

POLL_SUB07    subfunc=0x07  x1         (baru ketemu di recheck.txt)
              00 AE 1E 00 07 C8 80 05 06 06 12 BE 46 00 00 00 00 00 00 00 00
```

Layout frame (`zp84/frames.py`):

```
offset  lebar  nilai        status
------  -----  -----------  -------------------------------
0       1      0x00         pasti, selalu 0
1       1      0xAE         pasti, selalu 0xAE
2       1      0x1E         pasti, konstan
3       1      0x00         pasti, selalu 0
4       1      varying      kandidat sub-fungsi 0x07/09/0F
5       1      0xC8         pasti, konstan
6       1      0x80         pasti, konstan
7..20   14     varying      isi perintah  <-- belum dipetakan
21..64  44     0x00         padding
```

Byte `0xAE` di offset 1 cocok dengan byte pertama UUID
`0000ae00-0000-1000-8000-00805f9b34fb` yang ada di `classes.dex` APK —
indikasi framing milik modul yang sama.

## Yang belum diketahui

1. **Peta offset → parameter.** Offset 7..20 berubah-ubah, tapi belum
   dikaitkan ke kontrol UI mana. Ini yang paling penting.
2. **Checksum.** Offset 21 selalu 0, jadi belum ketemu field checksum.
   Hipotesis XOR byte 0..20 menghasilkan `0x7D` / `0xC0` yang tidak cocok
   dengan apa pun — kemungkinan tidak ada checksum, atau di tempat lain.
3. **Respons RX.** Nol frame RX di 205 frame tangkapan. Kemungkinan besar
   perangkat diam, atau balasan lewat jalur lain (mis. USB IN),
   atau memang protocolnya one-way.
4. **Handle `0x5bc` device apa.** Belum teratasi — butuh sesi Administrator
   (lihat [Mendapatkan identitas device](#mendapatkan-identitas-device)).
5. **Peran `0xC8` dan `0x80`.** Keduanya konstan di ketiga frame, jadi
   belum jelas apakah marker, versi protokol, atau bagian checksum.

## Pakai sekarang

```python
import zp84

# baca frame yang tertangkap
with open("data/captures/run1-poll.frames.bin", "rb") as f:
    for line in f:
        tag, body = line[0], line[1:-1]
        print(zp84.describe(body))

# rakit frame sendiri
# body = offset 7..20, tepat 14 byte
frame = zp84.build(subfunc=0x0F, body=bytes.fromhex("0D0606120620000C00200021F54B"))
print(frame.hexdump())

# kirim ke amplifier (Linux, USB CDC-ACM)
with zp84.SerialTransport("/dev/ttyACM0", baud=115200) as t:
    t.send_frame(zp84.by_label("POLL_STATUS"))
```

CLI:

```bash
python tools/verify.py                       # cek paket vs data asli
python tools/tocapture.py <log> <stem>        # parse log spyon
python tools/analyze-diff.py <log>            # cari offset yang berubah
```

## Cara melanjutkan

### 1. Tangkap frame aksi (paling penting)

Frame polling itu konstan, jadi protocol cuma bisa dipetakan dari frame
yang **berubah**. Di Windows, rekam sambil berinteraksi:

```powershell
python tools\windows\spyon.py -n "ZP 8.4 AMP_EN.exe" --seconds 300 -o run2.txt
```

Lalu **satu aksi satu per satu, jeda ~2 detik**:

1. Klik `CH1` … `CH8` satu per satu
2. Geser slider `Gain` (naik-turun sedikit)
3. Ubah `HPF Freq` (100 → 250), lalu `Oct` (36dB → 12dB)
4. Ubah `LPF Freq` dan `Type` (`Link_R` → `Link_L`)
5. Klik ikon mute (speaker) di `CH1`
6. Ubah `Delay` `CH1` (0 → 0.050)
7. Ubah `Volume` master
8. Klik `Mixer`, `GEQ`, `Reset EQ`, `Link Output`

Lalu analisis:

```bash
python tools/analyze-diff.py run2.txt
```

Keluaran akhir memuat tabel offset yang berubah + nilai yang pernah
muncul — itu bahan untuk memetakan `Gain`/`Freq`/`Delay` ke offset.

Kalau `nama proses` tidak ketemu, jalankan app dulu. Semua operasi ini
hanya menulis ke amplifier, tidak mengubah apa pun di komputer.

### 2. Identitas device (Linux)

```bash
sudo ./tools/detect.sh            # cari /dev/ttyACM* atau /dev/ttyUSB*
sudo ./tools/detect-hid.sh        # kalau ternyata HID
```

`detect.sh` akan menampilkan device USB dengan usage page vendor
(`0xFF00`) dan report descriptor > 30 byte kalau memang HID.

### 3. Mendapatkan identitas device (Windows)

Butuh PowerShell **sebagai Administrator**:

```powershell
python tools\windows\handle2name.py 9700 0x5bc
```

Sesi non-elevated akan gagal diam-diam. Alternatif tanpa admin —
jalankan app di bawah Frida supaya `CreateFile` tertangkap sejak awal
(harus **tutup** app yang sedang jalan dulu):

```powershell
python tools\windows\capture-diff.py --spawn --seconds 120
```

### 4. Replay dari Linux

Setelah transport ketemu:

```python
import zp84
t = zp84.open_auto("/dev/ttyACM0", "/dev/ttyUSB0", "/dev/rfcomm0")
t.send_frame(zp84.by_label("POLL_STATUS"))
rx = t.read_frame(65, timeout=1.0)
print(zp84.describe(rx) if rx else "tidak ada balasan")
```

> `KNOWN_FRAMES` di-key oleh **byte** frame, jadi jangan tulis
> `KNOWN_FRAMES["POLL_STATUS"]`. Pakai `by_label("POLL_STATUS")`, atau
> `all_frames()` untuk melihat semua.

## Sumber & provenance

- **App**: `ZP 8.4 AMP_EN.exe`, 9,36 MB, file asli `ZP 8.4AMP`,
  versi `2.2021.1.26` (18 Juni 2021), bahasa China (Simplified).
  SHA256 `8AC6B6ED6787E5A11B85FF8AC7F8AE4AE44CFCABE2806B6AE1534061B14CF67E`.
  Suffix `_EN` ditambahkan pengepak ulang, bukan nama asli.
  Delphi Win32 (dibukti dari `RICHED20.DLL`, `OLEACC.dll`, pola
  version resource).
- **APK** (opsional): `com.tigerapp.jheqchart_application` v1.5.17,
  SHA256 `407777B82835BCF124DB346008D75E5CDB10D0213221040D88CB8BCFF28DD450`.
  Memberi UUID `ae00/ae01/ae02` dan SPP `00001101`.
- **Skin**: 25 PNG di dalam EXE, dibuat Adobe Photoshop CC / ImageReady 2013.
  Lihat `data/skin/` + `zp84-data/README.md` di repo induk.
- **Bluetooth**: perangkat bernama `DSP audio8`, MAC `C0:00:00:0B:91:59:DA`,
  VID `000105D6` PID `000A`, muncul sebagai SPP `COM4`.
  Tapi Bluetooth di app ini untuk **audio playback**; kontrol DSP lewat USB.

> Sprite dan protocol ini karya vendor. Pakai untuk kebutuhan pribadi
> tidak masalah; mendistribusikan ulang sprite/kode ke publik adalah
> urusan lisensi terpisah dari persoalan teknis.
