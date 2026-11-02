#pragma once

#include <Arduino.h>
#include <WiFi.h>
#if __has_include("config.h")
#include "config.h"
#endif
#include "miniseed.h"

#define MAX_SL_CLIENTS 16

enum SeedLinkClientState {
    SL_STATE_DISCONNECTED,
    SL_STATE_HANDSHAKE,
    SL_STATE_STREAMING
};

class SeedLinkServer {
private:
    WiFiServer _server;
    WiFiClient _clients[MAX_SL_CLIENTS];
    SeedLinkClientState _clientStates[MAX_SL_CLIENTS];
    String _cmdBuffers[MAX_SL_CLIENTS];
    uint16_t _clientChannelMasks[MAX_SL_CLIENTS]; // Bitmask channel yang dipilih per client
    uint32_t _clientConnectTime[MAX_SL_CLIENTS];  // Timestamp connect untuk timeout

    uint32_t _packetSeq;
    uint32_t _totalPacketsSent;

    // Buffer sampel independen untuk 9 saluran FDSN (E, B, S)
    int32_t _channelBuffers[NUM_CHANNELS][MAX_SAMPLES_PER_RECORD];
    uint16_t _channelSampleCounts[NUM_CHANNELS];

    // Timestamp record awal & kontinuitas stream independen per channel
    time_t _channelStartSec[NUM_CHANNELS];
    uint32_t _channelStartMicros[NUM_CHANNELS];
    bool _channelStreaming[NUM_CHANNELS];

    // Packet 520-byte outbound buffer
    uint8_t _packetBuffer[SEEDLINK_PACKET_SIZE];

    // Selector matching logic (mendukung "EHZ", "00EHZ.D", "?HZ", "*", "EH*", "BH*", dll.)
    bool matchSelector(const String& sel, const char* code) {
        String s = sel;
        s.toUpperCase();
        s.trim();
        if (s == "*" || s == "*.*" || s.length() == 0) return true;
        if (s.indexOf(code) >= 0) return true;

        String cleanSel = s;
        if (cleanSel.length() >= 5 && (cleanSel.endsWith(".D") || cleanSel.endsWith(".?"))) {
            cleanSel = cleanSel.substring(2, 5);
        } else if (cleanSel.length() >= 3 && cleanSel.endsWith(".D")) {
            cleanSel = cleanSel.substring(0, cleanSel.length() - 2);
        }

        if (cleanSel == "*" || cleanSel == "???") return true;
        if (cleanSel.length() == 3) {
            for (int i = 0; i < 3; ++i) {
                char c = cleanSel[i];
                if (c == '*' || c == '?') continue;
                if (c != code[i]) return false;
            }
            return true;
        }
        return false;
    }

    // Mengirim paket respons metadata XML (SLINFO) yang wajib untuk discovery Swarm / ObsPy / SeisComP
    void sendInfoXml(WiFiClient& client, const char* xmlStr) {
        if (!client || !client.connected()) return;

        size_t totalLen = strlen(xmlStr);
        size_t offset = 0;

        while (offset < totalLen) {
            size_t chunkLen = totalLen - offset;
            if (chunkLen > 448) chunkLen = 448; // Max MiniSEED text payload di record 512 byte
            bool isLast = (offset + chunkLen >= totalLen);

            uint8_t infoPkt[SEEDLINK_PACKET_SIZE];
            memset(infoPkt, 0, sizeof(infoPkt));

            // 1. SeedLink Header (8 bytes): "SLINFO  " (terakhir) atau "SLINFO *" (bersambung)
            if (isLast) {
                memcpy(infoPkt, "SLINFO  ", 8);
            } else {
                memcpy(infoPkt, "SLINFO *", 8);
            }

            uint8_t* mseed = infoPkt + 8;
            MiniSeedFSDH* fsdh = (MiniSeedFSDH*)mseed;

            memcpy(fsdh->sequence_number, "000000", 6);
            fsdh->quality_indicator = 'D';
            fsdh->reserved_byte = ' ';

            memset(fsdh->station_code, ' ', 5);
            size_t staLen = strlen(STATION_CODE);
            if (staLen > 5) staLen = 5;
            memcpy(fsdh->station_code, STATION_CODE, staLen);

            memset(fsdh->location_code, ' ', 2);
            size_t locLen = strlen(LOCATION_CODE);
            if (locLen > 2) locLen = 2;
            memcpy(fsdh->location_code, LOCATION_CODE, locLen);

            memcpy(fsdh->channel_code, "LOG", 3);
            memset(fsdh->network_code, ' ', 2);
            size_t netLen = strlen(NETWORK_CODE);
            if (netLen > 2) netLen = 2;
            memcpy(fsdh->network_code, NETWORK_CODE, netLen);

            time_t nowSec = _channelStartSec[0] ? _channelStartSec[0] : time(NULL);
            struct tm tmInfo;
            gmtime_r(&nowSec, &tmInfo);
            fsdh->start_time.year = htons((uint16_t)(tmInfo.tm_year + 1900));
            fsdh->start_time.day_of_year = htons((uint16_t)(tmInfo.tm_yday + 1));
            fsdh->start_time.hour = (uint8_t)tmInfo.tm_hour;
            fsdh->start_time.minute = (uint8_t)tmInfo.tm_min;
            fsdh->start_time.second = (uint8_t)tmInfo.tm_sec;
            fsdh->start_time.unused = 0;
            fsdh->start_time.fract_sec = 0;

            fsdh->num_samples = htons((uint16_t)chunkLen);
            fsdh->sample_rate_factor = 0;
            fsdh->sample_rate_multiplier = 0;
            fsdh->activity_flags = 0;
            fsdh->io_clock_flags = 0;
            fsdh->data_quality_flags = 0;
            fsdh->num_blockettes_follow = 1;
            fsdh->time_correction = 0;
            fsdh->beginning_of_data = htons(64);
            fsdh->first_blockette_offset = htons(48);

            // Blockette 1000 (8 bytes) dengan encoding 0 (ASCII text)
            Blockette1000* b1000 = (Blockette1000*)(mseed + 48);
            b1000->blockette_type = htons(1000);
            b1000->next_blockette_offset = 0;
            b1000->encoding_format = 0; // 0 = ASCII text
            b1000->word_order = 1;      // Big Endian
            b1000->record_length_exponent = 9; // 512 bytes
            b1000->reserved = 0;

            // Tulis teks XML mulai offset 64
            memcpy(mseed + 64, xmlStr + offset, chunkLen);

            client.write(infoPkt, SEEDLINK_PACKET_SIZE);
            client.flush();
            delay(5); // Jeda 5ms agar buffer socket tidak tertekan
            offset += chunkLen;
        }
        Serial.printf("[SEEDLINK] TX: SLINFO XML Packet dikirim (%u bytes)\n", (unsigned int)totalLen);
    }

    String generateStreamsXml() {
        String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
        xml += "<seedlink software=\"ESP32-Seismometer\" organization=\"" + String(STATION_NAME) + "\" version=\"3.1\">";
        xml += "<station name=\"" + String(STATION_CODE) + "\" network=\"" + String(NETWORK_CODE) + "\" net=\"" + String(NETWORK_CODE) + "\" description=\"" + String(STATION_NAME) + "\">";
        for (size_t i = 0; i < NUM_CHANNELS; ++i) {
            String ch = AVAILABLE_CHANNELS[i].code;
            String loc = AVAILABLE_CHANNELS[i].location;
            String rate = String(AVAILABLE_CHANNELS[i].sampleRate);
            xml += "<stream location=\"" + loc + "\" seedname=\"" + ch + "\" schan=\"" + ch + "\" slocation=\"" + loc + "\" type=\"D\" rate=\"" + rate + "\"/>";
        }
        xml += "</station>";
        xml += "</seedlink>";
        return xml;
    }

    void processCommandLine(int idx, const String& cmd) {
        String trimmed = cmd;
        trimmed.trim();
        if (trimmed.length() == 0) return;

        WiFiClient& client = _clients[idx];
        Serial.printf("[SEEDLINK #%d] RX: %s\n", idx, trimmed.c_str());

        int spaceIdx = trimmed.indexOf(' ');
        String mainCmd = (spaceIdx > 0) ? trimmed.substring(0, spaceIdx) : trimmed;
        mainCmd.toUpperCase();

        String arg = (spaceIdx > 0) ? trimmed.substring(spaceIdx + 1) : "";
        arg.trim();
        arg.toUpperCase();

        if (mainCmd == "HELLO") {
            String resp = "SeedLink v3.1 (ESP32-Seismometer) :: SLPROTO:3.1\r\n";
            resp += "ESP32 Seismometer Station " + String(STATION_CODE) + "\r\n";
            client.print(resp);
            Serial.printf("[SEEDLINK #%d] TX: HELLO banner sent (Station %s).\n", idx, STATION_CODE);
        }
        else if (mainCmd == "INFO") {
            if (arg == "ID") {
                String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><seedlink software=\"ESP32-Seismometer\" organization=\"" + String(STATION_NAME) + "\" version=\"3.1\"/>";
                sendInfoXml(client, xml.c_str());
            }
            else if (arg == "STATIONS") {
                String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><seedlink><station name=\"" + String(STATION_CODE) + "\" network=\"" + String(NETWORK_CODE) + "\" net=\"" + String(NETWORK_CODE) + "\" description=\"" + String(STATION_NAME) + "\"/></seedlink>";
                sendInfoXml(client, xml.c_str());
            }
            else if (arg == "CAPABILITIES") {
                String xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><seedlink><capability name=\"info:all\"/><capability name=\"info:id\"/><capability name=\"info:stations\"/><capability name=\"info:streams\"/><capability name=\"multistation\"/></seedlink>";
                sendInfoXml(client, xml.c_str());
            }
            else {
                // Default / STREAMS / ALL: XML channel info yang wajib dibaca oleh SWARM
                String xml = generateStreamsXml();
                sendInfoXml(client, xml.c_str());
            }
        }
        else if (mainCmd == "CAT") {
            client.print(String(NETWORK_CODE) + " " + String(STATION_CODE) + " " + String(STATION_NAME) + "\r\nEND\r\n");
            Serial.printf("[SEEDLINK #%d] TX: CAT response sent.\n", idx);
        }
        else if (mainCmd == "STATION") {
            // Reset filter channel untuk station ini
            _clientChannelMasks[idx] = 0;
            client.print("OK\r\n");
            Serial.printf("[SEEDLINK #%d] TX: OK (STATION %s)\n", idx, arg.c_str());
        }
        else if (mainCmd == "SELECT") {
            // Periksa matching channel terhadap selector argumen
            for (size_t c = 0; c < NUM_CHANNELS; ++c) {
                if (matchSelector(arg, AVAILABLE_CHANNELS[c].code)) {
                    _clientChannelMasks[idx] |= (1 << c);
                }
            }
            client.print("OK\r\n");
            Serial.printf("[SEEDLINK #%d] TX: OK (SELECT %s -> mask: 0x%03X)\n", idx, arg.c_str(), _clientChannelMasks[idx]);
        }
        else if (mainCmd == "DATA" || mainCmd == "FETCH") {
            client.print("OK\r\n");
            Serial.printf("[SEEDLINK #%d] TX: OK\n", idx);
        }
        else if (mainCmd == "USERAGENT" || mainCmd == "TIME" || mainCmd == "SLPROTO") {
            client.print("OK\r\n");
            Serial.printf("[SEEDLINK #%d] TX: OK\n", idx);
        }
        else if (mainCmd == "END") {
            // Jika klien tidak mengirim SELECT sama sekali, aktifkan seluruh saluran secara default
            if (_clientChannelMasks[idx] == 0) {
                _clientChannelMasks[idx] = (uint16_t)((1 << NUM_CHANNELS) - 1);
            }
            Serial.printf("[SEEDLINK #%d] Handshake Selesai! Masuk ke Mode STREAMING (Channels Mask: 0x%03X).\n", 
                          idx, _clientChannelMasks[idx]);
            _clientStates[idx] = SL_STATE_STREAMING;
        }
        else if (mainCmd == "BYE") {
            Serial.printf("[SEEDLINK #%d] Client meminta BYE. Menutup koneksi.\n", idx);
            client.stop();
            _clientStates[idx] = SL_STATE_DISCONNECTED;
            _clientChannelMasks[idx] = 0;
        }
        else {
            client.print("OK\r\n");
            Serial.printf("[SEEDLINK #%d] TX: OK (permissive default for: %s)\n", idx, mainCmd.c_str());
        }
    }

public:
    SeedLinkServer(uint16_t port = SEEDLINK_PORT)
        : _server(port),
          _packetSeq(1),
          _totalPacketsSent(0)
    {
        for (int i = 0; i < MAX_SL_CLIENTS; ++i) {
            _clientStates[i] = SL_STATE_DISCONNECTED;
            _cmdBuffers[i] = "";
            _clientChannelMasks[i] = 0;
            _clientConnectTime[i] = 0;
        }
        for (size_t c = 0; c < NUM_CHANNELS; ++c) {
            _channelSampleCounts[c] = 0;
            _channelStartSec[c] = 0;
            _channelStartMicros[c] = 0;
            _channelStreaming[c] = false;
        }
    }

    void begin() {
        _server.begin();
        for (size_t c = 0; c < NUM_CHANNELS; ++c) {
            _channelSampleCounts[c] = 0;
            _channelStartSec[c] = 0;
            _channelStartMicros[c] = 0;
            _channelStreaming[c] = false;
        }
        Serial.printf("[SEEDLINK] Multi-client Server (Max %d) aktif di port %d untuk Stasiun %s (%u Channels: E@100Hz, B@50Hz, S@50Hz)\n", 
                      MAX_SL_CLIENTS, SEEDLINK_PORT, STATION_CODE, (unsigned int)NUM_CHANNELS);
    }

    void handleClient() {
        uint32_t nowMs = millis();

        // 1. Bersihkan dan periksa status koneksi seluruh slot klien
        for (int i = 0; i < MAX_SL_CLIENTS; ++i) {
            if (_clientStates[i] != SL_STATE_DISCONNECTED) {
                if (!_clients[i] || !_clients[i].connected()) {
                    Serial.printf("[SEEDLINK] Klien #%d terputus. Mengosongkan slot.\n", i);
                    _clients[i].stop();
                    _clientStates[i] = SL_STATE_DISCONNECTED;
                    _cmdBuffers[i] = "";
                    _clientChannelMasks[i] = 0;
                }
                else if (_clientStates[i] == SL_STATE_HANDSHAKE && (nowMs - _clientConnectTime[i] > 15000)) {
                    Serial.printf("[SEEDLINK] Klien #%d timeout handshake (15s). Menutup koneksi.\n", i);
                    _clients[i].stop();
                    _clientStates[i] = SL_STATE_DISCONNECTED;
                    _cmdBuffers[i] = "";
                    _clientChannelMasks[i] = 0;
                }
            }
        }

        // 2. Terima koneksi klien baru jika ada slot kosong
        if (_server.hasClient()) {
            WiFiClient incoming = _server.accept();
            if (incoming) {
                int assignedIdx = -1;
                for (int i = 0; i < MAX_SL_CLIENTS; ++i) {
                    if (_clientStates[i] == SL_STATE_DISCONNECTED || !_clients[i] || !_clients[i].connected()) {
                        assignedIdx = i;
                        break;
                    }
                }

                if (assignedIdx >= 0) {
                    incoming.setNoDelay(true); // Nonaktifkan algoritma Nagle untuk transmisi real-time instan
                    _clients[assignedIdx] = incoming;
                    _clientStates[assignedIdx] = SL_STATE_HANDSHAKE;
                    _cmdBuffers[assignedIdx] = "";
                    _clientChannelMasks[assignedIdx] = 0;
                    _clientConnectTime[assignedIdx] = nowMs;
                    Serial.printf("[SEEDLINK] Klien baru masuk pada slot #%d dari %s:%d\n",
                                  assignedIdx,
                                  _clients[assignedIdx].remoteIP().toString().c_str(),
                                  _clients[assignedIdx].remotePort());
                } else {
                    Serial.println("[SEEDLINK] Server penuh! Menolak koneksi klien baru.");
                    incoming.stop();
                }
            }
        }

        // 3. Baca perintah dari seluruh klien yang terhubung
        for (int i = 0; i < MAX_SL_CLIENTS; ++i) {
            if (_clients[i] && _clients[i].connected()) {
                while (_clients[i].available() > 0) {
                    char c = (char)_clients[i].read();
                    if (c == '\r' || c == '\n') {
                        if (_cmdBuffers[i].length() > 0) {
                            processCommandLine(i, _cmdBuffers[i]);
                            _cmdBuffers[i] = "";
                        }
                    } else {
                        if (_cmdBuffers[i].length() < 256) {
                            _cmdBuffers[i] += c;
                        }
                    }
                }
            }
        }
    }

    // Memasukkan sampel untuk satu saluran dengan kontinuitas waktu matematis independen
    void addChannelSample(size_t chIdx, int32_t sample, time_t currentUtcSec, uint32_t currentMicros) {
        if (chIdx >= NUM_CHANNELS) return;

        if (_channelSampleCounts[chIdx] == 0) {
            // Toleransi drift dilonggarkan ke 60 detik agar jitter kristal/NTP tidak memicu gap mendadak
            if (!_channelStreaming[chIdx] || _channelStartSec[chIdx] == 0 || abs((long)(currentUtcSec - _channelStartSec[chIdx])) > 60) {
                _channelStartSec[chIdx] = currentUtcSec;
                _channelStartMicros[chIdx] = currentMicros % 1000000UL;
                _channelStreaming[chIdx] = true;
            }
        }

        _channelBuffers[chIdx][_channelSampleCounts[chIdx]] = sample;
        _channelSampleCounts[chIdx]++;

        if (_channelSampleCounts[chIdx] >= AVAILABLE_CHANNELS[chIdx].samplesPerRecord) {
            flushChannel(chIdx);
        }
    }

    void flushChannel(size_t chIdx) {
        if (chIdx >= NUM_CHANNELS) return;
        uint16_t sampleCount = _channelSampleCounts[chIdx];
        if (sampleCount == 0) return;

        // Cek apakah ada klien aktif yang membutuhkan saluran ini
        bool needed = false;
        for (int i = 0; i < MAX_SL_CLIENTS; ++i) {
            if (_clients[i] && _clients[i].connected() && _clientStates[i] == SL_STATE_STREAMING) {
                if ((_clientChannelMasks[i] & (1 << chIdx)) != 0) {
                    needed = true;
                    break;
                }
            }
        }

        if (needed) {
            uint16_t usOffset = (_channelStartMicros[chIdx] % 1000000UL);
            bool ok = MiniSeedBuilder::buildSeedlinkPacket(
                _packetBuffer,
                _packetSeq,
                _channelBuffers[chIdx],
                sampleCount,
                AVAILABLE_CHANNELS[chIdx].sampleRate,
                _channelStartSec[chIdx],
                usOffset,
                STATION_CODE,
                AVAILABLE_CHANNELS[chIdx].code,
                AVAILABLE_CHANNELS[chIdx].location,
                NETWORK_CODE
            );

            if (ok) {
                for (int i = 0; i < MAX_SL_CLIENTS; ++i) {
                    if (_clients[i] && _clients[i].connected() && _clientStates[i] == SL_STATE_STREAMING) {
                        if ((_clientChannelMasks[i] & (1 << chIdx)) != 0) {
                            // Penulisan non-blocking langsung ke ring buffer LwIP (jangan gunakan flush() karena memblokir CPU menunggu ACK)
                            size_t written = _clients[i].write(_packetBuffer, SEEDLINK_PACKET_SIZE);
                            if (written != SEEDLINK_PACKET_SIZE) {
                                Serial.printf("[SEEDLINK] Write error ke klien #%d (%u/520). Menutup socket.\n",
                                              i, (unsigned int)written);
                                _clients[i].stop();
                                _clientStates[i] = SL_STATE_DISCONNECTED;
                                _clientChannelMasks[i] = 0;
                            }
                        }
                    }
                }
                _packetSeq++;
                if (_packetSeq > 0xFFFFFF) _packetSeq = 1;
                _totalPacketsSent++;
                if (_totalPacketsSent % 25 == 0 || _totalPacketsSent <= 5) {
                    Serial.printf("[SEEDLINK] Paket #%lu terkirim: %s (%d smpl @ %dHz)\n",
                                  (unsigned long)_totalPacketsSent, AVAILABLE_CHANNELS[chIdx].code, sampleCount, AVAILABLE_CHANNELS[chIdx].sampleRate);
                }
            }
        }

        // Majukan timestamp matematis secara persis sebesar durasi record
        // untuk menjamin kontinuitas tanpa celah (zero-gap) di SWARM / ObsPy
        uint32_t durationMicros = ((uint32_t)sampleCount * 1000000UL) / AVAILABLE_CHANNELS[chIdx].sampleRate;
        _channelStartMicros[chIdx] += durationMicros;
        _channelStartSec[chIdx] += (_channelStartMicros[chIdx] / 1000000UL);
        _channelStartMicros[chIdx] %= 1000000UL;
        _channelSampleCounts[chIdx] = 0;
    }

    bool isClientStreaming() {
        for (int i = 0; i < MAX_SL_CLIENTS; ++i) {
            if (_clientStates[i] == SL_STATE_STREAMING && _clients[i] && _clients[i].connected()) {
                return true;
            }
        }
        return false;
    }

    uint32_t getTotalPacketsSent() const {
        return _totalPacketsSent;
    }
};
