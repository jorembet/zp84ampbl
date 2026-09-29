# Video Tutorial: ZP 8.4 AMP DSP Configuration Tool
## Script Lengkap - Bahasa Indonesia

---

## BAB 1: PENDAHULUAN (0:00 - 1:30)

### Visual: Opening dengan logo dan judul

**Narasi:**
> "Selamat datang dalam video tutorial aplikasi ZP 8.4 AMP DSP Configuration Tool. Aplikasi ini adalah tools open-source untuk mengkonfigurasi amplifier DSP Zevox ZP 8.4 AMP — amplifier audio mobil 8 channel. Aplikasi ini menggantikan tools Windows bawaan vendor yang hanya tersedia di Windows, dan kini bisa dijalankan di Linux dengan antarmuka grafis native."

### Visual: Tampilan hardware ZP 8.4 AMP

**Narasi:**
> "ZP 8.4 AMP adalah amplifier DSP 8 channel dengan fitur:
> - 31-band graphic EQ per channel
> - Crossover dengan filter Butterworth, Bessel, dan Linkwitz-Riley
> - Time alignment per channel
> - Mixer matrix 8 output
> - Kontrol master volume dan mute
> - Input source: AUX, Bluetooth, High level, USB media
> - Preset system dengan 8 slot"

### Visual: Screenshot aplikasi

**Narasi:**
> "Aplikasi ini berkomunikasi dengan amplifier melalui USB HID. Semua parameter dibaca dan ditulis langsung ke hardware dengan verifikasi ack dan readback."

---

## BAB 2: INSTALLASI (1:30 - 4:00)

### Visual: Terminal

**Narasi:**
> "Pertama, mari kita install aplikasi ini. Pastikan Anda sudah memiliki dependencies: compiler C, libX11, dan libm."

### Visual: Command line

```bash
# Clone repository
git clone <repo-url>
cd zp84-dsp

# Build aplikasi
make

# Install udev rules (membutuhkan root)
sudo ./scripts/install-udev.sh

# Install aplikasi ke ~/.local
./scripts/install-app.sh
```

**Narasi:**
> "Perintah `make` akan compile dua binary: `zpsniff` untuk eksplorasi protocol USB, dan `zp84gui` untuk antarmuka grafis utama."

### Visual: File .desktop muncul di menu aplikasi

**Narasi:**
> "Setelah install, aplikasi akan muncul di menu desktop Anda sebagai 'ZP 8.4 AMP DSP'."

---

## BAB 3: MENJALANKAN APLIKASI (4:00 - 5:30)

### Visual: Desktop, klik icon aplikasi

**Narasi:**
> "Untuk menjalankan aplikasi, pastikan amplifier sudah terhubung via USB. Lalu jalankan:"

```bash
./build/zp84gui --read-dsp
```

### Visual: Aplikasi terbuka, loading snapshot

**Narasi:**
> "Aplikasi akan membaca seluruh 1934 parameter ID dari amplifier. Proses ini memakan waktu beberapa detik. Setelah selesai, Anda akan melihat antarmuka utama."

### Visual: Opsi scale

**Narasi:**
> "Untuk layar dengan resolusi tinggi, Anda bisa menambahkan opsi scale:"

```bash
./build/zp84gui --read-dsp --scale 1.5
```

---

## BAB 4: TAMPILAN UTAMA (5:30 - 8:00)

### Visual: Annotated screenshot dengan label

**Narasi:**
> "Mari kita kenali bagian-bagian utama antarmuka:"

### Visual: Highlight area

**Narasi:**
> "1. **Menu bar** — Memory, Option, Info
> 2. **Input source** — Pilihan AUX, Bluetooth, High level, USB media
> 3. **Mixer** — Matriks routing 8 output
> 4. **Crossover** — HPF/LPF per channel
> 5. **Grafik EQ** — Tampilan frekuensi response
> 6. **31-band EQ** — Tabel EQ dengan fader interaktif
> 7. **Diagram mobil** — Posisi speaker
> 8. **Master volume** — Kontrol volume utama
> 9. **8 channel output** — Strip kontrol per channel
> 10. **Kolom kanan** — Kontrol tambahan"

---

## BAB 5: CHANNEL DAN EQ (8:00 - 12:00)

### Visual: Klik tombol 1-8

**Narasi:**
> "Ada 8 channel output: CH1-CH2 untuk tweeter, CH3-CH4 untuk woofer, CH5-CH6 untuk midrange, dan CH7-CH8 untuk subwoofer. Klik tombol 1-8 atau strip output untuk memilih channel."

### Visual: Drag fader EQ

**Narasi:**
> "Setiap channel memiliki 31-band graphic EQ. Anda bisa:
> - **Drag fader** untuk mengatur gain band tertentu
> - **Klik kolom EQ** untuk memilih band dan melihat F/Q/G di grafik
> - **Klik angka frequency, Q, atau gain** untuk mengetik nilai manual
> - **Edit EQ** untuk membuka editor gain band terpilih
> - **Reset EQ** untuk meratakan semua gain
> - **Restore EQ** untuk mengembalikan nilai sebelum reset"

### Visual: Overlay channel

**Narasi:**
> "Anda bisa menampilkan beberapa channel sekaligus di grafik dengan mengklik daftar CH di kanan grafik. Channel yang sedang dipilih selalu ditampilkan."

---

## BAB 6: CROSSOVER (12:00 - 15:00)

### Visual: Panel crossover

**Narasi:**
> "Panel crossover mengatur filter high-pass dan low-pass per channel:"

### Visual: Klik HPF/LPF Type

**Narasi:**
> - **HPF Type** — Pilih keluarga filter: Butterworth, Bessel, atau Linkwitz-Riley
> - **HPF Oct** — Pilih slope: 6, 12, 18, 24, 30, 36, 42, 48 dB/oct, atau OFF
> - **HPF Frequency** — Atur frekuensi cutoff
> - **LPF Type** — Sama seperti HPF
> - **LPF Oct** — Sama seperti HPF
> - **LPF Frequency** — Atur frekuensi cutoff"

### Visual: Contoh konfigurasi

**Narasi:**
> "Contoh: Untuk subwoofer, set HPF 80 Hz Linkwitz-Riley 24 dB/oct. Untuk tweeter, set LPF 5000 Hz Linkwitz-Riley 24 dB/oct."

---

## BAB 7: TIME ALIGNMENT (15:00 - 18:00)

### Visual: Panel delay

**Narasi:**
> "Time alignment mengatur delay per channel agar suara dari semua speaker tiba di telinga pendengar secara bersamaan."

### Visual: Klik unit delay

**Narasi:**
> "Anda bisa mengganti unit delay dengan mengklik Ms, Cm, atau Inch. Konversi menggunakan kecepatan suara 346 m/s."

### Visual: Diagram mobil, drag speaker

**Narasi:**
> "Buka panel posisi speaker dengan mengklik diagram mobil. Anda bisa:
> - Pilih mode **Mobil** atau **Ruangan**
> - Drag angka channel untuk mengatur posisi speaker
> - Drag titik **P** untuk mengatur posisi dengar
> - Atur lebar denah dengan **Jarak/Delay > Atur lebar**
> - Klik **Terapkan Delay** untuk mengirim nilai ke DSP"

---

## BAB 8: MIXER (18:00 - 20:00)

### Visual: Buka Mixer dari Option

**Narasi:**
> "Mixer mengatur routing input ke output. Buka dari Option > Mixer."

### Visual: Matriks mixer

**Narasi:**
> "Mixer menampilkan matriks 8 output (kolom) vs input (baris):
> - **AUX/Bluetooth** — 2 input (kiri-kanan)
> - **High level** — 4 input
> - Klik **ON/OFF** untuk enable/disable routing
> - Klik angka persen untuk edit gain
> - Drag slider untuk preview gain"

---

## BAB 9: PRESET DAN MEMORY (20:00 - 23:00)

### Visual: Buka Memory

**Narasi:**
> "Menu Memory menyediakan 8 slot preset lokal. Preset disimpan sebagai file `.scene` di folder kerja."

### Visual: Simpan preset

**Narasi:**
> "Untuk menyimpan preset:
> 1. Klik **Simpan** pada slot yang diinginkan
> 2. Masukkan nama preset (1-24 karakter)
> 3. Klik **Simpan preset** atau Enter"

### Visual: Load preset

**Narasi:**
> "Untuk memuat preset:
> 1. Klik **Load** pada baris preset yang diinginkan
> 2. Aplikasi akan menerapkan semua parameter yang sudah dipetakan
> 3. Output di-mute selama penerapan, lalu status mute dipulihkan"

---

## BAB 10: KONTROL TAMBAHAN (23:00 - 25:00)

### Visual: Panel kanan

**Narasi:**
> "Kontrol tambahan di panel kanan:"

### Visual: Highlight kontrol

**Narasi:**
> - **Link pairs** — Hubungkan pasangan CH1/2, 3/4, 5/6, 7/8
> - **Lock output** — Kunci perubahan level, phase, mute, delay
> - **Reset output** — Reset channel terpilih ke default
> - **Mute** — Ikon speaker untuk mute channel, ikon master untuk mute semua
> - **Input volume** — Level USB dan Bluetooth 0-100
> - **Noise Gate** — Tingkat 0-20 (0 = OFF)"

---

## BAB 11: CLI TOOL (25:00 - 27:00)

### Visual: Terminal

**Narasi:**
> "Untuk debugging dan eksplorasi protocol, tersedia CLI tool `zpsniff`:"

```bash
# Cari device
./build/zpsniff find

# Handshake
./build/zpsniff ping

# Baca parameter tertentu
./build/zpsniff get --id 139

# Snapshot semua parameter
./build/zpsniff snap -o snapshot.txt

# Bandingkan dua snapshot
./build/zpsniff diff before.txt after.txt

# Dump seluruh parameter image
./build/zpsniff dump -o dump.bin
```

---

## BAB 12: PENUTUP (27:00 - 28:00)

### Visual: Closing

**Narasi:**
> "Itulah tutorial lengkap aplikasi ZP 8.4 AMP DSP Configuration Tool. Aplikasi ini memungkinkan Anda mengkonfigurasi amplifier DSP Zevox ZP 8.4 AMP secara penuh dari Linux. Untuk informasi lebih lanjut, lihat dokumentasi di folder `docs/`. Terima kasih!"

---

## CATATAN PRODUKSI

- **Durasi total:** ~28 menit
- **Resolusi:** 1920x1080
- **Format:** MP4, H.264
- **Audio:** Narasi bahasa Indonesia, background music volume rendah
- **Subtitle:** Bahasa Indonesia + English
- **Tools recording:** OBS Studio + Xvfb (untuk demo tanpa hardware)
