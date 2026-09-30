# Perbaikan frequency response ZP 8.4 AMP

## Koreksi lanjutan: skala Q PEQ (30 September 2026)

Kesimpulan awal bahwa puncak sempit sudah benar belum lengkap. Penelusuran
`MainActivity.java:1731` menemukan faktor Q tampilan **19/6**. Pada
`chart/c.java:836`, alpha PEQ memakai `sin(omega)/(2*Q_display/(19/6))`.
`d/a.java` mengubah raw menjadi Q tampilan sebelum meneruskannya ke chart.
Adapter kita sebelumnya mengirim Q tampilan langsung ke RBJ sehingga filter
sekitar 3,1667 kali terlalu sempit. Sekarang PEQ memakai Q efektif `raw/100`
(contoh raw239 → Q2,39), sementara angka tampilan tetap sekitar7,57.
Shelf mempertahankan konvensi sebelumnya karena cabang vendor tidak memakai
pembagi ini. Tidak ada penulisan parameter hardware dalam koreksi ini.

Pengujian regresi membandingkan respons di luar center frequency terhadap
persamaan vendor secara independen; tes tepat di center saja tidak mendeteksi
kesalahan bandwidth. CTest 4/4 dan Make lulus; screenshot fixture diperbarui.
Grafik tetap satu respons gabungan. Log section PEQ menyatakan Q efektif.

## Temuan dan akar masalah

1. `src/dsp/biquad.c`: builder Linkwitz–Riley lama mengulang biquad Q=0,5 untuk setiap 12 dB/oct. Ini hanya menghasilkan LR12 yang benar; LR24 menjadi −12,04 dB dan LR48 menjadi −24,08 dB pada cutoff. LR harus berupa dua Butterworth identik berorde setengah orde total, dengan Q masing-masing pole yang benar. LR36 kini terdiri dari dua Butterworth orde 3, masing-masing satu section orde pertama dan satu biquad Q=1.
2. `src/gui/console.h`: grafik memakai mesin berbeda dari builder DSP, yaitu rumus magnitude crossover dan cascade PEQ. Rumus magnitude Butterworth/LR sebelumnya sendiri tidak menyebabkan ripple. Namun bypass EQ tidak diterapkan, semua tipe EQ dianggap peaking, dan filter yang tidak didukung ditampilkan sebagai respons datar. Grafik sekarang menggunakan satu cascade koefisien untuk semua komponennya.
3. Screenshot menampilkan sejumlah channel overlay dengan warna abu-abu yang sama. Kurva abu-abu dengan puncak frekuensi tinggi tidak dapat dianggap sebagai LPF channel CH3 yang terpilih. Overlay sekarang berwarna sesuai tombol channel, digambar sebelum channel terpilih. Mode debug mengisolasi channel terpilih.
4. `tests/zpmath_test.c` mengabadikan hasil LR yang salah dalam expected values. Ekspektasi diperbaiki dan pengujian independen ditambahkan di `tests/zpresponse_test.c`.

Tidak ditemukan kesalahan derajat/radian, tanda denominator, normalisasi a0 pada koefisien valid, atau spline pada renderer lama. Penjumlahan dB antarfilter sebelumnya juga secara matematis setara dengan perkalian magnitude; bukan penjumlahan magnitude linear. Karena itu, bukan semua poin dugaan merupakan bug.

## Implementasi

- `biquad.c/.h`: H(z) kompleks dengan z1=exp(−jω), z2=z1²; denominator 1+a1*z1+a2*z2. a0 dinormalisasi saat pembuatan koefisien. Respons cascade mengalikan H setiap section, kemudian mengambil 20*log10(max(abs(H),1e−12)). Parameter/frekuensi invalid menghasilkan NaN, bukan kurva datar.
- Butterworth: orde 1–8, slope 6–48 dB/oct; Q conjugate pole berbeda untuk masing-masing section, plus section orde pertama untuk orde ganjil.
- Linkwitz–Riley: slope 12/24/36/48 dB/oct; dua Butterworth identik. Cutoff semua orde sekitar −6,0206 dB.
- `response.c/.h`: model channel HPF × EQ1…EQ31 × LPF, bypass, mode terpisah, pencatatan koefisien. EQ aktif dengan gain 0 tetap dibuat dan dihitung. Filter yang bypass dilewati.
- Fs desktop 48.000 Hz, Nyquist 24.000 Hz. Grafik memakai 2048 titik logaritmik 20–20.000 Hz; garis lurus antarsampel, tanpa spline atau smoothing. Evaluasi di atas Nyquist ditolak.
- `console.h`: adapter dari snapshot yang sudah dibaca, rendering dan log. `zp84gui.c`: tombol keyboard mode/log. Tidak ada perubahan struktur utama UI.
- Bessel, tipe EQ tidak dikenal, dan slope LR nonstandar 6/18/30/42 dB/oct tidak ditebak. Mode terkait menampilkan status tidak didukung dan tidak menggambar cascade parsial.

## Kompatibilitas data

Sumber mapping lokal: `work/apk_src/sources/com/tigerapp/jheqchart_application/`.

- `f/a.java`: alamat EQ enable 66+channel, type 147+136*channel+4*band (indeks nol).
- `service/BTService.java`, metode `s`: alamat vendor dikurangi satu untuk indeks parameter USB.
- `d/c.java` / `d/i.java`: EQ enable 0 menunjukkan bypass; nonzero aktif.
- `chart/c.java`: type 5 high shelf, 6 low shelf; type 7 peaking juga digunakan oleh reset di `d/c.java`. Q shelf dibatasi maksimum 2 sesuai model vendor.
- Adapter menggunakan USB ID 65+channel untuk enable dan 146+136*channel+4*band untuk type. Decode frequency, gain `(raw−500)/10`, Q tampilan `raw*19/600` tetap dipertahankan; Q efektif PEQ untuk respons adalah `raw/100`.

Ini adalah **model dari parameter**, bukan pembacaan koefisien hardware atau hasil pengukuran audio. Jalur USB menyediakan nilai parameter uint16; tidak ada koefisien fixed-point hardware yang dikonversi menjadi floating point oleh grafik. Format fixed-point, protokol, packet, memory/preset dan parameter snapshot tidak diubah. Bypass per-band hardware tidak diasumsikan tanpa mapping; model mendukung bypass eksplisit, adapter menerapkan flag global yang telah ditelusuri.

## Menggunakan debug

Di tampilan DSP, tanpa dialog terbuka:

- **F2** berputar antara HPF+EQ+LPF, HPF only, LPF only, EQ only, HPF+LPF.
- Mode terpisah hanya menggambar channel terpilih. Mode penuh mempertahankan pilihan overlay.
- **F3** menulis `/tmp/zp84-response-XXXXXX`; lokasi muncul di status bawah. File berisi kelima mode, type/frequency/gain/Q/slope per section, b0/b1/b2/a0=1/a1/a2, dan 2048 pasangan frequency/response dB per mode.
- Q=0 pada log section orde pertama berarti Q tidak berlaku, bukan Q biquad yang invalid.

## Verifikasi

Dilakukan tanpa koneksi atau penulisan ke DSP:

- `make test gui`: lulus; 50 pemeriksaan matematika lama yang diperbarui, tes protokol, GUI data, serta suite respons baru.
- CMake dengan `-DZP84_GUI=ON`: compile berhasil; CTest **4/4 lulus**.
- AddressSanitizer/UndefinedBehaviorSanitizer suite respons: lulus dengan leak detection dimatikan karena LeakSanitizer tidak bekerja di lingkungan ptrace ini.
- Suite baru: 2048 sampel, monotonic HPF/LPF, cascade penuh sama dengan jumlah dB komponen, cutoff BW/LR, semua orde BW/LR terhadap rumus BLT independen, PEQ dengan DFT impulse sebagai pembanding kompleks, EQ gain nol, bypass, unknown filter, Nyquist, dan peak EQ asli pada 20 Hz/20 kHz tetap dipertahankan.
- Adapter snapshot: CH3, HPF BW24, LPF LR36, Q239 → 7,5683, 31 EQ, dan flag bypass.
- Xvfb render memakai fixture terpisah dari binary produksi: **56.645 painted samples**, hasil diperiksa secara visual. Tidak menggunakan `--read-dsp`.

Fixture sesuai permintaan: HPF100 BW24, LPF6000 LR36; EQ100/125/160/200 Hz +1,9/+3,3/+3/+1,9 dB, Q tampilan7,57 (Q efektif≈2,3905). LPF pada cutoff tepat −6,0206 dB; setelah cutoff respons penuh terus turun. Pada sekitar10 kHz respons penuh sekitar−32 dB, dan pada20.000 Hz sekitar−114,57 dB. Lengkungan lebih lebar sekitar100–200 Hz berasal dari EQ yang diaktifkan setelah koreksi skala Q. Batas plot tetap ±20 dB, sehingga bagian di bawah−20 dB tidak terlihat.

Artefak:

- `build/frequency-response.csv`: 2048 titik untuk lima mode (dibuat ulang dengan `build/zpresponse_test build/frequency-response.csv`).
- `build/frequency-response-preview.png`: screenshot fixture, bukan pembacaan perangkat.
- `artifacts/frequency-response.patch`: patch lengkap terhadap keadaan workspace sebelum perbaikan respons ini; tidak mencakup perubahan UI/memory/link-pairs terdahulu.

## Referensi matematis

[W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/) mendokumentasikan koefisien RBJ, normalisasi dan konvensi transfer function. [Linkwitz Lab — Filters](https://www.linkwitzlab.com/filters.htm) mendokumentasikan crossover Linkwitz–Riley. Implementasi hardware persis tetap memerlukan koefisien vendor atau pengukuran, sehingga laporan ini tidak menyatakan verifikasi akustik perangkat.
