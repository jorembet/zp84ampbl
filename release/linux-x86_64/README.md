# ZP 8.4 AMP DSP Editor — Linux x86_64

Paket ini berisi `zp84gui` (aplikasi desktop), `zp84web` (antarmuka web lokal),
`zpsniff` (alat protokol), worker Bluetooth `zp84-ble.py`, dan aturan udev.

## Menjalankan

```sh
./zp84gui
./zp84web
```

Jalankan `zp84gui` dari direktori ini atau pastikan `zp84-ble.py` tetap di
sebelah binary GUI. GUI memerlukan X11 dan akses ke perangkat USB hidraw.
`zp84web` membuka antarmuka di `http://127.0.0.1:8085`.

Untuk memberi akses USB, salin aturan sebagai root lalu muat ulang udev:

```sh
sudo install -m 0644 99-zp84.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

## Bluetooth

Bluetooth memakai BlueZ, Python 3, dan paket `bleak` serta `dbus-fast`.
Worker harus tetap berada di sebelah `zp84gui`. Dukungan BLE mengikuti protokol
perangkat, tetapi jalur koneksi dan transfer Bluetooth belum dikonfirmasi
langsung pada hardware; USB merupakan jalur yang sudah diverifikasi.

## Verifikasi paket

Dari direktori utama repository:

```sh
sha256sum -c release/SHA256SUMS
```
