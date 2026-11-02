#pragma once

#include <Arduino.h>
#include <lwip/def.h>  // Untuk htons, htonl
#if __has_include("config.h")
#include "config.h"
#endif

#ifndef STATION_CODE
#define STATION_CODE "JERIS"
#endif
#ifndef CHANNEL_CODE
#define CHANNEL_CODE "EHZ"
#endif
#ifndef LOCATION_CODE
#define LOCATION_CODE "00"
#endif
#ifndef NETWORK_CODE
#define NETWORK_CODE "AM"
#endif

// Standard MiniSEED record size (512 bytes)
#define MINISEED_RECORD_SIZE    512
// Standard SeedLink packet size: 8 bytes ASCII header + 512 bytes MiniSEED = 520 bytes
#define SEEDLINK_PACKET_SIZE    520
#define SEEDLINK_HEADER_SIZE    8

#pragma pack(push, 1)

// SEED BTIME Structure (10 bytes)
struct MiniSeedBTime {
    uint16_t year;          // Tahun (e.g. 2026, Big-Endian)
    uint16_t day_of_year;   // Hari dalam tahun (1-366, Big-Endian)
    uint8_t  hour;          // Jam (0-23)
    uint8_t  minute;        // Menit (0-59)
    uint8_t  second;        // Detik (0-59)
    uint8_t  unused;        // 0
    uint16_t fract_sec;     // 0.0001 detik (0-9999, Big-Endian)
};

// Fixed Section of Data Header (FSDH) - 48 bytes
struct MiniSeedFSDH {
    char sequence_number[6];        // 6 digit ASCII e.g. "000001"
    char quality_indicator;          // 'D' (Indeterminate/Data)
    char reserved_byte;              // ' ' (Space)
    char station_code[5];            // Station code space-padded, e.g. "ESP01"
    char location_code[2];           // Location code, e.g. "00"
    char channel_code[3];            // Channel code, e.g. "EHZ"
    char network_code[2];            // Network code, e.g. "XX"
    MiniSeedBTime start_time;        // Waktu sample pertama (10 bytes)
    uint16_t num_samples;            // Jumlah sample (Big-Endian)
    int16_t  sample_rate_factor;     // Sample rate factor (e.g. 50, Big-Endian)
    int16_t  sample_rate_multiplier; // Sample rate multiplier (e.g. 1, Big-Endian)
    uint8_t  activity_flags;         // 0
    uint8_t  io_clock_flags;         // 0
    uint8_t  data_quality_flags;     // 0
    uint8_t  num_blockettes_follow;  // 1 (Blockette 1000)
    int32_t  time_correction;        // 0 (Big-Endian)
    uint16_t beginning_of_data;      // Offset awal data (64, Big-Endian)
    uint16_t first_blockette_offset; // Offset blockette pertama (48, Big-Endian)
};

// Blockette 1000 - Data Only SEED Blockette (8 bytes)
struct Blockette1000 {
    uint16_t blockette_type;         // 1000 (0x03E8, Big-Endian)
    uint16_t next_blockette_offset;  // 0 (tidak ada blockette selanjutnya, Big-Endian)
    uint8_t  encoding_format;        // 3 = 32-bit Integer (Big-Endian)
    uint8_t  word_order;             // 1 = Big Endian
    uint8_t  record_length_exponent; // 9 = 2^9 = 512 bytes
    uint8_t  reserved;               // 0
};

#pragma pack(pop)

// MiniSEED Builder Utility
class MiniSeedBuilder {
public:
    // Mengisi buffer paket SeedLink (520 bytes) dari array sampel int32
    static bool buildSeedlinkPacket(
        uint8_t* outPacket520,
        uint32_t packetSeqNum,
        const int32_t* samples,
        uint16_t sampleCount,
        uint16_t sampleRateHz,
        time_t utcEpochSec,
        uint16_t microsecOffset = 0,
        const char* stationCode = STATION_CODE,
        const char* channelCode = CHANNEL_CODE,
        const char* locationCode = LOCATION_CODE,
        const char* networkCode = NETWORK_CODE
    ) {
        if (!outPacket520 || !samples || sampleCount == 0) return false;

        // Bersihkan seluruh buffer 520 byte dengan 0
        memset(outPacket520, 0, SEEDLINK_PACKET_SIZE);

        // 1. SeedLink Header (8 bytes): "SL" + 6-digit Hex Sequence Number (e.g. "SL000001")
        snprintf((char*)outPacket520, 9, "SL%06X", (unsigned int)(packetSeqNum & 0xFFFFFF));

        // Pointer ke record MiniSEED 512-byte (dimulai dari byte ke-8)
        uint8_t* mseedBuf = outPacket520 + SEEDLINK_HEADER_SIZE;

        // 2. FSDH (Fixed Section of Data Header)
        MiniSeedFSDH* fsdh = (MiniSeedFSDH*)mseedBuf;
        
        // Sequence number record (6 digit decimal ASCII)
        char seqStr[7];
        snprintf(seqStr, sizeof(seqStr), "%06u", (unsigned int)(packetSeqNum % 1000000));
        memcpy(fsdh->sequence_number, seqStr, 6);

        fsdh->quality_indicator = 'D';
        fsdh->reserved_byte = ' ';

        // Station (5 chars space padded)
        memset(fsdh->station_code, ' ', 5);
        size_t staLen = strlen(stationCode);
        if (staLen > 5) staLen = 5;
        memcpy(fsdh->station_code, stationCode, staLen);

        // Location (2 chars)
        memset(fsdh->location_code, ' ', 2);
        size_t locLen = strlen(locationCode);
        if (locLen > 2) locLen = 2;
        memcpy(fsdh->location_code, locationCode, locLen);

        // Channel (3 chars)
        memset(fsdh->channel_code, ' ', 3);
        size_t chLen = strlen(channelCode);
        if (chLen > 3) chLen = 3;
        memcpy(fsdh->channel_code, channelCode, chLen);

        // Network (2 chars)
        memset(fsdh->network_code, ' ', 2);
        size_t netLen = strlen(networkCode);
        if (netLen > 2) netLen = 2;
        memcpy(fsdh->network_code, networkCode, netLen);

        // Timestamp konversi ke BTIME
        struct tm tmInfo;
        gmtime_r(&utcEpochSec, &tmInfo);

        fsdh->start_time.year = htons((uint16_t)(tmInfo.tm_year + 1900));
        fsdh->start_time.day_of_year = htons((uint16_t)(tmInfo.tm_yday + 1));
        fsdh->start_time.hour = (uint8_t)tmInfo.tm_hour;
        fsdh->start_time.minute = (uint8_t)tmInfo.tm_min;
        fsdh->start_time.second = (uint8_t)tmInfo.tm_sec;
        fsdh->start_time.unused = 0;
        // Konversi mikrodetik ke unit 0.0001 detik (100 us per count, max 9999)
        uint16_t fract_count = (microsecOffset / 100) % 10000;
        fsdh->start_time.fract_sec = htons(fract_count);

        // Sample count & Rate
        fsdh->num_samples = htons(sampleCount);
        fsdh->sample_rate_factor = htons((int16_t)sampleRateHz);
        fsdh->sample_rate_multiplier = htons((int16_t)1);

        fsdh->activity_flags = 0;
        fsdh->io_clock_flags = 0;
        fsdh->data_quality_flags = 0;
        fsdh->num_blockettes_follow = 1;
        fsdh->time_correction = 0;
        fsdh->beginning_of_data = htons(64);       // Data dimulai di byte 64
        fsdh->first_blockette_offset = htons(48);  // Blockette 1000 ada di byte 48

        // 3. Blockette 1000 (8 bytes pada offset 48)
        Blockette1000* b1000 = (Blockette1000*)(mseedBuf + 48);
        b1000->blockette_type = htons(1000);
        b1000->next_blockette_offset = 0;
        b1000->encoding_format = 3;         // 3 = 32-bit Integer
        b1000->word_order = 1;              // 1 = Big Endian
        b1000->record_length_exponent = 9;  // 2^9 = 512 bytes
        b1000->reserved = 0;

        // Byte 56..63 adalah padding 0

        // 4. Data Samples (mulai byte 64)
        uint8_t* dataPtr = mseedBuf + 64;
        size_t maxSamples = (MINISEED_RECORD_SIZE - 64) / sizeof(int32_t); // (512 - 64)/4 = 112 samples
        if (sampleCount > maxSamples) sampleCount = maxSamples;

        for (uint16_t i = 0; i < sampleCount; ++i) {
            uint32_t sampleBe = htonl((uint32_t)samples[i]);
            memcpy(dataPtr + (i * sizeof(int32_t)), &sampleBe, sizeof(int32_t));
        }

        return true;
    }
};

