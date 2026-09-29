# ZP 8.4 AMP — data hasil ekstraksi untuk port ke Linux

Ringkasan apa yang dikerjakan dan apa yang **tidak** bisa di-sniff.

---

## 1. Isi RAR aslinya

| File | Ukuran | Isi sebenarnya |
|---|---|---|
| `ZP 8.4 AMP_EN.exe` | 9.8 MB | **Bukan aplikasi.** Self-extracting 7-Zip berisi **25 sprite PNG** untuk skin GUI. Made with Adobe Photoshop CC / ImageReady 2013. Nol baris logika program. |
| `ZP 8.4AMP-V1517-release.apk` | 1.5 MB | Aplikasi Android-nya: `com.tigerapp.jheqchart_application` v1.5.17 |

> ⚠️ Nama folder menyebut **8.4**, tapi APK-nya **1.5.17**. Ini bundle repack pihak ketiga,
> bukan paket resmi dari vendor. SHA256:
> - EXE `8AC6B6ED6787E5A11B85FF8AC7F8AE4AE44CFCABE2806B6AE1534061B14CF67E`
> - APK `407777B82835BCF124DB346008D75E5CDB10D0213221040D88CB8BCFF28DD450`

## 2. Aplikasinya apa

App tuning **amplifier / DSP audio mobil 8 channel**. Dari string `resources.arsc`:

```
FL-Full  FL-Midrange  FL-Tweeter  FL-Woofer     crossover per driver
FR-Full  FR-Midrange  FR-Tweeter  FR-Woofer
CH1 LINK CH2 ... CH7 LINK CH8     8 kanal, mode link
COAX, Delay:CM, Delay:MS, Bessel, Bypass
Bluetooth audio connected! / Device connected!
Change password ok! / fail!
```

## 3. Kenapa "sniff" tidak bisa dipakai di sini

`classes.dex` (1.1 MB) sudah discan menyeluruh:

- **Nol URL** `http://`, nol `.php`, nol endpoint API.
- **Nol kelas USB** — tidak ada `android.hardware.usb` / `support.v4.hardware.usb`.

Jadi tidak ada traffic web untuk ditiru, dan app ini **tidak memakai USB Host API**.
Yang dipakai adalah Bluetooth:

```
0000ae00-0000-1000-8000-00805f9b34fb   service GATT (custom)
0000ae01-0000-1000-8000-00805f9b34fb   characteristic  write
0000ae02-0000-1000-8000-00805f9b34fb   characteristic  notify
00002902-0000-1000-8000-00805f9b34fb   SPP (Bluetooth classic serial)
```

`ae00/ae01/ae02` adalah profil modul **serial transparan BLE** yang umum dipakai DSP audio
Cina — isinya tembol data mentah, tanpa framing rumit. Ini kabar baik untuk port ke Linux.

> Kalau yang kamu maksud "via USB" adalah kabel USB yanguxe menyambung ke amp, kemungkinan
> amp-nya pakai **USB-serial adapter**, sementara HP tetap talks lewat BLE. Perlu dipastikan
> dulu: pasangkan HP ke amp dengan Bluetooth, lalu cek di `bluetoothctl` apakah muncul GATT
> `ae00`. Kalau iya, berarti "USB" itu cuma untuk power/data ke modul BLE-nya.

## 4. Yang tersedia di folder ini

```
zp84-data/
├── build-manifest.ps1   # regenerate assets.json
├── assets.json          # manifest 25 asset + palet + content bbox
├── theme.css            # palet warna siap pakai untuk UI Linux
├── contact_sheet.png    # ringkasan visual semua sprite
└── images/              # 25 PNG hasil ekstraksi
```

### assets.json

Setiap asset punya:

```jsonc
{
  "id": "slider_h_track",          // nama semantik
  "file": "images/img04.png",
  "kind": "control",               // button|control|icon|indicator|overlay
  "desc": "Track slider horizontal: ...",
  "stretch": "9patch-x",           // hanya pada asset nine-patch
  "width": 360,
  "height": 26,
  "content": { "x": 0, "y": 0, "width": 360, "height": 26 },  // bbox non-transparan
  "palette": [ { "hex": "#FD0E02", "pixels": 636 }, ... ]     // 5 warna dominan
}
```

`content` penting karena sebagian sprite punya padding kosong di dalam gambar
(mis. `icon_power_states` 136px tapi isinya cuma 117px).

### Catatan nine-patch

PNG asli **tidak punya chunk `9p`**, jadi metadata nine-patch tidak ikut terekstrak.
Kalau dipakai sebagai nine-patch di Linux, definisikan manual. Untuk
`btn_state_ninepatch` (192×29)zbidez warpannya kira-kira:

```
┌────────────┬─────────┬────────┐
│ 16px fixed │ stretch │ 16px   │
├────────────┼─────────┼────────┤
│ 11px fixed │  1px    │ 11px   │   <- tinggi tengah hanya 1px
├────────────┼─────────┼────────┤
│ 16px fixed │ stretch │ 16px   │
└────────────┴─────────┴────────┘
```

Tepatnya harus dicek visual; `contact_sheet.png` ada buat itu.

## 5. Daftar 25 asset

| # | id | Ukuran | Fungsi |
|---|---|---|---|
| 01 | `btn_state_ninepatch` | 192×29 | tombol: abu / hijau / abu |
| 02 | `toggle_knob` | 56×14 | knob ON (kuning) / OFF |
| 03 | `meter_scale_gain` | 80×233 | skala gain 60..10 dB + OFF |
| 04 | `slider_h_track` | 360×26 | track slider horizontal |
| 05 | `dropdown_blue` | 140×15 | dropdown gelap/biru |
| 06 | `led_row_4` | 64×16 | 4 LED status |
| 07 | `link_bar_dark` | 180×20 | bar segment gelap |
| 08 | `link_bar_orange` | 184×24 | bar gelap + isi oranye |
| 09 | `fader_v_swatch` | 30×159 | fader vertikal + 4 swatch kanal |
| 10 | `fader_v_link3` | 36×209 | 2 fader + 3 swatch kanal |
| 11 | `fader_v_off` | 36×212 | 2 fader, semua kanal mati |
| 12 | `icon_speaker_mute` | 148×18 | ikon speaker / mute |
| 13 | `slider_fill_orange` | 148×18 | isian slider oranye |
| 14 | `bar_status_gr` | 160×22 | bar hijau/merah level |
| 15 | `meter_scale_mono` | 86×198 | skala meter mono |
| 16 | `fader_v_ticks` | 58×206 | fader + tick mark |
| 17 | `meter_scale_cut` | 80×205 | skala cut 0..-40 dB |
| 18 | `icon_power_states` | 136×17 | tombol power 4 state |
| 19 | `eq_grid_lines` | 145×56 | grid EQ |
| 20 | `freq_response_plot` | 340×33 | kurva respon + ikon zoom |
| 21 | `dropdown_error` | 140×22 | dropdown state error |
| 22 | `checkbox_states` | 288×24 | checkbox unchecked/checked/disabled |
| 23 | `dropdown_dark` | 146×18 | dropdown gelap, panah oranye |
| 24 | `dropdown_blue_3seg` | 200×21 | dropdown biru 3 segmen |
| 25 | `bar_dark_4seg` | 196×21 | bar gelap 4 segmen |

## 6. Palet inti

```
#343533  body gelap / track          #929297  teks skala abu
#282828  surface                     #C9C9C9  teks terang
#FFBC1F  aksen kuning (knob ON)      #EC1E1E  merah (error/level)
#1E781E  hijau (aktif)               #378BCA  biru aksen
#253863  biru tua dropdown            #46728F  biru dropdown
#4F595E  abu dropdown                #AQUA
```

`theme.css` berisi versi lengkap dengan alpha-nya, siap di-paste ke app GTK/Qt/Tauri.

## 7. Catatan lisensi

Sprite ini karya vendor (dibuat 2013 di Photoshop CC) dan diambil dari bundle repack
yang sumbernya tidak terverifikasi. Pakai untuk kebutuhan pribadi boleh; mendistribusikan
ulang sprite atau kode ke publik itu masalah lisensi terpisah dari topik teknisnya.
