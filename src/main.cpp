#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
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
    Serial.println("\n[WIFI] Memulai koneksi Wi-Fi (Multi-AP Prioritas)...");
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Nonaktifkan Modem Sleep agar radio Wi-Fi 100% aktif (mencegah lag / jitter)
    WiFi.setAutoReconnect(true);

    bool connected = false;
    for (size_t i = 0; i < NUM_WIFI_NETWORKS; ++i) {
        Serial.printf("[WIFI] [%u/%u] Mencoba koneksi ke: \"%s\"...\n", 
                      (unsigned int)(i + 1), (unsigned int)NUM_WIFI_NETWORKS, WIFI_SSID[i]);
        
        WiFi.begin(WIFI_SSID[i], WIFI_PASS[i]);
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 24) { // ~12 detik per AP
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
            Serial.printf("[WIFI] Berhasil terhubung ke: \"%s\" (Prioritas #%u)\n", 
                          WIFI_SSID[i], (unsigned int)(i + 1));
            break;
        } else {
            Serial.printf("[WIFI] Gagal terhubung ke \"%s\". Mencoba cadangan berikutnya...\n", WIFI_SSID[i]);
            WiFi.disconnect(true);
            delay(100);
        }
    }

    if (connected && WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI] Status Jaringan Aktif:");
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
        // Fallback: Aktifkan Access Point Mode (SoftAP) jika semua Wi-Fi cadangan gagal
        Serial.println("[WIFI] Semua Wi-Fi cadangan gagal terhubung. Mengaktifkan Access Point (SoftAP)...");
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
    Serial.printf(" Total Channels: %u FDSN Channels (E, B, S)\n", (unsigned int)NUM_CHANNELS);
    Serial.println(" Saluran Aktif : EHZ (100Hz), EHN (100Hz), EHE (100Hz),");
    Serial.println("                 BHZ (50Hz),  BHN (50Hz),  BHE (50Hz),");
    Serial.println("                 SHZ (50Hz),  SHN (50Hz),  SHE (50Hz)");
    Serial.printf(" Master Tick   : %d Hz (10,000 us)\n", MASTER_SAMPLING_RATE_HZ);
    Serial.println(" Packet Record : 1.0 detik per saluran (100 smpl @ 100Hz, 50 smpl @ 50Hz)");
    Serial.printf(" SeedLink Port : %d\n", SEEDLINK_PORT);
    Serial.println("--------------------------------------------------------");
    Serial.println(" Perintah Serial:");
    Serial.println("   'q' atau 'Q' -> Memicu simulasi gempa bumi triaksial (Z, N, E)");
    Serial.println("   's' atau 'S' -> Menampilkan status stasiun & server");
    Serial.println("========================================================\n");
}

void setup() {
    bootMillis = millis();
    Serial.begin(115200);
    delay(1000);

    printBanner();

    // Inisialisasi Jaringan
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
