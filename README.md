# ESP32 Seismometer SeedLink Server (Dummy & Realtime)

Proyek firmware seismometer berbasis **ESP32** (PlatformIO / Arduino Framework) yang mengimplementasikan **SeedLink Protocol v3.1** (port `18000`) dan memancarkan data time-series dalam format **MiniSEED v2 (512-byte)** secara *native* dan *real-time*.

Firmware ini dirancang khusus untuk pengujian awal (*dummy test*) maupun sebagai pondasi stasiun seismometer IoT siap produksi yang dapat langsung dihubungkan ke software analisis seismologi standar dunia seperti **ObsPy**, **SeisComP**, **SWARM**, **Earthworm**, dan **Jamaseis**.

---

## 🌟 Fitur Utama

1. **Native SeedLink Server (Port 18000)**:
   - Menjalankan TCP server mandiri langsung di atas ESP32.
   - Mendukung siklus negosiasi SeedLink lengkap: `HELLO`, `USERAGENT`, `STATION`, `SELECT`, `DATA`, `FETCH`, `TIME`, `END`, dan `BYE`.
   - Mengemas paket transmisi standar 520 byte (8-byte ASCII header `SL######` + 512-byte MiniSEED).
2. **Kepatuhan Penuh Standar MiniSEED v2**:
   - Fixed Section of Data Header (FSDH, 48 bytes) dengan metadata stasiun (`XX.ESP01.00.EHZ`).
   - Blockette 1000 (8 bytes) dengan encoding `INT32` (Big-Endian).
   - Sinkronisasi waktu presisi tinggi (`BTIME`) via SNTP (NTP UTC) dengan fallback internal microsecond counter.
3. **Simulasi Sinyal Seismik Sintetis (Dummy Engine)**:
   - Sinyal mikroseismik bumi (background noise $\pm 30$ counts).
   - Event gempa otomatis berkala (setiap 60 detik) atau dipicu manual lewat Serial Monitor (`q`).
   - Memodelkan fase gelombang seismik nyata: **P-wave onset** (~6 Hz), **S-wave arrival** (~2.5 Hz amplitudo puncak), dan **Coda exponential decay** (~1.2 Hz).
4. **Dual Mode Jaringan (STA + SoftAP Fallback)**:
   - Jika router Wi-Fi tidak tersedia, ESP32 otomatis mengaktifkan mode Access Point (`ESP32-SEISMOMETER`, IP: `192.168.4.1`), sehingga Anda dapat langsung menghubungkan laptop ke Wi-Fi ESP32 tanpa jaringan internet.
5. **Testing Suite Lengkap (Python)**:
   - `tools/seedlink_test_client.py`: Client terminal dengan visualisasi seismogram ASCII live (sparkline).
   - `tools/seedlink_mock_server.py`: Simulator SeedLink standalone untuk uji coba PC tanpa perangkat fisik.
   - `tools/verify_packets.py`: Unit test verifikasi struktur biner MiniSEED dengan ObsPy.

---

## 📁 Struktur Direktori

```text
SEISMOMETER/
├── include/
│   ├── config.h                 # Konfigurasi stasiun, sampling rate, Wi-Fi multi-AP, NTP
│   ├── miniseed.h               # Struktur biner FSDH, Blockette 1000, dan MiniSEED builder
│   ├── seedlink_server.h        # State machine TCP server SeedLink v3.1
│   └── synthetic_sensor.h       # Generator gelombang gempa sintetis
├── src/
│   ├── Example.cpp              # Entry point simulasi dummy sintetis (P, S, Coda, Microseism)
│   └── ExampleAnalog.cpp        # Entry point input sensor analog ADC riil (Geophone / Accelerometer)
├── tools/
│   ├── seedlink_test_client.py  # Client pemantau live stream terminal
│   ├── seedlink_mock_server.py  # Server mock untuk testing di PC
│   ├── slink2ringserver.py      # Bridge forwarder ke Central RingServer (DataLink)
│   ├── seedlink_archiver.py     # Perekam otomatis ke file MiniSEED SDS (.mseed) harian
│   └── verify_packets.py        # Unit test validasi biner MiniSEED
├── platformio.ini               # Konfigurasi board ESP32
└── README.md
```

---

## 🚀 Panduan Memulai Cepat

### 1. Konfigurasi Firmware (`include/config.h`)

Buka file [include/config.h](include/config.h) dan sesuaikan kredensial Wi-Fi prioritas (diurutkan dari prioritas tertinggi ke cadangan):

```cpp
const char* const WIFI_SSID[] = {
    "JERUKAGUNG SEISMOLOGI",
    "Jerukagung Seismologi",
    "widhyatma",
    "Kudo Labs"
};

const char* const WIFI_PASS[] = {
    "JERIS6467",
    "riset1234",
    "aduhai6467",
    "HasuminBest"
};

#define STATION_CODE        "ESP01"  // 5 karakter kode stasiun
#define NETWORK_CODE        "XX"     // 2 karakter kode network
#define CHANNEL_CODE        "EHZ"    // Saluran seismik (e.g. EHZ = Short-period, High-gain, Vertical)
#define SAMPLING_RATE_HZ    50       // Frekuensi sampling (50 Hz = 20 ms interval)
#define SAMPLES_PER_RECORD  100      // 100 sampel per paket MiniSEED (2 detik data per paket)
```

> **Catatan:** Jika seluruh Wi-Fi di atas tidak dapat dihubungi, ESP32 otomatis mengaktifkan mode Access Point cadangan darurat (*fail-safe*):
> - **SSID:** `ESP32-SEISMOMETER`
> - **Password:** `seismo1234`
> - **IP Server SeedLink:** `192.168.4.1:18000`

### 2. Kompilasi dan Flash ke ESP32

Pastikan PlatformIO CLI atau ekstensi PlatformIO IDE di VS Code terpasang:

```powershell
# Kompilasi mode Analog Sensor ADC riil (Default):
pio run -e esp32dev_analog

# Atau kompilasi mode Dummy Sintetis:
pio run -e esp32dev

# Flash ke ESP32 (hubungkan kabel USB):
pio run -e esp32dev_analog -t upload

# Buka Serial Monitor (115200 baud):
pio device monitor -b 115200
```

### 3. Kontrol Lewat Serial Monitor

Saat terhubung di Serial Monitor (115200 baud):
- Ketik `q` atau `Q` lalu Enter: **Memicu simulasi gempa bumi secara manual**.
- Ketik `s` atau `S` lalu Enter: **Menampilkan ringkasan status stasiun dan paket**.

---

## 🔬 Pengujian dengan Python Test Client

Gunakan script `seedlink_test_client.py` yang sudah disediakan di folder `tools/`:

```powershell
# Hubungkan ke ESP32 via Wi-Fi router (ganti dengan IP ESP32 Anda)
python tools/seedlink_test_client.py --host 192.168.1.50 --port 18000

# Atau jika terhubung ke SoftAP ESP32:
python tools/seedlink_test_client.py --host 192.168.4.1 --port 18000
```

Output di terminal akan menampilkan stream gelombang secara langsung:

```text
SEQ      | TIMESTAMP (UTC)          | SMPL  | RATE  | PEAK(counts) | WAVEFORM SPARKLINE                      
--------------------------------------------------------------------------------------------------------------
SL000001 | 2026-10-31 00:30:19      | 100   | 50    | 32           | [:..+-===##*#=+%+:=+@%*+##+*#+::%%:**%=]
SL000002 | 2026-10-31 00:30:21      | 100   | 50    | 28           | [*=:#-=**#=:.:++_=+**.:-=._:  - =+.=- _]
SL000003 | 2026-10-31 00:30:23      | 100   | 50    | 7850         | [::::::==::=::::::==::=:=:=@: :%= -%* .]
         >>> EVENT GEMPA TERDETEKSI! Amplitudo Puncak: 7850 counts <<<
```

---

## 💻 Pengujian di PC Tanpa ESP32 (Mock Server)

Anda dapat menjalankan mock server langsung di PC untuk menguji software seismologi:

```powershell
# Terminal 1: Jalankan mock server
python tools/seedlink_mock_server.py

# Terminal 2: Sambungkan test client atau ObsPy
python tools/seedlink_test_client.py --host 127.0.0.1 --port 18000
```

---

## 🛠️ Integrasi Sensor Fisik (Geophone / Accelerometer)

Ketika Anda siap mengganti generator sintetis dengan sensor fisik (seperti geophone SM-24 dengan ADC ADS1115 / ADS1256 atau accelerometer MEMS I2C/SPI):

1. Buka [src/main.cpp](src/main.cpp).
2. Di fungsi `loop()`, ganti pembacaan `seismicSensor.readSample()` dengan fungsi pembacaan ADC fisik:

```cpp
// Contoh integrasi ADC ADS1115 / ADS1256:
int32_t rawSample = readExternalADC(); // atau ads.readADC_Differential_0_1();
seedlinkServer.addSample(rawSample, utcSec, usOffset);
```

Arsitektur modular ini memastikan bahwa seluruh pipeline jaringan, packetizer MiniSEED, dan server SeedLink tetap berjalan tanpa perubahan!

