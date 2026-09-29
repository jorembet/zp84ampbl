<#
  build-manifest.ps1
  Menghasilkan assets.json dari sekumpulan sprite PNG hasil ekstraksi "ZP 8.4 AMP_EN.exe".

  Untuk setiap gambar dihitung:
    - dimensions
    - content bbox (bounding box piksel non-transparan)  -> berguna untuk sprite sheet
    - palette        (5 warna dominan)                  -> berguna untuk tema Linux
    - opaque / fullyTransparent flags

  Pakai:  pwsh -File build-manifest.ps1
#>
param(
    [string]$ImageDir = "$PSScriptRoot\images",
    [string]$OutFile  = "$PSScriptRoot\assets.json"
)

Add-Type -AssemblyName System.Drawing

# Nama semantik per file (urutan = img01..imgNN). Ditentukan dari inspeksi visual.
$Roles = @{
    'img01' = @{ name='btn_state_ninepatch'; kind='button';  desc='9-patch tombol: [normal abu-abu][aktif hijau] + [abu-abu]'; stretch='9patch-x' }
    'img02' = @{ name='toggle_knob';         kind='control'; desc='Knob toggle: [ON kuning][OFF gelap] + knob gelap lepas' }
    'img03' = @{ name='meter_scale_gain';    kind='overlay'; desc='Skala meter gain 60..10 dB + OFF, dengan tick mark kecil' }
    'img04' = @{ name='slider_h_track';      kind='control'; desc='Track slider horizontal: [kosong abu][isi kuning] + [abu]' }
    'img05' = @{ name='dropdown_blue';       kind='control'; desc='Dropdown 2 segmen gelap/biru dengan panah' }
    'img06' = @{ name='led_row_4';           kind='indicator'; desc='Baris 4 LED status (abu, merah, merah, abu)' }
    'img07' = @{ name='link_bar_dark';       kind='control'; desc='Bar segment 2 bagian warna gelap solid' }
    'img08' = @{ name='link_bar_orange';     kind='control'; desc='Bar segment gelap + isi oranye' }
    'img09' = @{ name='fader_v_swatch';      kind='control'; desc='Fader vertikal + 4 swatch warna link kanal (oranye/hijau/hitam)' }
    'img10' = @{ name='fader_v_link3';       kind='control'; desc='2 track fader vertikal + 3 swatch kanal (oranye/hijau/hitam)' }
    'img11' = @{ name='fader_v_off';         kind='control'; desc='2 track fader vertikal + 3 swatch kanal (semua mati)' }
    'img12' = @{ name='icon_speaker_mute';   kind='icon';    desc='Ikon speaker: [bunyi][mute X][bunyi]' }
    'img13' = @{ name='slider_fill_orange';  kind='control'; desc='Isian slider abu/oranye + abu' }
    'img14' = @{ name='bar_status_gr';       kind='control'; desc='Bar status hijau/merah (indikator level)' }
    'img15' = @{ name='meter_scale_mono';    kind='overlay'; desc='Skala meter 60..10 dB mono' }
    'img16' = @{ name='fader_v_ticks';       kind='overlay'; desc='Fader vertikal dengan tick mark + swatch oranye/hijau' }
    'img17' = @{ name='meter_scale_cut';     kind='overlay'; desc='Skala 0,-10,-20,-30,-40 dB + OFF (cut/attenuation)' }
    'img18' = @{ name='icon_power_states';   kind='icon';    desc='Tombol power 4 state (abu, biru, merah, merah)' }
    'img19' = @{ name='eq_grid_lines';       kind='overlay'; desc='Garis grid EQ + 2 blok transparan' }
    'img20' = @{ name='freq_response_plot';  kind='overlay'; desc='Kurva respon frekuensi + ikon zoom (4 varian)' }
    'img21' = @{ name='dropdown_error';      kind='control'; desc='Dropdown biru tua + merah tua (state error) dengan panah' }
    'img22' = @{ name='checkbox_states';     kind='control'; desc='Checkbox: [unchecked][checked][disabled]' }
    'img23' = @{ name='dropdown_dark';       kind='control'; desc='Dropdown gelap dengan panah oranye' }
    'img24' = @{ name='dropdown_blue_3seg';  kind='control'; desc='Dropdown biru 3 segmen dengan panah' }
    'img25' = @{ name='bar_dark_4seg';       kind='control'; desc='Bar gelap 4 segmen' }
}

function Get-Palette([System.Drawing.Bitmap]$bmp, [int]$top = 5) {
    $counts = @{}
    for ($y = 0; $y -lt $bmp.Height; $y++) {
        for ($x = 0; $x -lt $bmp.Width; $x++) {
            $c = $bmp.GetPixel($x, $y)
            if ($c.A -lt 8) { continue }          # aba-abakan transparan penuh
            $k = '#{0:X2}{1:X2}{2:X2}' -f $c.R, $c.G, $c.B
            $counts[$k] = [int]$counts[$k] + 1
        }
    }
    $counts.GetEnumerator() |
        Sort-Object Value -Descending |
        Select-Object -First $top |
        ForEach-Object { [ordered]@{ hex = $_.Key; pixels = $_.Value } }
}

function Get-ContentBBox([System.Drawing.Bitmap]$bmp) {
    $minX = $bmp.Width; $minY = $bmp.Height; $maxX = -1; $maxY = -1
    for ($y = 0; $y -lt $bmp.Height; $y++) {
        for ($x = 0; $x -lt $bmp.Width; $x++) {
            if ($bmp.GetPixel($x, $y).A -gt 8) {
                if ($x -lt $minX) { $minX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    if ($maxX -lt 0) {
        return [ordered]@{ x = 0; y = 0; width = 0; height = 0; empty = $true }
    }
    [ordered]@{
        x = $minX; y = $minY
        width  = $maxX - $minX + 1
        height = $maxY - $minY + 1
        empty = $false
    }
}

$assets = @()
Get-ChildItem (Join-Path $ImageDir '*.png') | Sort-Object Name | ForEach-Object {
    $key   = [System.IO.Path]::GetFileNameWithoutExtension($_.Name)
    $meta  = if ($Roles.ContainsKey($key)) { $Roles[$key] }
              else { @{ name = $key; kind = 'unknown'; desc = ''; stretch = $null } }

    $bmp = New-Object System.Drawing.Bitmap($_.FullName)
    try {
        $bbox = Get-ContentBBox $bmp
        $assets += [ordered]@{
            id       = $meta.name
            file     = "images/$($_.Name)"
            kind     = $meta.kind
            desc     = $meta.desc
            stretch  = $meta.stretch
            width    = $bmp.Width
            height   = $bmp.Height
            content  = $bbox
            palette  = @(Get-Palette $bmp 5)
        }
        Write-Host ("{0,-26} {1,4}x{2,-4} bbox={3},{4} {5}x{6}" -f `
            $meta.name, $bmp.Width, $bmp.Height,
            $bbox.x, $bbox.y, $bbox.width, $bbox.height)
    }
    finally { $bmp.Dispose() }
}

$doc = [ordered]@{
    schema   = 'zp84-amp-skin/1'
    name     = 'ZP 8.4 AMP skin'
    source   = 'Ekstraksi dari ZP 8.4 AMP_EN.exe (self-extracting 7-Zip, 25 sprite PNG)'
    origin   = 'Dibuat dengan Adobe Photoshop CC / ImageReady, 2013'
    channels = 8
    notes    = @(
        'Tidak ada chunk 9p pada PNG asli, jadi tidak ada metadata nine-patch bawaan.',
        'Asset nine-patch harus didefinisikan manual di aplikasi Linux (lihat README.md).',
        'Semua warna dalam PNG RGBA; dipakai alpha untuk masking.'
    )
    assets   = $assets
}

$doc | ConvertTo-Json -Depth 8 | Set-Content -Path $OutFile -Encoding UTF8
Write-Host ""
Write-Host "-> $OutFile  ($($assets.Count) asset)"
