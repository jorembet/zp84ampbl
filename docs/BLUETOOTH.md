# Koneksi Bluetooth Mango3.0

Toolbar menyediakan tombol **Koneksi: USB** / **BLE Mango3.0**. Disconnect
terlebih dahulu jika sedang terhubung, pilih BLE, klik Connect, lalu Baca DSP.
Pemindaian berlangsung sekitar 6 detik; resolusi layanan dapat menambah waktu.
Pemindaian, koneksi dan verifikasi awal berjalan di worker; GUI tetap responsif.
Tombol Connect berubah menjadi **Batal koneksi**, dengan status connecting.
Batas waktu startup 25 detik. Baca DSP/ganti transport ditahan saat connecting.
Pilihan awal tetap USB.

Komputer memerlukan Bluetooth/BlueZ yang aktif, Python 3 dan paket `bleak`.
Tutup koneksi aplikasi vendor di HP apabila Mango3.0 tidak muncul atau tidak
bisa terhubung. Tidak ada proses pairing/trust otomatis. Jika lebih dari satu
perangkat bernama Mango3.0 ditemukan, aplikasi menolak memilih secara acak.

## Implementasi

- `src/ble/zp84-ble.py`: worker BLE lewat BlueZ, pemilihan nama Mango 3.0 atau Mango3.0,
  pemeriksaan karakteristik vendor AE01 write-without-response dan AE02 notify.
- `src/gui/bluetooth.h`: child process dengan socket IPC privat; tidak memakai
  shell. Worker dipasang di sebelah binary sebagai `zp84-ble.py`.
- `zp_conn` menyediakan hook transport. USB tetap menggunakan implementasi
  HID sebelumnya. GUI BLE menggunakan `zp_xfer` untuk query/write parameter;
  API HID tingkat rendah tidak dialihkan menjadi operasi Bluetooth.
- Bluetooth memakai frame `80 length command payload CRC_hi CRC_lo` sesuai
  `BTService.f` dan CRC16 Modbus pada kode APK, tanpa pembungkus HID AE1EC8.
- Query 0x06 dibagi menjadi kelompok maksimal empat ID agar request muat
  dalam 20 byte. Notifikasi parsial digabung, CRC serta ID balasan diverifikasi.
- Hanya query parameter 0x06 dan write parameter 0x03 diteruskan. Write tetap
  melalui acknowledgement dan pembacaan ulang independen pada `zp_id_write`.
  Tidak ada retry otomatis pada write yang timeout.
- Query baca yang kehilangan notifikasi dicoba ulang. Jika GATT putus di tengah
  pembacaan, worker mencoba menyambungkan ulang sekali dan mengulang query baca
  yang sama; write tetap tidak diulang karena hasilnya bisa ambigu.
- Proteksi speaker, preset, Link Pairs, serta lock desktop/web tetap berlaku
  karena semua kontrol memakai fungsi pengiriman yang sama.
- Disconnect/error menutup worker dan transport. Pilih Disconnect lalu
  Connect untuk mencoba ulang setelah kegagalan.

## Status verifikasi

Perangkat DA:59:91:0B:D0:CE bernama Mango3.0 pernah berhasil dihubungkan untuk
service discovery. Advertisement AF00 ternyata diikuti service GATT AE00,
dengan AE01 dan AE02 sesuai vendor. Tidak ada parameter yang ditulis saat itu.

Setelah integrasi, dua percobaan pembacaan nyata belum bisa berjalan karena
perangkat tidak ditemukan selama scan. Jadi pembacaan/pengubahan parameter
BLE pada hardware **belum terverifikasi**, meskipun discovery layanan cocok.

Make dan CMake berhasil. CTest 5/5 lulus, termasuk tes BLE tanpa perangkat:
CRC known-vector, notifikasi terfragmentasi, frame rusak, penggabungan frame,
pembacaan bertahap, balasan write/readback, penolakan perintah di luar cakupan,
dan penolakan perangkat dengan nama duplikat. Tes BLE membutuhkan IPC event
loop lokal; jalankan di luar sandbox yang memblokir wakeup thread Python.

`make gui` menyalin helper ke `build/`. Build CMake menyalin helper ke direktori
build. Installer `scripts2/install-app.sh` turut memasang helper di ~/.local/bin.

Pencarian menerima kedua penulisan nama: `Mango 3.0` dan `Mango3.0`.

## Perbaikan timeout saat perangkat sudah terhubung

Sebelum scan, worker membaca daftar Device1 dari BlueZ melalui ObjectManager.
Perangkat yang sudah dikenal bernama Mango3.0/Mango 3.0 digunakan langsung
dengan object path BlueZ, sehingga tidak perlu mengiklankan dirinya lagi.
Scan hanya dilakukan jika belum ada kandidat; kandidat ganda tetap ditolak.
GUI membedakan kegagalan penemuan, sambungan GATT, layanan, notifikasi,
dan query DSP. Tes regresi memastikan perangkat tersimpan tidak memicu scan.
