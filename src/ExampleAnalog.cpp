#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// ==============================================================================
// 1. KONFIGURASI JARINGAN (SINGLE WI-FI & SOFTAP FALLBACK)
// ==============================================================================
// Ganti dengan SSID dan Password Wi-Fi router Anda (Single Wi-Fi)
const char* const WIFI_SSID       = "JERUKAGUNG SEISMOLOGI";
const char* const WIFI_PASS       = "JERIS6467";

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

// ==============================================================================
// 3. HEADER ENGINE SEEDLINK & PROTOKOL
// ==============================================================================
#include "miniseed.h"
#include "seedlink_server.h"

// ==============================================================================
// 4. KONFIGURASI SENSOR ANALOG ADC1 (GEOPHONE / ACCELEROMETER)
// ==============================================================================
// PENTING - ATURAN PIN ADC ESP32:
// Hanya gunakan pin ADC1 (GPIO 32, 33, 34, 35, 36, 39) saat Wi-Fi aktif!
// Pin ADC2 TIDAK DAPAT digunakan bersamaan dengan driver radio Wi-Fi.

#define ANALOG_PIN_Z              36  // GPIO36 / VP / A0 (Komponen Vertikal - Z)
#define ANALOG_PIN_N              39  // GPIO39 / VN / A3 (Komponen Horisontal Utara - N)
#define ANALOG_PIN_E              34  // GPIO34 / A6      (Komponen Horisontal Timur - E)

// true jika hanya memasang 1 sensor geophone vertikal Z (SANGAT DIREKOMENDASIKAN jika hanya 1 sensor
// agar CPU tidak membuang waktu membaca pin N dan E yang mengambang/floating)
// false jika menggunakan 3 sensor triaksial lengkap (Z, N, E)
#define SINGLE_AXIS_VERTICAL      true

// Oversampling ADC: 2x sudah sangat optimal untuk ESP32 (waktu konversi ~80 us vs ~2.4 ms pada 16x)
// Mengurangi oversampling ke 2x secara drastis melenyapkan lag/jeda transmisi paket SeedLink!
#define ADC_OVERSAMPLE_COUNT      2
#define ANALOG_GAIN_MULTIPLIER    16

// Filter DC-Blocker rekursif: y[n] = x[n] - x[n-1] + R * y[n-1] (fc ≈ 0.05 Hz)
struct AnalogFilterState {
    float x1;
    float y1;
    float rawAvg;
};

static AnalogFilterState filterZ = {0.0f, 0.0f, 2048.0f};
static AnalogFilterState filterN = {0.0f, 0.0f, 2048.0f};
static AnalogFilterState filterE = {0.0f, 0.0f, 2048.0f};

// Instance Server SeedLink
SeedLinkServer seedlinkServer(SEEDLINK_PORT);

// Sampling timer
const uint32_t MASTER_TICK_MICROS = 1000000UL / MASTER_SAMPLING_RATE_HZ; // 10,000 us (100 Hz)
uint32_t lastSampleMicros = 0;
uint32_t tickCount = 0;

// Status & diagnostic timer
uint32_t lastStatusMillis = 0;
bool ntpSynced = false;

// Fallback base epoch jika offline (2026-01-01 00:00:00 UTC)
const time_t FALLBACK_EPOCH = 1767225600;
uint32_t bootMillis = 0;

// Diagnostik nilai terakhir
volatile int32_t lastRawZ = 0, lastRawN = 0, lastRawE = 0;
volatile int32_t lastCountsZ = 0, lastCountsN = 0, lastCountsE = 0;

// ==============================================================================
// 5. FUNGSI AKUISISI & FILTERING ANALOG
// ==============================================================================

int32_t readAdcOversampled(uint8_t pin) {
    uint32_t sum = 0;
    for (int i = 0; i < ADC_OVERSAMPLE_COUNT; ++i) {
        sum += analogRead(pin);
    }
    return (int32_t)(sum / ADC_OVERSAMPLE_COUNT);
}

int32_t processAnalogSample(int32_t rawValue, AnalogFilterState& state) {
    float rawF = (float)rawValue;
    state.rawAvg += 0.001f * (rawF - state.rawAvg);

    // Filter DC-Blocker High-pass
    float dcBlocked = rawF - state.x1 + 0.995f * state.y1;
    state.x1 = rawF;
    state.y1 = dcBlocked;

    return (int32_t)(dcBlocked * (float)ANALOG_GAIN_MULTIPLIER);
}

void readAnalogSensors(int32_t& outZ, int32_t& outN, int32_t& outE) {
    int32_t rawZ = readAdcOversampled(ANALOG_PIN_Z);
    outZ = processAnalogSample(rawZ, filterZ);
    lastRawZ = rawZ;
    lastCountsZ = outZ;

    if (!SINGLE_AXIS_VERTICAL) {
        int32_t rawN = readAdcOversampled(ANALOG_PIN_N);
        int32_t rawE = readAdcOversampled(ANALOG_PIN_E);
        outN = processAnalogSample(rawN, filterN);
        outE = processAnalogSample(rawE, filterE);
        lastRawN = rawN;
        lastRawE = rawE;
        lastCountsN = outN;
        lastCountsE = outE;
    } else {
        outN = 0;
        outE = 0;
        lastRawN = 0;
        lastRawE = 0;
        lastCountsN = 0;
        lastCountsE = 0;
    }
}

// ==============================================================================
// 6. WAKTU UTC & SINGLE WI-FI SETUP
// ==============================================================================

time_t getCurrentUtcTime(uint32_t* outMicros) {
    if (outMicros) {
        *outMicros = micros();
    }

    time_t nowSec;
    time(&nowSec);

    if (nowSec < 1704067200) {
        nowSec = FALLBACK_EPOCH + ((millis() - bootMillis) / 1000);
    }
    return nowSec;
}

void setupWiFi() {
    Serial.println("\n[WIFI] Memulai koneksi Wi-Fi (Single Wi-Fi)...");
    Serial.printf("[WIFI] Menghubungkan ke SSID: \"%s\"...\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // Nonaktifkan Modem Sleep
    WiFi.setAutoReconnect(true);

    WiFi.begin(WIFI_SSID, WIFI_PASS);

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

        Serial.println("[NTP] Mengonfigurasi SNTP Client...");
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

        int ntpRetries = 0;
        time_t testTime;
        while (ntpRetries < 10) {
            time(&testTime);
            if (testTime > 1704067200) {
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
    Serial.println("     ESP32 SEISMOMETER - ANALOG INPUT SEEDLINK SERVER   ");
    Serial.println("========================================================");
    Serial.printf(" Station ID    : %s (%s)\n", STATION_CODE, STATION_NAME);
    Serial.printf(" Network ID    : %s\n", NETWORK_CODE);
    Serial.printf(" Wi-Fi Target  : %s (Single Wi-Fi)\n", WIFI_SSID);
    Serial.printf(" Mode Sensor   : %s\n", SINGLE_AXIS_VERTICAL ? "1-Sumbu Vertikal (Z)" : "3-Sumbu Triaksial (Z, N, E)");
    Serial.printf(" Pin Analog Z  : GPIO%d (Oversample: %dx, Gain: %dx)\n", ANALOG_PIN_Z, ADC_OVERSAMPLE_COUNT, ANALOG_GAIN_MULTIPLIER);
    if (!SINGLE_AXIS_VERTICAL) {
        Serial.printf(" Pin Analog N  : GPIO%d\n", ANALOG_PIN_N);
        Serial.printf(" Pin Analog E  : GPIO%d\n", ANALOG_PIN_E);
    }
    Serial.printf(" Total Channels: %u FDSN Channels (E, B, S)\n", (unsigned int)NUM_CHANNELS);
    Serial.println(" Saluran Aktif : EHZ (100Hz), EHN (100Hz), EHE (100Hz),");
    Serial.println("                 BHZ (50Hz),  BHN (50Hz),  BHE (50Hz),");
    Serial.println("                 SHZ (50Hz),  SHN (50Hz),  SHE (50Hz)");
    Serial.printf(" Master Tick   : %d Hz (10,000 us)\n", MASTER_SAMPLING_RATE_HZ);
    Serial.println(" Packet Record : 1.0 detik per saluran");
    Serial.printf(" SeedLink Port : %d\n", SEEDLINK_PORT);
    Serial.println("--------------------------------------------------------");
    Serial.println(" Perintah Serial Monitor:");
    Serial.println("   's' atau 'S' -> Menampilkan status stasiun & server");
    Serial.println("   'a' atau 'A' -> Cetak nilai pembacaan analog terkini");
    Serial.println("   'c' atau 'C' -> Reset/Kalibrasi ulang DC-Blocker baseline");
    Serial.println("========================================================\n");
}

void setup() {
    bootMillis = millis();
    Serial.begin(115200);
    delay(1000);

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    pinMode(ANALOG_PIN_Z, INPUT);
    if (!SINGLE_AXIS_VERTICAL) {
        pinMode(ANALOG_PIN_N, INPUT);
        pinMode(ANALOG_PIN_E, INPUT);
    }

    printBanner();

    setupWiFi();
    seedlinkServer.begin();

    lastSampleMicros = micros();
    lastStatusMillis = millis();
}

void loop() {
    uint32_t currentMicros = micros();

    int catchUpCount = 0;
    int32_t sampleZ = 0, sampleN = 0, sampleE = 0;
    bool adcSampleRead = false;

    while ((currentMicros - lastSampleMicros) >= MASTER_TICK_MICROS && catchUpCount < 10) {
        lastSampleMicros += MASTER_TICK_MICROS;
        tickCount++;
        catchUpCount++;

        uint32_t usOffset = 0;
        time_t utcSec = getCurrentUtcTime(&usOffset);

        // Baca konversi ADC hanya sekali per siklus loop utama (mencegah beban berulang saat catch-up)
        if (!adcSampleRead) {
            readAnalogSensors(sampleZ, sampleN, sampleE);
            adcSampleRead = true;
        }

        // (a) Band E (100 Hz): disampel setiap tick
        seedlinkServer.addChannelSample(0, sampleZ, utcSec, usOffset); // EHZ
        seedlinkServer.addChannelSample(1, sampleN, utcSec, usOffset); // EHN
        seedlinkServer.addChannelSample(2, sampleE, utcSec, usOffset); // EHE

        // (b) Band B (50 Hz) & Band S (50 Hz): disampel setiap 2 tick
        if (tickCount % 2 == 0) {
            seedlinkServer.addChannelSample(3, sampleZ, utcSec, usOffset); // BHZ
            seedlinkServer.addChannelSample(4, sampleN, utcSec, usOffset); // BHN
            seedlinkServer.addChannelSample(5, sampleE, utcSec, usOffset); // BHE

            seedlinkServer.addChannelSample(6, sampleZ, utcSec, usOffset); // SHZ
            seedlinkServer.addChannelSample(7, sampleN, utcSec, usOffset); // SHN
            seedlinkServer.addChannelSample(8, sampleE, utcSec, usOffset); // SHE
        }
    }

    if ((currentMicros - lastSampleMicros) > (MASTER_TICK_MICROS * 50)) {
        lastSampleMicros = currentMicros;
    }

    seedlinkServer.handleClient();

    if (Serial.available() > 0) {
        char ch = (char)Serial.read();
        if (ch == 's' || ch == 'S') {
            Serial.println("\n--- STATUS STASIUN SEISMOMETER ANALOG ---");
            Serial.printf(" Uptime         : %lu detik\n", millis() / 1000);
            Serial.printf(" Mode Jaringan  : %s\n", (WiFi.getMode() == WIFI_AP) ? "SoftAP" : "STA");
            Serial.printf(" IP Address     : %s\n", (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString().c_str() : WiFi.localIP().toString().c_str());
            Serial.printf(" Status Klien   : %s\n", seedlinkServer.isClientStreaming() ? "STREAMING (Aktif)" : "MENUNGGU KONEKSI");
            Serial.printf(" Total Paket TX : %lu paket (%lu bytes)\n", 
                          (unsigned long)seedlinkServer.getTotalPacketsSent(),
                          (unsigned long)seedlinkServer.getTotalPacketsSent() * SEEDLINK_PACKET_SIZE);
            Serial.printf(" ADC Raw (Z/N/E): %ld / %ld / %ld\n", (long)lastRawZ, (long)lastRawN, (long)lastRawE);
            Serial.printf(" Seismo Counts  : %ld / %ld / %ld (DC-Blocked)\n", (long)lastCountsZ, (long)lastCountsN, (long)lastCountsE);
            Serial.println("-----------------------------------------\n");
        } 
        else if (ch == 'a' || ch == 'A') {
            Serial.printf("[ADC] Raw:[Z:%ld, N:%ld, E:%ld] | Baseline:[Z:%.1f, N:%.1f, E:%.1f] | FilteredCounts:[Z:%ld, N:%ld, E:%ld]\n",
                          (long)lastRawZ, (long)lastRawN, (long)lastRawE,
                          filterZ.rawAvg, filterN.rawAvg, filterE.rawAvg,
                          (long)lastCountsZ, (long)lastCountsN, (long)lastCountsE);
        }
        else if (ch == 'c' || ch == 'C') {
            filterZ.x1 = (float)lastRawZ;
            filterZ.y1 = 0.0f;
            filterN.x1 = (float)lastRawN;
            filterN.y1 = 0.0f;
            filterE.x1 = (float)lastRawE;
            filterE.y1 = 0.0f;
            Serial.println("[ADC] Baseline DC-Blocker telah dikalibrasi ulang ke titik 0.");
        }
    }

    if (millis() - lastStatusMillis > 30000) {
        lastStatusMillis = millis();
        if (!seedlinkServer.isClientStreaming()) {
            IPAddress ip = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
            Serial.printf("[HEARTBEAT] Analog SeedLink Server siap di %s:%d | Menunggu klien...\n",
                          ip.toString().c_str(), SEEDLINK_PORT);
        }
    }
}

