# Aturan I: Manajemen Wi-Fi Multi-AP Berprioritas & Failover

## Deskripsi & Tujuan
Mengatur strategi konektivitas jaringan nirkabel (Wi-Fi) untuk mikrokontroler IoT/ESP32 seismometer agar menjamin ketahanan koneksi, radio Wi-Fi berdaya penuh (tanpa lag/jitter), serta kemampuan pemulihan darurat di lapangan.

## Pedoman Implementasi

1. **Struktur Multi-AP Terurut Berdasarkan Prioritas:**
   - Kredensial jaringan wajib didefinisikan dalam bentuk array string konstan (`const char* const WIFI_SSID[]` dan `WIFI_PASS[]`).
   - Urutan indeks mencerminkan prioritas utama (indeks 0) hingga cadangan sekunder (*fallback/backup AP*).

2. **Koneksi Sekuensial & Zero Modem Sleep:**
   - Firmware mencoba menghubungkan ke SSID secara berurutan dengan batas waktu (*timeout*) 12 detik (~24 iterasi x 500 ms) per titik akses.
   - **Wajib menonaktifkan Wi-Fi Modem Sleep** (`WiFi.setSleep(false)`). Radio Wi-Fi harus tetap aktif 100% untuk menghindari fluktuasi latensi paket (jitter) dan pemutusan koneksi sepihak oleh klien seismologi (SWARM/ObsPy).
   - Mengaktifkan fitur auto-reconnect (`WiFi.setAutoReconnect(true)`).

3. **Emergency SoftAP Fallback:**
   - Apabila seluruh jaringan Wi-Fi cadangan dalam daftar gagal terhubung, perangkat wajib otomatis beralih ke mode Access Point (`WiFi.mode(WIFI_AP)`) dengan SSID e.g. `ESP32-SEISMOMETER` dan IP default `192.168.4.1`.
   - Hal ini memastikan seismometer tetap dapat diakses untuk inspeksi lokal langsung di lapangan tanpa membutuhkan router eksternal.

4. **Non-blocking Sampling:**
   - Proses monitoring status jaringan, rekoneksi, atau penyaluran soket tidak boleh menggunakan `delay()` panjang yang memblokir timer sampling sensor seismik ($10.000\ \mu\text{s}$ atau $20.000\ \mu\text{s}$).

