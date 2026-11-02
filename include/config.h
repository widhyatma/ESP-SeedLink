#pragma once

#include <Arduino.h>

// ==============================================================================
// SEISMOMETER & SEEDLINK CONFIGURATION
// ==============================================================================

// Prioritized Wi-Fi Credentials List (Diurutkan dari prioritas tertinggi ke cadangan/backup)
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

const size_t NUM_WIFI_NETWORKS = sizeof(WIFI_SSID) / sizeof(WIFI_SSID[0]);

// Fallback SoftAP Mode (Jika Wi-Fi router tidak terhubung)
#define AP_SSID             "ESP32-SEISMOMETER"
#define AP_PASSWORD         "seismo1234"

// SeedLink Server Configuration
#define SEEDLINK_PORT       18000       // Standar port FDSN SeedLink

// Seismic Station Metadata (FDSN Standard)
#define STATION_CODE        "JERIS"     // Kode stasiun (Jerukagung Seismologi, maks 5 karakter)
#define STATION_NAME        "Jerukagung Seismologi"
#define NETWORK_CODE        "AM"        // Kode network (2 karakter, AM = Raspberry Shake / Citizen Science Seismology)
#define LOCATION_CODE       "00"        // Kode lokasi (2 karakter, "00" = primary sensor)
#define CHANNEL_CODE        "EHZ"       // Saluran default

// Channel definitions (9 Standard FDSN Multi-Rate Seismic Channels: E, B, S)
// E = Extremely Short Period / High Rate (100 Hz, 100 sample/detik per record)
// B = Broadband (50 Hz, 50 sample/detik per record)
// S = Short Period (50 Hz, 50 sample/detik per record)
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

// Sampling Configuration
#define MASTER_SAMPLING_RATE_HZ 100     // Master loop tick pada 100 Hz (10,000 us)
#define SAMPLING_RATE_HZ        100     // Kompatibilitas alias
#define MAX_SAMPLES_PER_RECORD  100     // Maksimum buffer per channel (100 sampel)
#define SAMPLES_PER_RECORD      100     // Kompatibilitas alias

// NTP Server Configuration (untuk presisi timestamp BTIME MiniSEED)
#define NTP_SERVER_1        "pool.ntp.org"
#define NTP_SERVER_2        "time.nist.gov"
#define NTP_SERVER_3        "id.pool.ntp.org"
#define GMT_OFFSET_SEC      0           // SeedLink / MiniSEED mewajibkan UTC (GMT+0)
#define DAYLIGHT_OFFSET_SEC 0

// Dummy Seismic Signal Simulator Configuration (Sinyal Seismometer Alami & Wajar)
#define SEISMIC_NOISE_STD_DEV   120     // Amplitudo standar deviasi background noise alami (+/- 150 s/d +/- 250 counts)
#define ENABLE_AUTO_QUAKE       false   // false = sinyal natural resting seismometer continuous; true = gempa periodik
#define EARTHQUAKE_PERIOD_SEC   300     // Jika diaktifkan, interval simulasi gempa (e.g. 5 menit)
#define QUAKE_MAX_AMPLITUDE     1800    // Amplitudo puncak gempa wajar (counts, tidak over-clip di SWARM)
