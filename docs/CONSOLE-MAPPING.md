# Panel DSP mengikuti referensi aplikasi Windows

Panel utama menampilkan crossover di kiri, grafik model EQ, tabel 31 band
beserta fader interaktif, pemilih channel, dan delapan strip output. Tab
Register USB dan Simulasi lokal tetap terpisah. Tombol 1–8 memilih channel;
R atau Baca DSP memperbarui snapshot. Koneksi terputus ditandai di footer.

Label speaker mengikuti screenshot pengguna, bukan hasil identifikasi
speaker fisik: tweeter CH1–2, woofer CH3–4, midrange CH5–6, subwoofer CH7–8.

## Pemetaan yang digunakan

Untuk indeks channel `c = 0..7` dan band `b = 0..30`:

| Nilai | ID USB | Konversi |
|---|---|---|
| HPF frequency | `139 + 136*c` | decoding frequency di bawah |
| LPF frequency | `143 + 136*c` | decoding frequency di bawah |
| HPF/LPF filter code | `138/142 + 136*c` | keluarga + slope/OFF; lihat DESKTOP-UI.md |
| EQ frequency | `147 + 136*c + 4*b` | decoding frequency |
| EQ gain | `148 + 136*c + 4*b` | `(raw-500)/10` dB |
| EQ Q | `149 + 136*c + 4*b` | `raw * (19/6) / 100` |
| Level + phase | `26+c` | skala vendor di bawah |
| Delay | `73+c` | pembulatan 48 kHz di bawah |

Frequency: jika bit `0x8000` aktif, `(raw & 0x7fff)/10` Hz;
jika tidak, `raw` Hz. Tabel dan konversi PC terlihat di `work/zp.dis`
pada `0x41d256`, `0x41d2ee`, `0x41d2a7`, dengan tabel PE `0x667080`,
`0x6670b0`, `0x6670e0`, `0x667110`, `0x667140`.

Level: `(raw >= 5000 ? raw-5000 : raw)/10` menggunakan pembagian integer;
jika hasil >40, kurangi 40. Phase: raw <5000 adalah toggle phase aktif,
ditampilkan 180 derajat sesuai screenshot. Sumber APK:
`f/a.java` array c; `d/o.java` metode k/n; `g/a.java` metode f/h;
`d/m.java` menghubungkan toggle ke `btn_phase`. API `BTService.s`
mengakses indeks `i-1`.

Delay mengikuti `d/m.java` metode d dan b:
`round(round(raw*48/1000)*1000/48)/1000` ms. Snapshot yang dibaca
memiliki 208 untuk CH7–8, ditampilkan 0.208 ms, cocok dengan screenshot.

## Batas

Bypass EQ belum dipetakan. Jangan
mengisi nilai tersebut dari screenshot lama. Kurva menampilkan model EQ dan
crossover Butterworth/Linkwitz–Riley 48 kHz, bukan pengukuran audio.
Bessel tidak dimodelkan. Kode filter dan OFF telah diterjemahkan berdasarkan
resource APK; lihat [DESKTOP-UI.md](DESKTOP-UI.md).
Kontrol tulis menggunakan acknowledgement dan pembacaan ulang untuk verifikasi.

Pengujian: konversi frequency/level/delay, snapshot lengkap/parsial,
respons salah/pendek/timeout, navigasi halaman, serta pembacaan GUI langsung
ke hardware dan pemeriksaan screenshot. Build Make dan CMake diuji.

## Kontrol tulis yang kini dipetakan

Kontrol
berikut kini tersedia dengan acknowledgement dan readback USB:

| Kontrol | ID USB | Encoding |
|---|---|---|
| Input source | 1554 | AUX=1, Bluetooth=2, High level=3, USB media=7 |
| Master level | 12..19 | level vendor + phase normal (5000) |
| Output mute | 2..9 | 0=unmute, 1=mute |
| Mixer High level | 1226 + 8*c + i | i=0..3; gain persen *256 + enable |
| Mixer Bluetooth | 1360 + 8*c + i | i=0..1; gain persen *256 + enable |
| Mixer AUX | 1482 + 8*c + i | i=0..1; gain persen *256 + enable |

Sumber lokal: APK `activity/MainActivity.java` callback `d.t.b` menulis
input ke alamat 1555. `d/u.java` mengatur mixer dan `f/a.java` menyimpan
tabel alamatnya. `d/s.java` mengatur master, `d/o.java` mengatur mute.
`service/BTService.java` metode `c(int,int)` mengurangi alamat vendor satu
untuk mendapatkan indeks register USB. Mixer USB media belum dipetakan.

## Volume input dan noise gate

| Kontrol | ID USB | Encoding |
|---|---|---|
| USB_VOL | 1908 | 500 + level 0–100 |
| BT_VOL | 1900 | 500 + level 0–100 |
| Noise gate | 1565 | tabel tingkat vendor di bawah |

Bukti lokal: APK `d/w.java` callback SeekBar memakai alamat 1909/1901,
`g/a.java b(false, level)` menambahkan 500. Noise gate memakai alamat 1566
serta `g/a.java m`: `[0,1,2,3,4,5,6,7,8,9,10,11,13,14,16,18,23,29,41,65,103]`.
Alamat vendor dikurangi satu oleh BTService sebelum menjadi indeks USB.
Pemetaan ini bersumber dari kode vendor; pengujian write dilakukan dengan
mock, belum melalui amplifier fisik.
