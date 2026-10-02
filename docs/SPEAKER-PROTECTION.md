# Proteksi speaker

Tombol **Proteksi speaker** di toolbar membuka profil batas per channel.
Ini adalah pengaman perubahan parameter di aplikasi desktop, bukan limiter
tegangan/daya dalam firmware DSP dan bukan jaminan speaker tidak rusak.

## Penggunaan

1. Tentukan batas dari spesifikasi model speaker dan tuning box. Merek
   dan keterangan ported saja belum cukup menentukan cutoff subsonic.
2. Hubungkan perangkat, Baca DSP, lalu atur crossover dan level sesuai batas
   yang sudah diverifikasi. Jangan memakai setelan sekarang sebagai acuan
   aman tanpa memeriksanya.
3. Buka Proteksi speaker, pilih channel, konfirmasi spesifikasi, lalu pilih
   **Kunci sebagai tweeter** atau **Kunci sebagai subwoofer**.
4. Ulangi untuk setiap channel yang memerlukan proteksi, termasuk pasangan
   stereo. Mengunci profil tidak menulis parameter DSP dan tidak otomatis
   menyalin profil ke pasangan.

Profil menyimpan kondisi saat dikunci sebagai batas. Untuk mengganti batas,
lepaskan kunci dengan konfirmasi, atur nilai yang disetujui, lalu kunci lagi.
Tidak ada preset frekuensi universal atau profil Audax otomatis yang dibuat.

## Aturan

- Tweeter: HPF wajib aktif; cutoff tidak boleh turun; slope tidak boleh lebih
  landai. Jenis filter tidak boleh berubah.
- Subwoofer: aturan HPF yang sama (subsonic), ditambah LPF wajib aktif;
  cutoff LPF tidak boleh naik dan slope tidak boleh lebih landai. HPF harus
  lebih rendah daripada LPF.
- Level master dan output tidak boleh melewati nilai tersimpan. Nilai level
  adalah skala perangkat, bukan watt atau pengukuran SPL.
- Gain setiap band EQ tidak boleh melebihi gain yang dikunci. Frekuensi, Q,
  jenis dan status bypass EQ dikunci agar tidak mengubah respons di luar
  profil yang telah diverifikasi.
- Input, mixer/routing dan gain USB/BT dikunci selama ada profil aktif karena
  dapat mengubah level sinyal sebelum output.
- Pemeriksaan berlaku untuk edit angka, fader, reset/restore EQ, preset, dan
  Link Pairs. Kedua tujuan link diperiksa sebelum write pertama. Preset
  diperiksa sebelum proses mute/write dimulai.
- Mute tetap dapat dikirim. Unmute ditolak apabila snapshot channel melanggar
  batas. Snapshot baru di luar batas ditandai; aplikasi tidak melakukan
  penulisan atau mute otomatis saat pembacaan.
- Jika setelan sudah di luar batas, perbaiki dengan prosedur terkontrol dan
  profil yang benar; aplikasi dapat menolak edit parsial yang masih menyisakan
  pelanggaran. Jangan menganggap status USB connected sebagai status aman.

## Penyimpanan dan batas cakupan

Profil tersimpan atomik di `$HOME/.zp84-protection.conf`, tidak bergantung
pada direktori peluncuran. Kegagalan menyimpan membatalkan perubahan profil.
File rusak memblokir write (mute tetap tersedia); pulihkan file profil yang
valid sebelum melanjutkan. Profil berlaku pada aplikasi desktop ini dan
perangkat yang dihubungkan; bukan identifikasi speaker/perangkat otomatis.

Aplikasi vendor, aplikasi web/CLI lain, perubahan langsung perangkat dan
pengaturan saat aplikasi desktop tidak berjalan tidak dilindungi guard ini.
Tidak ada pengukuran clipping, suhu, excursion, daya atau limiter hardware
baru. Setelan fisik gain amplifier juga tetap harus sesuai speaker.

## Verifikasi tanpa hardware

Make: suite respons, matematika, protokol dan GUI data lulus. CMake/CTest:
4/4 lulus. Regresi baru memeriksa HPF OFF, slope terlalu landai, cutoff,
LPF subwoofer, gain EQ/master/output, input volume, linked write tanpa efek
parsial saat ditolak, preset tanpa write saat ditolak, save/load profil,
file rusak serta mute/unmute. Dialog diperiksa dengan Xvfb menggunakan data
fixture, tanpa koneksi DSP.
