#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// ==============================================================================
// 1. KONFIGURASI JARINGAN (SINGLE WI-FI & SOFTAP FALLBACK)
// ==============================================================================
// Ganti dengan SSID dan Password Wi-Fi router Anda (Single Wi-Fi)
const char* const WIFI_SSID       = "JERUKAGUNG SEISMOLOGI"; //Nama SSID
const char* const WIFI_PASS       = "JERIS6467"; //Password SSID

// Parameter SoftAP Darurat (Aktif otomatis jika Wi-Fi di atas gagal terhubung)
#define AP_SSID                   "ESP32-SEISMOMETER"
#define AP_PASSWORD               "seismo1234"

// ==============================================================================
// 2. KONFIGURASI SEEDLINK SERVER & METADATA STASIUN (FDSN STANDARD)
// ==============================================================================
#define SEEDLINK_PORT             18000       // Standar port FDSN SeedLink

// Metadata Stasiun Seismologi
#define STATION_CODE              "JERIS"     // Kode stasiun (maks 5 karakter)
#define STATION_NAME              "Jerukagung Seismologi"
#define NETWORK_CODE              "AM"        // Kode network (2 karakter, AM = Citizen Science / Raspberry Shake)
#define LOCATION_CODE             "00"        // Kode lokasi (2 karakter, "00" = primary sensor)
#define CHANNEL_CODE              "EHZ"       // Saluran default

// Definisi 9 Saluran Multi-Rate FDSN: E (100 Hz), B (50 Hz), S (50 Hz)
// Komponen Triaksial: Z (Vertical), N (North-South), E (East-West)
struct ChannelMeta {
    const char* code;          // e.g. "EHZ"
    const char* location;      // e.g. "00"
    char band;                 // 'E', 'B', 'S'
    char orientation;          // 'Z', 'N', 'E'
    uint16_t sampleRate;       // 100 Hz (E) atau 50 Hz (B, S)
    uint16_t samplesPerRecord; // 100 (E) atau 50 (B, S) -> 1.0 detik per paket
};

const ChannelMeta AVAILABLE_CHANNELS[] = {
    {"EHZ", "00", 'E', 'Z', 100, 100},
    {"EHN", "00", 'E', 'N', 100, 100},
    {"EHE", "00", 'E', 'E', 100, 100},
    {"BHZ", "00", 'B', 'Z', 50,  50},
    {"BHN", "00", 'B', 'N', 50,  50},
    {"BHE", "00", 'B', 'E', 50,  50},
    {"SHZ", "00", 'S', 'Z', 50,  50},
    {"SHN", "00", 'S', 'N', 50,  50},
    {"SHE", "00", 'S', 'E', 50,  50}
};
const size_t NUM_CHANNELS = sizeof(AVAILABLE_CHANNELS) / sizeof(AVAILABLE_CHANNELS[0]);

// Konfigurasi Sampling Rate Master
#define MASTER_SAMPLING_RATE_HZ   100         // Master loop tick pada 100 Hz (10,000 us)
#define SAMPLING_RATE_HZ          100         // Kompatibilitas alias
#define MAX_SAMPLES_PER_RECORD    100         // Maksimum buffer per channel (100 sampel)
#define SAMPLES_PER_RECORD        100         // Kompatibilitas alias

// Konfigurasi SNTP Server (Presisi waktu UTC untuk BTIME MiniSEED)
#define NTP_SERVER_1              "pool.ntp.org"
#define NTP_SERVER_2              "time.nist.gov"
#define NTP_SERVER_3              "id.pool.ntp.org"
#define GMT_OFFSET_SEC            0           // SeedLink / MiniSEED mewajibkan UTC (GMT+0)
#define DAYLIGHT_OFFSET_SEC       0

// Konfigurasi Generator Sinyal Seismik Dummy
#define SEISMIC_NOISE_STD_DEV     120         // Standar deviasi background noise alami (+/- 150 s/d +/- 250 counts)
#define ENABLE_AUTO_QUAKE         false       // false = sinyal natural kontinu; true = gempa periodik
#define EARTHQUAKE_PERIOD_SEC     300         // Interval simulasi gempa otomatis jika diaktifkan (detik)
#define QUAKE_MAX_AMPLITUDE       1800        // Amplitudo puncak gempa wajar (counts, tidak over-clip)

// ==============================================================================
// 3. HEADER ENGINE SEEDLINK & SENSOR
// ==============================================================================
#include "miniseed.h"
#include "synthetic_sensor.h"
#include "seedlink_server.h"

// Instance global
SyntheticSeismicSensor seismicSensor;
SeedLinkServer seedlinkServer(SEEDLINK_PORT);

// Sampling timer
const uint32_t MASTER_TICK_MICROS = 1000000UL / MASTER_SAMPLING_RATE_HZ; // 10,000 us untuk 100Hz
uint32_t lastSampleMicros = 0;
uint32_t tickCount = 0;

// Status & diagnostic timer
uint32_t lastStatusMillis = 0;
bool ntpSynced = false;

// Fallback base epoch jika offline (2026-01-01 00:00:00 UTC)
const time_t FALLBACK_EPOCH = 1767225600;
uint32_t bootMillis = 0;

// ==============================================================================
// 4. IMPLEMENTASI WAKTU UTC & KONEKSI SINGLE WI-FI
// ==============================================================================

time_t getCurrentUtcTime(uint32_t* outMicros) {
    if (outMicros) {
        *outMicros = micros();
    }

    time_t nowSec;
    time(&nowSec);

    // Jika NTP belum tersinkronisasi (waktu < tahun 2024), gunakan waktu boot + fallback epoch
    if (nowSec < 1704067200) {
        nowSec = FALLBACK_EPOCH + ((millis() - bootMillis) / 1000);
    }
    return nowSec;
}

void setupWiFi() {
    Serial.println("\n[WIFI] Memulai koneksi Wi-Fi (Single Wi-Fi)...");
    Serial.printf("[WIFI] Menghubungkan ke SSID: \"%s\"...\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Nonaktifkan Modem Sleep agar radio Wi-Fi 100% aktif (mencegah lag / jitter)
    WiFi.setAutoReconnect(true);

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // Tunggu koneksi hingga ~15 detik (30 iterasi x 500 ms)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] Berhasil terhubung ke Wi-Fi!");
        Serial.printf("[WIFI] SSID Terhubung: %s\n", WiFi.SSID().c_str());
        Serial.printf("[WIFI] IP Address     : %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[WIFI] Subnet Mask    : %s\n", WiFi.subnetMask().toString().c_str());
        Serial.printf("[WIFI] Gateway        : %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("[WIFI] RSSI Signal    : %d dBm\n", WiFi.RSSI());

        // Sinkronisasi NTP waktu UTC
        Serial.println("[NTP] Mengonfigurasi SNTP Client...");
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

        int ntpRetries = 0;
        time_t testTime;
        while (ntpRetries < 10) {
            time(&testTime);
            if (testTime > 1704067200) { // Valid jika > tahun 2024
                ntpSynced = true;
                break;
            }
            delay(300);
            ntpRetries++;
        }

        if (ntpSynced) {
            struct tm timeinfo;
            gmtime_r(&testTime, &timeinfo);
            Serial.printf("[NTP] Waktu UTC Tersinkron: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        } else {
            Serial.println("[NTP] Peringatan: NTP belum merespon. Menggunakan fallback timestamp.");
        }
    } else {
        // Fallback: Aktifkan Access Point Mode (SoftAP) jika Wi-Fi gagal terhubung
        Serial.println("[WIFI] Gagal terhubung ke router. Mengaktifkan Access Point darurat (SoftAP)...");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        IPAddress apIP = WiFi.softAPIP();
        Serial.println("[WIFI] SoftAP Aktif!");
        Serial.printf("[WIFI] AP SSID        : %s\n", AP_SSID);
        Serial.printf("[WIFI] AP Password    : %s\n", AP_PASSWORD);
        Serial.printf("[WIFI] AP IP Addr     : %s\n", apIP.toString().c_str());
    }
}

void printBanner() {
    Serial.println("\n========================================================");
    Serial.println("         ESP32 SEISMOMETER - SEEDLINK SERVER            ");
    Serial.println("========================================================");
    Serial.printf(" Station ID    : %s (%s)\n", STATION_CODE, STATION_NAME);
    Serial.printf(" Network ID    : %s\n", NETWORK_CODE);
    Serial.printf(" Wi-Fi Target  : %s (Single Wi-Fi)\n", WIFI_SSID);
    Serial.printf(" Total Channels: %u FDSN Channels (E, B, S)\n", (unsigned int)NUM_CHANNELS);
    Serial.println(" Saluran Aktif : EHZ (100Hz), EHN (100Hz), EHE (100Hz),");
    Serial.println("                 BHZ (50Hz),  BHN (50Hz),  BHE (50Hz),");
    Serial.println("                 SHZ (50Hz),  SHN (50Hz),  SHE (50Hz)");
    Serial.printf(" Master Tick   : %d Hz (10,000 us)\n", MASTER_SAMPLING_RATE_HZ);
    Serial.println(" Packet Record : 1.0 detik per saluran (100 smpl @ 100Hz, 50 smpl @ 50Hz)");
    Serial.printf(" SeedLink Port : %d\n", SEEDLINK_PORT);
    Serial.println("--------------------------------------------------------");
    Serial.println(" Perintah Serial Monitor:");
    Serial.println("   'q' atau 'Q' -> Memicu simulasi gempa bumi triaksial (Z, N, E)");
    Serial.println("   's' atau 'S' -> Menampilkan status stasiun & server");
    Serial.println("========================================================\n");
}

void setup() {
    bootMillis = millis();
    Serial.begin(115200);
    delay(1000);

    printBanner();

    // Inisialisasi Jaringan (Single Wi-Fi + SoftAP fallback)
    setupWiFi();

    // Jalankan SeedLink TCP Server
    seedlinkServer.begin();

    lastSampleMicros = micros();
    lastStatusMillis = millis();
}

void loop() {
    uint32_t currentMicros = micros();

    // 1. Precise Multi-Rate Master Sampling Engine (100 Hz = 10,000 us)
    // Gunakan while catch-up loop non-dropping agar tidak ada 1 sampel pun yang terbuang
    int catchUpCount = 0;
    while ((currentMicros - lastSampleMicros) >= MASTER_TICK_MICROS && catchUpCount < 10) {
        lastSampleMicros += MASTER_TICK_MICROS;
        tickCount++;
        catchUpCount++;

        uint32_t usOffset = 0;
        time_t utcSec = getCurrentUtcTime(&usOffset);

        // (a) Saluran Band E (EHZ, EHN, EHE): disampel setiap tick (100 Hz) -> indeks 0, 1, 2
        for (size_t c = 0; c < 3; ++c) {
            int32_t s = seismicSensor.readChannel(c);
            seedlinkServer.addChannelSample(c, s, utcSec, usOffset);
        }

        // (b) Saluran Band B (BHZ, BHN, BHE) dan Band S (SHZ, SHN, SHE):
        //     disampel setiap 2 tick (50 Hz) -> indeks 3 s/d 8
        if (tickCount % 2 == 0) {
            for (size_t c = 3; c < NUM_CHANNELS; ++c) {
                int32_t s = seismicSensor.readChannel(c);
                seedlinkServer.addChannelSample(c, s, utcSec, usOffset);
            }
        }
    }

    // Jika terjadi hambatan ekstrem di luar kendali (>500 ms misal reconnect radio), sinkronkan ulang timer
    if ((currentMicros - lastSampleMicros) > (MASTER_TICK_MICROS * 50)) {
        lastSampleMicros = currentMicros;
    }

    // 2. Layani koneksi SeedLink Client (handshake & streaming)
    seedlinkServer.handleClient();

    // 3. User Input via Serial Monitor
    if (Serial.available() > 0) {
        char ch = (char)Serial.read();
        if (ch == 'q' || ch == 'Q') {
            seismicSensor.triggerQuake();
        } else if (ch == 's' || ch == 'S') {
            Serial.println("\n--- STATUS STASIUN SEISMOMETER ---");
            Serial.printf(" Uptime         : %lu detik\n", millis() / 1000);
            Serial.printf(" Mode Jaringan  : %s\n", (WiFi.getMode() == WIFI_AP) ? "SoftAP" : "STA");
            Serial.printf(" IP Address     : %s\n", (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString().c_str() : WiFi.localIP().toString().c_str());
            Serial.printf(" Status Klien   : %s\n", seedlinkServer.isClientStreaming() ? "STREAMING (Aktif)" : "MENUNGGU KONEKSI");
            Serial.printf(" Total Paket TX : %lu paket (%lu bytes)\n", 
                          (unsigned long)seedlinkServer.getTotalPacketsSent(),
                          (unsigned long)seedlinkServer.getTotalPacketsSent() * SEEDLINK_PACKET_SIZE);
            Serial.printf(" Simulasi Gempa : %s\n", seismicSensor.isQuakeActive() ? "SEDANG AKTIF" : "Tenang (Background Noise)");
            Serial.println("----------------------------------\n");
        }
    }

    // 4. Heartbeat Periodik (setiap 30 detik di Serial jika tidak ada streaming aktif)
    if (millis() - lastStatusMillis > 30000) {
        lastStatusMillis = millis();
        if (!seedlinkServer.isClientStreaming()) {
            IPAddress ip = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
            Serial.printf("[HEARTBEAT] Server siap di %s:%d | Menunggu koneksi SeedLink client...\n",
                          ip.toString().c_str(), SEEDLINK_PORT);
        }
    }
}
