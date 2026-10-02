# Tampilan desktop ZP 8.4 AMP

Panel utama mengikuti screenshot pengguna: menu Memory/Option/Info,
input dan Mixer, crossover kiri, grafik utama dan pemilih overlay channel,
tabel 31 band EQ dan fader, diagram mobil, master volume, unit delay,
delapan channel output, serta kolom kontrol di kanan.

## Menjalankan

```sh
make
./build/zp84gui --read-dsp
```

Ukuran antarmuka menyesuaikan resolusi layar saat dibuka. Untuk menentukan
besar teks dan seluruh panel secara manual:

```sh
./build/zp84gui --read-dsp --scale 1.5
```

Skala yang diterima 0.75 sampai 2.5. Jendela dapat diubah ukurannya; skala pilihan dipakai saat dibuka.
Saat resize, seluruh panel menyesuaikan secara proporsional dan dipusatkan
agar 31 kolom EQ tidak terpotong. Font Helvetica X11
memiliki fallback `fixed` bila font yang diminta tidak tersedia.

## Interaksi

- Klik strip output atau tombol 1–8 untuk memilih channel.
- Klik daftar CH di kanan grafik untuk menambah/menghapus overlay; channel
  yang sedang dipilih tetap ditampilkan.
- Klik kolom EQ untuk memilih band dan melihat F/Q/G di grafik.
- Klik Ms/Cm/Inch untuk mengganti unit tampilan delay. Konversi mengikuti
  kecepatan suara 346 m/s yang dipakai aplikasi vendor.
- Baca DSP atau R mengambil snapshot lengkap baru; nilai nol tetap valid.
- Memory > Simpan snapshot menulis file unik `zevox-snapshot-XXXXXX` di
  direktori kerja. Itu register mentah, bukan preset yang bisa direstore.
- Option membuka Register USB atau Simulasi lokal. Tab Panel DSP kembali
  ke tampilan utama. Esc menutup dialog; Esc di luar dialog menutup aplikasi.
- Kontrol input/mixer dan parameter yang sudah dipetakan mendukung write
  terverifikasi; lihat pembaruan interaksi di bawah.

## Filter dan grafik

Kode filter telah dicocokkan dengan `g/a.c,d` dan array resource
`filter_type` (`0x7f020004`) serta `filter_rate` (`0x7f020002`) dari APK.
Urutan keluarga Butter-W, Bessel, Link-Ril. Urutan rate 6,12,18,24,30,36,
42,48 dB/oct, OFF. Contoh HPF 56 = Link_R 36 dB/oct, LPF 25 = Link_R
24 dB/oct, LPF 69 = OFF. Kode tidak dikenal ditampilkan `--`.

Kurva adalah model matematis 48 kHz: peaking EQ, Butterworth, dan
Linkwitz–Riley. Bypass EQ dibaca dari ID USB 65 + channel (indeks nol).
Bessel dan orde LR nonstandar ditandai tidak didukung, tanpa kurva palsu.
Grafik bukan pengukuran respons audio. F2 mengganti komponen respons; F3
mengekspor koefisien dan 2048 sampel ke file sementara. Lihat [FREQUENCY-RESPONSE.md](FREQUENCY-RESPONSE.md).
Model Linkwitz–Riley menunjukkan -6.0206 dB di titik potong. Status input,
master volume dan mute dibaca dari register; link output adalah preferensi lokal.
Nilai yang belum dipetakan ditampilkan `--` atau ikon `?` yang redup.

## Verifikasi

- Build Make dan CMake, seluruh tiga suite CTest lulus.
- Tes konversi frequency/level/delay, kode filter dan OFF, level cutoff,
  navigasi menu/channel/band/overlay/unit, hit testing pada skala 1.5,
  snapshot parsial/lengkap, respons salah/pendek/timeout.
- Uji GUI dengan hardware berhasil membaca seluruh 1.934 ID.
- Screenshot pada skala 1.5 diperiksa untuk keterbacaan, kolom EQ, grafik,
  strip output, dan batas panel.

## Pembaruan interaksi September 2026

Panel memakai palet slate dengan penanda teal. Kontrol USB aktif setelah
Connect dan Baca DSP berhasil. Nilai hanya diperbarui setelah write mendapat
acknowledgement dan readback cocok; kegagalan membatalkan validitas snapshot.

- Input membuka pilihan AUX, Bluetooth, High level, atau USB media.
- Mixer menampilkan matriks delapan output untuk routing ON/OFF dan level 0–100%.
  AUX/Bluetooth memiliki dua input; High level memiliki empat. Routing USB
  media belum tersedia karena belum ada pemetaan terverifikasi.
- Fader EQ, master, dan output menampilkan preview selama drag; write terjadi
  saat tombol kiri dilepas. Esc membatalkan drag.
- Klik angka frequency, Q, gain, master/output level, atau delay untuk mengetik
  nilai. Angka lama otomatis terpilih. Ctrl+A, Backspace, Delete, Enter, Esc,
  tombol Terapkan/Batal, dan tanda desimal titik/koma didukung.
- HPF/LPF Type dan Oct berganti pilihan saat diklik; Oct mencakup OFF.
- Ikon speaker mengatur mute channel; ikon master mengatur mute semua output.
- Edit EQ membuka gain band terpilih. Reset EQ meratakan gain; Restore EQ
  mengembalikan frequency, gain dan Q sebelum reset selama sesi koneksi ini.
- Link pairs menyalin pengaturan channel terpilih ke pasangannya, kemudian
  menyamakan edit volume, EQ, crossover, phase, mute, dan delay pada pasangan itu.
  Lock output mengunci perubahan level, phase, mute, dan delay dari panel ini.
- Reset Output mengatur level channel terpilih ke 0, phase normal, delay 0.
- Menu Info menggantikan tombol Encrypt yang belum didukung.

Validasi pembaruan: suite Make (DSP, framing, GUI), pengujian write mock
untuk source/mixer, konversi dan validasi input, drag, lock/link, undo serta
kegagalan write. Pemeriksaan visual memakai Xvfb tanpa amplifier; operasi
write pada perangkat fisik belum diuji dalam pembaruan ini.

## Posisi speaker: Mobil / Ruangan

Panel posisi memiliki pilihan Mobil, Ruangan, dan Reset. Klik angka channel
untuk memilih output; tahan tombol kiri dan geser untuk menyesuaikan lokasi
speaker. Posisi dibatasi di dalam panel. Lepaskan untuk menyimpan; Esc
mengembalikan posisi sebelum drag. Drop terlalu dekat dengan angka lain
akan dibatalkan agar semua channel tetap dapat dipilih.

Kedua mode menyimpan posisi sendiri. Reset hanya mengembalikan posisi mode
aktif. Mode dan koordinat tersimpan otomatis dalam `zp84-layout.conf` pada
folder kerja aplikasi, lalu dimuat pada pembukaan berikutnya dari folder
tersebut. Jika folder tidak dapat ditulis, perubahan tetap aktif selama sesi
dan pesan kegagalan simpan muncul di footer. Ini diagram penempatan lokal;
perubahan posisi tidak mengubah routing, delay, atau register amplifier.

## Jarak dan delay dari posisi dengar

Titik **P** pada diagram adalah posisi dengar. Geser P atau angka channel
untuk memperbarui preview jarak dan delay. Preview channel terpilih muncul
di footer selama drag; panel **Jarak / Delay** di toolbar menampilkan semua
jarak dan membandingkan hasil perhitungan dengan delay DSP yang terbaca.

1. Pilih Mobil atau Ruangan, lalu buka Jarak / Delay > Atur lebar (m).
2. Masukkan lebar nyata yang diwakili **seluruh denah dari batas kiri ke
   kanan**, bukan lebar gambar bodi mobil. Skala yang sama berlaku di kedua
   sumbu. Rentang lebar 0,5–30 m; tinggi denah mewakili 178/158 kali lebar.
3. Tempatkan P dan channel sesuai instalasi. Ini jarak lurus 2D, tanpa
   ketinggian speaker atau pengukuran akustik otomatis.
4. Periksa preview dan klik **Terapkan Delay** untuk mengirim nilai ke DSP.

Rumus: `(jarak speaker aktif terjauh - jarak channel) / 0,346` ms. Channel
terjauh mendapat 0 ms. Nilai yang dikirim dibulatkan ke sampel pada 48 kHz
mengikuti encoding delay perangkat. Channel mute tidak masuk perhitungan
acuan dan delay-nya tidak diubah. Unmute mengubah kelompok speaker aktif;
periksa preview dan terapkan kembali bila diperlukan.

Pengiriman memerlukan skala yang telah disimpan, USB terhubung, snapshot
lengkap, dan output tidak terkunci. Jika salah satu hasil melebihi 20 ms,
seluruh pengiriman dibatalkan sebelum write pertama. Jika transaksi gagal
di tengah proses, aplikasi melaporkan jumlah channel yang sudah diterapkan
dan meminta Baca DSP ulang; pengiriman multi-channel tidak atomik.

Posisi P dan skala disimpan terpisah untuk kedua mode dalam format layout
versi 2. File versi 1 tetap dibaca dan memerlukan pengaturan skala sebelum
fitur Terapkan Delay digunakan. Reset mengembalikan posisi P/channel;
skala yang sudah dikalibrasi tetap dipertahankan.

## Pelengkapan dari screenshot referensi

- **Mixer** menampilkan CH1–CH8 bersamaan, dengan dua baris AUX/Bluetooth
  atau empat baris High level. Klik ON/OFF untuk routing, angka persen untuk
  edit gain. Setelah Enter/Batal, editor kembali ke mixer.
- **Option > Input volume** menyediakan level USB dan Bluetooth 0–100.
  Klik angka untuk input numerik atau geser slider; slider menulis saat dilepas.
  Esc membatalkan drag. Angka diperbarui setelah verifikasi USB berhasil.
- **Option > Noise Gate** menyediakan tingkat vendor 0–20; nol berarti OFF.
  Tingkat ini bukan angka dB.
- **Memory** menyediakan delapan preset lokal di folder kerja sebagai
  `zp84-preset-1.scene` sampai `zp84-preset-8.scene`. Simpan menimpa slot yang
  dipilih. Muat menerapkan parameter yang sudah dipetakan: EQ, crossover,
  output, master, routing, sumber input, volume input, dan noise gate.
  Tombol Ekspor snapshot tetap tersedia untuk diagnostik.
- Preset harus lengkap dan nilainya valid sebelum satu write pun dikirim.
  Output di-mute selama penerapan, kemudian status mute preset dipulihkan.
  Hanya nilai yang berbeda dikirim. Penerapan berlangsung sinkron; UI bisa
  menunggu selama transaksi USB. Kegagalan berhenti pada transaksi pertama
  yang gagal dan meminta pembacaan ulang karena hasil dapat parsial.
- Output lock juga mencegah pengubahan routing/level di Mixer dan pemuatan
  preset. Reset/Restore EQ lama dibuang
  setelah pemuatan preset berhasil agar undo tidak menimpa preset baru.

Preset lokal ini **bukan slot preset internal amplifier** dan bukan format
`.jib`/`.jis` vendor. Penyimpanan internal, factory reset, dan Encrypt belum
tersedia karena protokol perintah tersebut belum terverifikasi. Preset menolak
snapshot dengan nilai di luar rentang kontrol yang didukung.

Validasi perubahan ini: tiga suite Make lulus, termasuk round-trip preset,
file tidak lengkap/perintah terlarang, output lock, write gagal di tengah
restore, seluruh baris mixer, encoding volume/noise gate dan kembali dari
editor. Dialog diperiksa dengan Xvfb dan data simulasi; write baru belum
diuji pada amplifier fisik.

Perbaikan validasi preset: crossover HPF/LPF menerima 10–23000 Hz,
termasuk encoding 10 Hz `0x8064` pada baseline perangkat CH5/CH6.
Batas editor crossover disamakan; batas EQ tetap 20–20000 Hz.

## Mixer Remix dan ukuran jendela

Mixer memakai dialog lebar seperti referensi: delapan kolom OutCh1–OutCh8,
baris High level 1–4 atau AUX/BT kiri-kanan, ikon power biru dan slider
horizontal metalik. Power mengganti enable jalur; slider menampilkan preview
angka dan baru mengirim gain ketika dilepas. Klik angka membuka editor.
Esc membatalkan drag tanpa write. Enable tetap dipertahankan saat gain diubah.

Tombol **Maksimalkan** dan **Layar penuh** tersedia di kanan atas. Klik lagi
untuk pulihkan; F11 mengganti mode layar penuh. Esc menutup dialog atau
membatalkan drag dahulu, lalu keluar dari layar penuh tanpa menutup aplikasi.
Tombol maksimalkan bawaan window manager juga aktif. Skala, font, aset gambar,
dan koordinat klik menyesuaikan ukuran baru; ruang sisa menjadi margin.
Resize membatalkan drag yang belum diterapkan.

Verifikasi: suite Make dan uji desktop Xvfb/Openbox untuk maximize,
fullscreen, Escape, dan restore ukuran asal; pemeriksaan visual mixer serta
tes gain mixer, enable yang dipertahankan, dan klik dengan margin resize.

### Nama preset Memory

Klik **Simpan** pada slot untuk mengisi nama (1–24 karakter), lalu **Simpan
preset** atau Enter. Nama tersimpan bersama parameter dan tampil saat aplikasi
dibuka kembali. Klik **Load** pada baris nama itu untuk menerapkan preset.
Simpan pada slot yang sama mengganti nama serta isinya. Batal/Esc tidak
mengubah file. Format scene versi 2 menyimpan nama; scene versi 1 tetap bisa
dimuat dan ditampilkan sebagai "Preset lama".

Label jenis output: CH1–CH4 menampilkan ikon amplifier AMP **dan** ikon konektor RCA;
CH5–CH8 menampilkan ikon konektor RCA saja. Label bersifat statis sesuai konfigurasi
pengguna, tidak menyatakan status daya atau menggantikan tombol mute.

### Perapian navigasi

Memory memakai tabel slot dan nama dengan tombol Simpan/Simpan ulang serta
Load pada setiap baris. Input volume dan Noise Gate juga tersedia langsung
di toolbar. Pesan panjang dipotong dengan elipsis agar tidak melewati panel.

### Menandai AMP/RCA yang digunakan

Klik ikon/label AMP atau RCA pada strip channel untuk memberi atau menghapus
centang. Pilihan menyala, pilihan kosong redup. CH1–CH4 dapat menandai AMP,
RCA, keduanya, atau tidak keduanya; CH5–CH8 hanya RCA. Penanda awal kosong.
Ini catatan instalasi manual, **bukan pembacaan atau sakelar daya output DSP**.
Mute channel tetap melalui tombol speaker. Penanda disimpan terpisah di
`zp84-outputs.conf` pada folder kerja, sehingga tidak berubah saat Load preset.

## Link pasangan stereo

Pilih channel sumber, lalu klik **Link pairs**. CH1 menyalin ke CH2, CH2 ke
CH1, dan seterusnya untuk pasangan 3/4, 5/6, 7/8. Link baru aktif setelah
seluruh salinan terverifikasi. Hanya pasangan yang dipilih yang dihubungkan;
beberapa pasangan dapat di-link secara terpisah. Header `CHn =` menandai
channel terhubung; tombol menjadi **Unlink pairs** pada pasangan aktif.

Perubahan dari sisi mana pun menyamakan level output, phase, mute, delay,
HPF/LPF (frekuensi, tipe, slope), serta 31 band EQ (frekuensi, Q, gain).
Reset/Restore EQ dan Reset Output juga berlaku ke pasangan. Unlink tidak
mengembalikan nilai sebelumnya. Mixer tetap mempertahankan routing sumber
L/R masing-masing, dan penanda AMP/RCA tetap merupakan catatan instalasi.

Mengaktifkan Link membutuhkan snapshot USB lengkap dan output tidak terkunci.
Kegagalan write dapat menghasilkan salinan parsial: link dilepas dan pengguna
harus Baca DSP kembali. Link adalah keadaan sesi lokal; koneksi/pembacaan
ulang serta penerapan preset yang mengubah nilai melepas link. Penerapan
delay geometris meminta Unlink dahulu karena setiap speaker bisa memiliki
jarak berbeda. Pengujian memakai mock USB, tanpa write pada amplifier fisik.

## Nama speaker per channel

Klik label speaker di bawah tombol CH (misalnya FL-Tweeter), ketik nama baru
maksimal 24 karakter, lalu pilih **Simpan nama** atau Enter. Escape/Batal
membatalkan. Nama dapat diubah tanpa koneksi USB dan disimpan di
`$HOME/.zp84-speaker-names.conf`. Nama kosong ditolak. Label ini tidak
mengubah jenis proteksi, crossover, routing, atau parameter DSP.
