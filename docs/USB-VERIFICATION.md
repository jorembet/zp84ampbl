# Verifikasi USB Zevox ZP 8.4 AMP

Pengujian langsung pada 29 September 2026 (Asia/Makassar).

- Node perangkat: `/dev/hidraw2`, symlink `/dev/zp84amp`.
- Akses read/write berhasil setelah grup node diubah menjadi `plugdev`.
- Handshake `0x06` untuk ID `0x0000` membalas `00 00 23 20`.
- Pembacaan 1.934 ID (`0x0000`–`0x078d`) berhasil dengan batch 8.
- Snapshot lengkap: `zevox-usb-baseline.txt`, 1.934 baris data dan satu header.
- Batch 64 gagal pada batch pertama. Penyebab belum dipastikan; jangan
  menganggap pengujian ini membuktikan batas firmware tertentu.

## Mengulang pembacaan

```sh
make
./build/zpsniff find
./build/zpsniff ping
./build/zpsniff snap -o snapshot.txt
```

`snap` sekarang memakai batch 8 secara default, memeriksa panjang balasan
dan kecocokan ID, serta menyimpan semua nilai termasuk nol. File tujuan
diganti hanya setelah seluruh pembacaan dan penulisan file berhasil.
Kegagalan menghasilkan exit status 1 dan mempertahankan file tujuan lama.

Pengujian kegagalan dengan `--batch 64` menghasilkan exit status 1 dan
snapshot lengkap sebelumnya tetap memiliki 1.935 baris. Build dan 105
pemeriksaan frame/matematika yang tersedia lulus.

Format tiga kolom lama (`id type value`) dipertahankan agar kompatibel
dengan alat diff. Dua kolom terakhir berisi nilai mentah 16 bit yang sama;
ini bukan bukti bahwa perangkat mengirim type dan value terpisah.

## Menampilkan data di aplikasi

Jalankan `./build/zp84gui --read-dsp` untuk langsung membaca USB dan membuka
tab **Data DSP (USB)**. Dari aplikasi yang sudah terbuka, klik **Baca DSP**.
Tabel menampilkan seluruh 1.934 ID, HEX dan desimal unsigned, termasuk nol.
Gunakan tombol halaman, roda mouse, atau PgUp/PgDn; tekan R untuk membaca ulang.
Waktu snapshot ditampilkan dan data terakhir dipertahankan jika pembacaan
berikutnya gagal. **Editor lokal** tetap merupakan simulasi, bukan pengaturan
yang dipetakan dari register amplifier.

Pembacaan dilakukan delapan ID per iterasi event loop. Respons harus lengkap
dan seluruh ID harus cocok sebelum snapshot baru ditampilkan. Tes GUI tanpa
hardware mencakup snapshot parsial, lengkap, salah ID, respons pendek, timeout,
dan batas halaman. Uji GUI langsung pada hardware berhasil membaca 1.934 ID.

## Batas interpretasi

Snapshot berisi nilai register mentah, bukan preset yang sudah dapat
dipulihkan. Arti ID, satuan, dan prosedur tulis belum diverifikasi.
Tidak ada pengaturan amplifier yang sengaja diubah pada pengujian ini;
perintah yang digunakan adalah query `0x06`.
