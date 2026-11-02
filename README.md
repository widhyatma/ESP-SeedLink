# ESP32 Seismometer SeedLink Server (MiniSEED v2 Real-time)

Proyek firmware seismometer berbasis **ESP32** yang mengimplementasikan **SeedLink Protocol v3.1** (port `18000`) dan memancarkan data getaran tanah (*time-series*) dalam format standar internasional **MiniSEED v2 (512-byte record)** secara *native*, deterministik, dan *real-time*.

Firmware ini mendukung **PlatformIO** dan **Arduino IDE** (kompatibel penuh), serta siap dihubungkan langsung ke software analisis seismologi standar dunia seperti **ObsPy**, **SeisComP**, **USGS SWARM**, **Earthworm**, dan **Jamaseis**.

---

## 🌟 Fitur Utama

1. **Native SeedLink Server (Port 18000)**:
   - Server TCP mandiri langsung di atas mikrokontroler ESP32 tanpa perantara komputer.
   - Siklus negosiasi SeedLink lengkap: `HELLO`, `USERAGENT`, `STATION`, `SELECT`, `DATA`, `FETCH`, `TIME`, `END`, dan `BYE`.
   - Mengemas paket transmisi standar 520 byte (8-byte ASCII header `SL######` + 512-byte MiniSEED).
   - Mendukung hingga 16 koneksi klien simultan dengan filter selektor saluran mandiri.
2. **Kepatuhan Standar FDSN MiniSEED v2**:
   - Fixed Section of Data Header (FSDH, 48 bytes) dengan metadata stasiun (`AM.JERIS.00.EHZ`).
   - Blockette 1000 (8 bytes) encoding `INT32` (Big-Endian).
   - Penjamin kontinuitas matematika (*Zero-Gap BTIME*) via sinkronisasi SNTP UTC.
3. **Dukungan 9 Saluran Multi-Rate FDSN (E, B, S)**:
   - **Band E (100 Hz)**: `EHZ`, `EHN`, `EHE` (Extremely Short Period / High Rate).
   - **Band B (50 Hz)**: `BHZ`, `BHN`, `BHE` (Broadband).
   - **Band S (50 Hz)**: `SHZ`, `SHN`, `SHE` (Short Period).
4. **Dual Engine: Sinyal Sintetis & Sensor Analog ADC Riil**:
   - **Engine Sintetis**: Pemodelan derau geofisika alami bumi (colored Gaussian noise, osilasi mikroseisme samudra 0,15 & 0,23 Hz, dan simulator gempa triaksial P-wave, S-wave, serta peluruhan Coda).
   - **Engine Analog Riil**: Pembacaan ADC1 ESP32 (GPIO 36/VP, GPIO 39/VN, GPIO 34) dengan 16x oversampling dan filter *DC-Blocker* (zero-baseline).
5. **Konektivitas Jaringan Ringkas (Single Wi-Fi + SoftAP Fallback)**:
   - Konfigurasi Wi-Fi tunggal sederhana langsung di dalam berkas sketsa utama.
   - *Fail-safe emergency fallback*: Jika router tidak terhubung, otomatis mengaktifkan Access Point mandiri (`ESP32-SEISMOMETER`, IP: `192.168.4.1`) untuk akses lokal di lapangan.
6. **Dual Compatibility (PlatformIO & Arduino IDE)**:
   - Dilengkapi metadata `library.properties` dan folder sketsa `examples/` mandiri berformat `.ino` siap buka 1-klik di Arduino IDE.
7. **Testing Suite Lengkap (Python)**:
   - `tools/seedlink_test_client.py`: Client terminal pemantau live stream dengan visualisasi ASCII sparkline.
   - `tools/seedlink_mock_server.py`: Simulator SeedLink mandiri pada PC untuk uji software tanpa perangkat fisik.
   - `tools/slink2ringserver.py`: Bridge forwarder ke Central RingServer IRIS/EarthScope (DataLink port 16000).
   - `tools/seedlink_archiver.py`: Perekam kontinu ke struktur file harian SDS (*SeisComP Data Structure*).
   - `tools/verify_packets.py`: Unit test ObsPy validasi biner paket MiniSEED v2 & SeedLink 520-byte.

---

## 📁 Struktur Direktori

```text
ESP-SeedLink/
├── library.properties                    # Metadata resmi Arduino Library
├── platformio.ini                        # Konfigurasi multi-environment PlatformIO
├── src/                                  # Source code utama & library core
│   ├── Example.cpp                       # Sketsa mode Dummy Sintetis (PlatformIO)
│   ├── ExampleAnalog.cpp                 # Sketsa mode Sensor Analog ADC (PlatformIO)
│   ├── miniseed.h                        # Builder biner MiniSEED v2 (FSDH & Blockette 1000)
│   ├── seedlink_server.h                 # State machine TCP Server SeedLink v3.1
│   └── synthetic_sensor.h                # Generator getaran gempa bumi sintetis
├── examples/                             # Sketsa siap pakai untuk Arduino IDE
│   ├── ESP32_SeedLink_Synthetic/
│   │   ├── ESP32_SeedLink_Synthetic.ino  # Sketsa simulasi sintetis (Arduino IDE)
│   │   ├── miniseed.h
│   │   ├── seedlink_server.h
│   │   └── synthetic_sensor.h
│   └── ESP32_SeedLink_Analog/
│       ├── ESP32_SeedLink_Analog.ino     # Sketsa sensor analog ADC riil (Arduino IDE)
│       ├── miniseed.h
│       └── seedlink_server.h
├── tools/                                # Perkakas uji & integrasi Python
│   ├── seedlink_test_client.py           # Client terminal pemantau live waveform
│   ├── seedlink_mock_server.py           # Server mock standalone di PC
│   ├── slink2ringserver.py               # Forwarder ke Central RingServer (DataLink)
│   ├── seedlink_archiver.py              # Perekam otomatis ke file .mseed harian (SDS)
│   └── verify_packets.py                 # Unit test verifikasi struktur biner MiniSEED
└── README.md
```

---

## ⚙️ 1. Konfigurasi Firmware (Single Wi-Fi & Stasiun)

Semua konfigurasi stasiun dan jaringan sekarang **disatukan langsung di bagian atas berkas sketsa** (`Example.cpp` atau berkas sketsa `.ino`), memudahkan kustomisasi tanpa perlu berpindah-pindah berkas:

```cpp
// 1. Kredensial Wi-Fi Router Anda (Single Wi-Fi)
const char* const WIFI_SSID       = "JERUKAGUNG SEISMOLOGI";
const char* const WIFI_PASS       = "JERIS6467";

// Parameter SoftAP Darurat (Aktif otomatis jika Wi-Fi di atas gagal)
#define AP_SSID                   "ESP32-SEISMOMETER"
#define AP_PASSWORD               "seismo1234"

// 2. Metadata Stasiun Seismologi (Standar FDSN)
#define SEEDLINK_PORT             18000
#define STATION_CODE              "JERIS"   // Kode stasiun (maks 5 karakter)
#define STATION_NAME              "Jerukagung Seismologi"
#define NETWORK_CODE              "AM"      // Kode network (AM = Raspberry Shake / Citizen Science)
#define LOCATION_CODE             "00"      // Kode lokasi ("00" = primary sensor)
#define CHANNEL_CODE              "EHZ"     // Saluran default
```

> **Catatan Darurat:** Jika router Wi-Fi tidak ditemukan atau padam, ESP32 otomatis beralih ke mode Access Point (`ESP32-SEISMOMETER`, password: `seismo1234`). Anda dapat langsung menghubungkan laptop ke Wi-Fi tersebut di IP `192.168.4.1:18000`.

---

## 🚀 2. Panduan Penggunaan: Arduino IDE

1. Buka aplikasi **Arduino IDE** (versi 1.8.x atau 2.x).
2. Klik menu **File** $\rightarrow$ **Open...**
3. Pilih salah satu sketsa di folder `examples/`:
   - **Mode Simulasi Sintetis**: `examples/ESP32_SeedLink_Synthetic/ESP32_SeedLink_Synthetic.ino`
   - **Mode Sensor Analog Riil**: `examples/ESP32_SeedLink_Analog/ESP32_SeedLink_Analog.ino`
4. Arduino IDE akan otomatis membuka file `.ino` beserta seluruh berkas header (`miniseed.h`, `seedlink_server.h`, dll.) sebagai **Tabs** di bagian atas editor.
5. Pada menu **Tools** $\rightarrow$ **Board**, pilih **ESP32 Dev Module**.
6. Klik tombol **Verify (Centang)** untuk mengompilasi, lalu **Upload (Panah)** untuk mem-flash ke ESP32.

---

## 💻 3. Panduan Penggunaan: PlatformIO (VS Code)

Buka terminal di root repositori dan jalankan perintah PlatformIO:

```powershell
# Kompilasi mode Sensor Analog ADC (Default):
pio run -e esp32dev_analog

# Atau kompilasi mode Dummy Sintetis:
pio run -e esp32dev

# Upload firmware ke ESP32 fisik (hubungkan kabel USB):
pio run -e esp32dev_analog -t upload

# Buka Serial Monitor (kecepatan baud 115200):
pio device monitor -b 115200
```

---

## ⌨️ 4. Kontrol Serial Monitor Interaktif

Buka Serial Monitor pada baud rate `115200`:
- Ketik `s` atau `S` lalu Enter: **Menampilkan ringkasan status stasiun**, IP, status klien streaming, dan total paket TX.
- Ketik `q` atau `Q` lalu Enter (*khusus mode simulasi*): **Memicu simulasi gempa bumi triaksial** secara manual.
- Ketik `a` atau `A` lalu Enter (*khusus mode analog*): **Mencetak nilai ADC mentah vs counts terfilter**.
- Ketik `c` atau `C` lalu Enter (*khusus mode analog*): **Kalibrasi ulang titik nol baseline DC-Blocker**.

---

## 🔬 5. Pengujian & Monitoring Live Stream (Python)

### A. Monitor Seismogram Terminal (Sparkline ASCII)
Gunakan `tools/seedlink_test_client.py` yang ditenagai oleh pustaka ObsPy:

```powershell
# Hubungkan ke IP router ESP32:
python tools/seedlink_test_client.py --host 192.168.1.50 --port 18000

# Atau jika terhubung ke SoftAP ESP32:
python tools/seedlink_test_client.py --host 192.168.4.1 --port 18000
```

Output langsung di terminal:
```text
SEQ      | TIMESTAMP (UTC)          | SMPL  | RATE  | PEAK(counts) | WAVEFORM SPARKLINE                      
--------------------------------------------------------------------------------------------------------------
SL000001 | 2026-11-02 12:30:00      | 100   | 100   | 34           | [:..+-===##*#=+%+:=+@%*+##+*#+::%%:**%=]
SL000002 | 2026-11-02 12:30:01      | 100   | 100   | 29           | [*=:#-=**#=:.:++_=+**.:-=._:  - =+.=- _]
SL000003 | 2026-11-02 12:30:02      | 100   | 100   | 1850         | [::::::==::=::::::==::=:=:=@: :%= -%* .]
         >>> EVENT GEMPA TERDETEKSI! Amplitudo Puncak: 1850 counts <<<
```

### B. Simulasi Tanpa Hardware (PC Mock Server)
```powershell
# Terminal 1: Jalankan server mock
python tools/seedlink_mock_server.py

# Terminal 2: Sambungkan test client
python tools/seedlink_test_client.py --host 127.0.0.1 --port 18000
```

### C. Meneruskan Aliran Data ke Central RingServer (DataLink)
```powershell
python tools/slink2ringserver.py --sl-host 192.168.1.50 --dl-host 127.0.0.1 --dl-port 16000
```

### D. Perekaman Berkas MiniSEED SDS Harian
```powershell
python tools/seedlink_archiver.py --host 192.168.1.50 --outdir archive/
```

### E. Unit Test Validasi Biner MiniSEED v2
```powershell
python tools/verify_packets.py
```

---

## 🔌 6. Panduan Sambungan Pin Analog Sensor (Geophone)

Pada sketsa mode analog ([src/ExampleAnalog.cpp](src/ExampleAnalog.cpp) atau [examples/ESP32_SeedLink_Analog/ESP32_SeedLink_Analog.ino](examples/ESP32_SeedLink_Analog/ESP32_SeedLink_Analog.ino)), sinyal analog dihubungkan ke saluran ADC1:

| Sumbu Sensor | Pin ESP32 | Keterangan |
| :---: | :---: | :--- |
| **Z (Vertikal)** | `GPIO 36` (VP / A0) | Input utama (Geophone Vertikal 1-sumbu atau Triaksial Z) |
| **N (Utara-Selatan)** | `GPIO 39` (VN / A3) | Input Triaksial Horisontal N |
| **E (Timur-Barat)** | `GPIO 34` (A6) | Input Triaksial Horisontal E |

> **Perhatian:** Jangan gunakan pin ADC2 (GPIO 0, 2, 4, 12–15, 25–27) untuk membaca sensor analog saat Wi-Fi menyala, karena ADC2 digunakan bergantian dengan radio Wi-Fi mikrokontroler. Sinyal geophone wajib di-bias pada tegangan tengah ($V_{cc}/2 \approx 1,65\text{ V}$) sebelum masuk ke ADC ESP32.
