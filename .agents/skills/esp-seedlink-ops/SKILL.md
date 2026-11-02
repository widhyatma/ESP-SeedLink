---
name: esp-seedlink-ops
description: >-
  Panduan operasional dan pengujian sistem ESP-SeedLink: kompilasi firmware PlatformIO,
  pemantauan Serial Monitor, eksekusi PC Mock Server, pengujian stream ObsPy terminal sparkline,
  forwarding ke Central RingServer DataLink, dan verifikasi kepatuhan biner MiniSEED v2.
---

# ESP-SeedLink Operational & Testing Guide

Gunakan panduan ini untuk mengoperasikan, menguji, memantau, dan mengembangkan firmware ESP-SeedLink beserta rangkaian perkakas pendukungnya.

---

## 1. Verifikasi & Kompilasi Firmware ESP32

Proyek ini dibangun menggunakan PlatformIO dengan kerangka kerja Arduino di atas mikrokontroler ESP32.

```powershell
# Kompilasi firmware
pio run

# Unggah firmware ke perangkat ESP32 fisik (pastikan kabel USB terpasang)
pio run -t upload

# Buka Serial Monitor (kecepatan baud 115200)
pio device monitor -b 115200
```

### Kontrol Serial Interaktif
Saat Serial Monitor aktif:
- Tekan `q` atau `Q` lalu Enter: **Memicu simulasi gempa bumi triaksial** (fase gelombang primer P-wave, gelombang geser S-wave, dan peluruhan Coda).
- Tekan `s` atau `S` lalu Enter: **Menampilkan status stasiun**, uptime, mode jaringan, IP address, jumlah paket TX, dan status streaming klien.

---

## 2. Pengujian Tanpa Hardware (PC Mock Server)

Jika tidak ada perangkat keras ESP32 fisik di dekat Anda, jalankan simulator server mandiri pada PC:

```powershell
# Terminal 1: Jalankan server mock (port 18000)
python tools/seedlink_mock_server.py

# Terminal 2: Sambungkan test client
python tools/seedlink_test_client.py --host 127.0.0.1 --port 18000
```

---

## 3. Pengujian Terhadap Perangkat ESP32 Riil

Pastikan laptop Anda berada dalam jaringan yang sama dengan ESP32:

### Skenario A: Terhubung via Router Wi-Fi (Mode STA)
Periksa IP lokal ESP32 di Serial Monitor (misal: `192.168.1.50`), lalu jalankan:
```powershell
python tools/seedlink_test_client.py --host 192.168.1.50 --port 18000
```

### Skenario B: Terhubung via Mode Darurat (SoftAP)
Jika tidak ada router Wi-Fi di lapangan, ESP32 otomatis mengaktifkan Access Point:
- **SSID**: `ESP32-SEISMOMETER`
- **Password**: `seismo1234`
- Sambungkan Wi-Fi laptop ke SSID tersebut, lalu pantau via IP default `192.168.4.1`:
```powershell
python tools/seedlink_test_client.py --host 192.168.4.1 --port 18000
```

---

## 4. Bridge ke Central RingServer (DataLink Port 16000)

Untuk menghubungkan stasiun ke pusat pengolahan data seismik nasional/global (misal IRIS / EarthScope RingServer):

```powershell
python tools/slink2ringserver.py --sl-host 192.168.1.50 --sl-port 18000 --dl-host 127.0.0.1 --dl-port 16000 --station JERIS --network AM --channel EHZ
```

---

## 5. Perekaman Berkas MiniSEED Kontinu (SDS)

Untuk mengarsipkan data mentah secara lokal dengan konvensi nama berkas SeisComP Data Structure (`NET.STA.00.CHAN.D.YYYY.DOY.mseed`):

```powershell
python tools/seedlink_archiver.py --host 192.168.1.50 --port 18000 --outdir archive/
```

---

## 6. Unit Testing Kepatuhan Struktur Biner MiniSEED v2

Untuk memvalidasi integritas Fixed Section of Data Header (48 byte), Blockette 1000 (8 byte), dan selubung paket SeedLink (520 byte) menggunakan engine pustaka ObsPy:

```powershell
python tools/verify_packets.py
```

